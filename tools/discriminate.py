#!/usr/bin/env python3
"""Eliminate candidate toolchains by compiling one function against all of them.

Synthetic probes cannot separate these compilers: a construct with one sensible
encoding is emitted identically by every candidate. Real functions have room to
disagree, so this compiles a single decompiled function against the whole
candidate set and reports where they diverge.

Two signals, in order of strength:

* **Instruction count.** If a compiler cannot emit a function of the retail
  length from this source at any setting, it did not build it. This is
  structural and survives an imperfect decompilation, because it does not
  require the C to be byte-correct — only close enough that the right compiler
  reaches the right shape.
* **Match ratio.** Among compilers that do reach the retail length, the share
  of instruction words matching retail. Weaker, since a wrong decompilation
  moves every candidate's ratio, but the *spread* between them is informative.

A ratio is only ever compared across candidates built from the same source.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import build_candidate
import match_function
from retail_common import RetailError, load_json, sha256_file

DEFAULT_OPTIMIZATIONS = ("-O1", "-O2")
DEFAULT_GP = ("-G0",)


@dataclass(frozen=True)
class CandidateResult:
    toolchain: str
    optimization: str
    gp: str
    size_bytes: int | None
    ratio: float | None

    @property
    def comparable(self) -> bool:
        """True when this build could be compared with retail at all.

        A ratio is computed only for a build that reached the retail length, so
        its presence is exactly the comparability condition. A build that failed
        or came out a different size has none.
        """

        return self.ratio is not None


def summarise(results: list[CandidateResult], retail_size: int) -> dict[str, object]:
    """Reduce per-configuration results to surviving and eliminated toolchains.

    A toolchain survives if *any* of its configurations reached the retail
    instruction count, and is scored by its best configuration. One failed
    build does not condemn a toolchain that succeeded elsewhere.
    """

    if not results:
        raise RetailError("cannot summarise an empty result set")

    scores: dict[str, float | None] = {}
    for result in results:
        scores.setdefault(result.toolchain, None)
        if result.size_bytes != retail_size:
            continue
        ratio = result.ratio if result.ratio is not None else 0.0
        current = scores[result.toolchain]
        if current is None or ratio > current:
            scores[result.toolchain] = ratio

    # An exact match proves the source. Once the C is known correct, a
    # candidate that cannot reproduce it is eliminated outright — a far
    # stronger criterion than instruction count, and one that carries no
    # "conditional on the decompilation" caveat.
    source_verified = any(score == 1.0 for score in scores.values())

    def survives(score: float | None) -> bool:
        if source_verified:
            return score == 1.0
        return score is not None

    surviving = sorted(
        (name for name, score in scores.items() if survives(score)),
        key=lambda name: (-(scores[name] or 0.0), name),
    )
    eliminated = sorted(name for name, score in scores.items() if not survives(score))

    best: tuple[str, float] | None = None
    spread = 0.0
    if surviving:
        best = (surviving[0], scores[surviving[0]] or 0.0)
        worst = scores[surviving[-1]] or 0.0
        spread = best[1] - worst

    # With no survivor there is no baseline to eliminate anything against: the
    # source failed to reproduce the retail shape under every candidate, which
    # indicts the decompilation rather than the compilers.
    inconclusive = not surviving

    return {
        "surviving": surviving,
        "eliminated": eliminated,
        "best": best,
        "spread": spread,
        "inconclusive": inconclusive,
        "source_verified": source_verified,
        "discriminating": bool(eliminated) and not inconclusive,
    }


def _build_one(
    source: Path,
    symbol: str,
    toolchain: str,
    optimization: str,
    gp: str,
    output: Path,
    extra: list[str],
) -> int | None:
    """Build one configuration, returning the produced size or None on failure."""

    argv = [
        str(source),
        "--symbol",
        symbol,
        "--toolchain",
        toolchain,
        f"--optimization={optimization}",
        f"--gp={gp}",
        "--output",
        str(output),
        *extra,
    ]
    output.unlink(missing_ok=True)
    sink = io.StringIO()
    with contextlib.redirect_stdout(sink), contextlib.redirect_stderr(sink):
        code = build_candidate.main(argv)
    if code != 0 or not output.is_file():
        return None
    return output.stat().st_size


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="C file to compile")
    parser.add_argument("--symbol", required=True, help="function symbol to extract")
    parser.add_argument("--vram", required=True, help="retail address, e.g. 0x80012E6C")
    parser.add_argument("--size", required=True, help="retail size in bytes, e.g. 0x108")
    parser.add_argument("--identity", type=Path, help="observed identity JSON")
    parser.add_argument(
        "--retail-file",
        type=Path,
        help="compare against a flat blob instead of the executable, for overlay code",
    )
    parser.add_argument("--base", help="load address of --retail-file")
    parser.add_argument("--sha256", help="pinned digest of --retail-file")
    parser.add_argument("--toolchain-root", type=Path)
    parser.add_argument("--maspsx", type=Path)
    parser.add_argument(
        "--optimization",
        action="append",
        dest="optimizations",
        help="use the = form; repeatable. Default: -O1 and -O2",
    )
    parser.add_argument("--gp", action="append", dest="gps", help="use the = form; repeatable")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repo = Path(__file__).resolve().parents[1]
    identity_path = (args.identity or repo / "provenance/exe_identity.json").resolve()
    try:
        vram = int(str(args.vram), 0)
        size = int(str(args.size), 0)

        if args.retail_file is not None:
            # Overlay code, addressed by its load base and pinned by hash for the
            # same reason the executable is.
            if args.base is None or args.sha256 is None:
                raise RetailError("--retail-file requires both --base and --sha256")
            base = int(str(args.base), 0)
            if not args.retail_file.is_file():
                raise RetailError(f"retail blob not found: {args.retail_file}")
            actual = sha256_file(args.retail_file)
            if actual.lower() != str(args.sha256).lower():
                raise RetailError(
                    f"blob SHA-256 mismatch for {args.retail_file}: expected {args.sha256}, got {actual}"
                )
            retail = match_function.extract_blob_bytes(args.retail_file, base, vram, size)
        else:
            identity = load_json(identity_path)
            exe = match_function._resolve_exe(repo, identity)
            header = identity.get("header")
            if not isinstance(header, dict):
                raise RetailError("executable identity lacks a header object")
            retail = match_function.extract_retail_bytes(exe, header, vram, size)

        extra: list[str] = ["--link-base", hex(vram)]
        if args.toolchain_root:
            extra += ["--toolchain-root", str(args.toolchain_root)]
        if args.maspsx:
            extra += ["--maspsx", str(args.maspsx)]

        optimizations = tuple(args.optimizations or DEFAULT_OPTIMIZATIONS)
        gps = tuple(args.gps or DEFAULT_GP)

        workspace = Path(tempfile.mkdtemp(prefix="discriminate."))
        results: list[CandidateResult] = []
        print(f"retail {args.symbol} at 0x{vram:08X}: {size} bytes ({size // 4} words)\n")
        print(f"{'toolchain':<20}{'opt':<6}{'gp':<6}{'words':<8}{'ratio'}")
        try:
            for toolchain in sorted(build_candidate.TOOLCHAINS):
                for optimization in optimizations:
                    for gp in gps:
                        output = workspace / "candidate.bin"
                        produced = _build_one(
                            args.source, args.symbol, toolchain, optimization, gp, output, extra
                        )
                        ratio = None
                        if produced == size:
                            ratio = match_function.compare_words(output.read_bytes(), retail)[
                                "match_ratio"
                            ]
                        results.append(
                            CandidateResult(toolchain, optimization, gp, produced, ratio)
                        )
                        if produced is None:
                            rendered, note = "-", "build failed"
                        elif produced != size:
                            rendered, note = str(produced // 4), "size differs"
                        else:
                            rendered, note = str(produced // 4), f"{(ratio or 0.0) * 100:.2f}%"
                        print(f"{toolchain:<20}{optimization:<6}{gp:<6}{rendered:<8}{note}")
        finally:
            import shutil

            shutil.rmtree(workspace, ignore_errors=True)

        summary = summarise(results, size)
        print()
        if summary["inconclusive"]:
            print("INCONCLUSIVE: no candidate reached the retail instruction count.")
            print("  This indicts the decompiled source, not the compilers: with no")
            print("  candidate reproducing the retail shape there is nothing to")
            print("  eliminate against. Refine the C and re-run.")
            return 0

        if summary["source_verified"]:
            print("SOURCE VERIFIED: at least one candidate reproduces retail exactly,")
            print("  so the C is known correct and failure to reproduce it is decisive.")
        else:
            print("SOURCE UNVERIFIED: no candidate reproduces retail exactly, so only")
            print("  instruction count is usable and results depend on the C being close.")

        if summary["eliminated"]:
            reason = (
                "did not reproduce the verified source exactly"
                if summary["source_verified"]
                else "never reached the retail instruction count at any tested setting"
            )
            print(f"ELIMINATED ({len(summary['eliminated'])}): {', '.join(summary['eliminated'])}")
            print(f"  {reason}")
        else:
            print("ELIMINATED (0): no candidate was ruled out")
        print(f"SURVIVING ({len(summary['surviving'])}): {', '.join(summary['surviving'])}")
        best = summary["best"]
        print(f"  best {best[0]} at {best[1] * 100:.2f}%, spread {summary['spread'] * 100:.2f} points")
        if not summary["discriminating"]:
            print("\nNOT DISCRIMINATING: no candidate was eliminated by this function")
        return 0
    except RetailError as exc:
        print(f"discriminate: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
