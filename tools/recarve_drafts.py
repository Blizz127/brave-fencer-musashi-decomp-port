#!/usr/bin/env python3
"""Re-decompile the unverified drafts that do not compile, and only those.

A draft that does not compile can never reach the oracle, so it is dead
weight: `batch_match.py --register-existing` reports `build-failed` and
moves on. Most of those failures had one cause. m2c, run in its default
mode, spells an access into an untyped global `NAME.unkHHHH`, while the
sanitizer has already had to declare NAME a plain scalar, so the retail
compiler rejects the member access. `--valid-syntax` emits the same access
as M2C_FIELD carrying the width m2c inferred, which compiles.

This tool is deliberately narrow, because the sources it edits are checked
work:

* A function already in the registry is never touched.
* A body that does not carry the UNVERIFIED draft marker is never touched.
  A verified match, a hand-written body, and an assembly overlay all stay
  exactly as they are.
* A draft that already compiles is never touched. Being close enough to
  build is progress; replacing it with a fresh guess would throw that away.
* A legacy `MUSASHI_NATIVE_MIPS_WORD_EXPORT` block, if a draft still has
  one, is copied across verbatim and compared word for word afterwards.
  Tracked sources no longer carry these blocks (retail words are not
  committed; tests/test_no_retail_words.py), and the port reads code from
  the user's disc at run time.
* A regenerated body that still does not compile is discarded, leaving the
  original file on disk.

Nothing here claims a match. Every file it writes is still an unverified
draft; only tools/match_function.py can promote one.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import re
import sys
import tempfile
from pathlib import Path

import batch_match
import build_candidate
from retail_common import RetailError

REGIONS = {
    "main": "asm/main.s",
    "main_0007": "asm/overlays/main_0007/main_0007.s",
    "main_0012": "asm/overlays/main_0012/main_0012.s",
}

EXPORT_WORD_RE = re.compile(r"MUSASHI_NATIVE_MIPS_WORD\(0x([0-9a-fA-F]{8})\)")
DRAFT_MARKER = "UNVERIFIED draft"
IFDEF = "#ifdef MUSASHI_NATIVE_MIPS_WORD_EXPORT"
_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)


def claims_nothing(body: str) -> bool:
    """True when the `#else` side holds no C implementation at all.

    These are the spans m2c refused outright, carrying only the word export
    and a note saying so. There is nothing to preserve, so replacing the
    body cannot lose work — which is why they are eligible even though they
    do not carry the UNVERIFIED-draft marker that a real draft carries.
    """

    return "{" not in _COMMENT_RE.sub("", body)


def split_dual_shape(text: str) -> tuple[str, str] | None:
    """Return (export block, body) for a word-export/`#else` source.

    Returns None for any other shape, which is then left alone rather than
    guessed at.
    """

    if IFDEF not in text or "#else" not in text:
        return None
    head, rest = text.split(IFDEF, 1)
    if "#else" not in rest:
        return None
    export, body = rest.split("#else", 1)
    if not body.rstrip().endswith("#endif"):
        return None
    return head + IFDEF + export, body.rstrip()[: -len("#endif")]


def builds(source: Path, symbol: str, vram: int, scratch: Path,
           errors: list[str] | None = None) -> bool:
    """True when at least one candidate optimization produces bytes.

    When `errors` is given, the compiler diagnostic from the last failing
    attempt is appended to it. Knowing *why* a fresh draft still will not
    compile is the only way to find the next systemic fix, and the whole
    point of this tool is that a draft which cannot compile is invisible to
    the oracle.
    """

    candidate = scratch / "probe.bin"
    last = ""
    for optimization in batch_match.OPTIMIZATION_CANDIDATES:
        candidate.unlink(missing_ok=True)
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured), contextlib.redirect_stderr(captured):
            try:
                code = build_candidate.main(
                    [
                        str(source),
                        "--symbol", symbol,
                        "--link-base", f"0x{vram:X}",
                        f"--optimization={optimization}",
                        "--output", str(candidate),
                    ]
                )
            except SystemExit as exit_request:
                code = exit_request.code
            except Exception as failure:  # noqa: BLE001 - diagnostic only
                code = 1
                captured.write(repr(failure))
        if code == 0 and candidate.is_file():
            return True
        last = captured.getvalue()
    if errors is not None:
        errors.append(last)
    return False


def recarve_region(region: str, repo: Path, registered: set[int], limit: int | None,
                   dry_run: bool, error_log: Path | None = None) -> dict[str, int]:
    asm = repo / REGIONS[region]
    text = asm.read_text()
    m2c = repo / "tools" / "m2c" / "m2c.py"
    counts: dict[str, int] = {}

    def bump(key: str) -> None:
        counts[key] = counts.get(key, 0) + 1

    attempted = 0
    with tempfile.TemporaryDirectory() as directory:
        scratch = Path(directory)
        for function in batch_match.enumerate_functions(text):
            if function.vram in registered:
                continue
            target = batch_match.source_path(repo, region, function.vram)
            if not target.is_file():
                bump("no-source")
                continue
            original = target.read_text()
            parts = split_dual_shape(original)
            if parts is None:
                bump("skipped-shape")
                continue
            export, existing_body = parts
            if DRAFT_MARKER not in original and not claims_nothing(existing_body):
                bump("skipped-not-a-draft")
                continue
            if limit is not None and attempted >= limit:
                break
            attempted += 1
            if builds(target, function.name, function.vram, scratch):
                bump("skipped-already-builds")
                continue

            fresh = scratch / f"{function.name}.c"
            asm_copy = scratch / f"{function.name}.s"
            asm_copy.write_text(
                batch_match.m2c_assembly_text(text, function), encoding="utf-8"
            )
            if not batch_match.run_m2c(
                m2c, asm_copy, function.name, fresh, display_asm_name=asm.name
            ):
                bump("m2c-failed")
                continue

            rebuilt = (
                export
                + "#else\n"
                + fresh.read_text().strip()
                + "\n#endif\n"
            )
            if EXPORT_WORD_RE.findall(rebuilt) != EXPORT_WORD_RE.findall(original):
                raise RetailError(
                    f"refusing to write {target}: export words would change"
                )
            probe = scratch / f"probe_{function.name}.c"
            probe.write_text(rebuilt)
            errors: list[str] = []
            if not builds(probe, function.name, function.vram, scratch, errors):
                bump("still-build-failed")
                if error_log is not None:
                    with error_log.open("a") as handle:
                        handle.write(f"### {region} {function.name}\n{errors[-1] if errors else ''}\n")
                continue
            if dry_run:
                bump("would-rewrite")
                continue
            # Written through a temporary in the same directory and renamed.
            # The test suite reads these sources concurrently to generate the
            # native word tables, and a torn read would fail a build for a
            # reason that has nothing to do with the change.
            staged = target.with_suffix(target.suffix + ".recarve")
            staged.write_text(rebuilt)
            staged.replace(target)
            bump("rewritten")
    return counts


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--region", action="append", choices=sorted(REGIONS),
                        help="repeatable; default is every region")
    parser.add_argument("--registry", type=Path, default=Path("provenance/matches.json"))
    parser.add_argument("--limit", type=int, help="examine at most this many drafts per region")
    parser.add_argument("--error-log", type=Path,
                        help="append the compiler diagnostic for every draft that still fails")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repo = Path(__file__).resolve().parent.parent
    registry = json.loads((repo / args.registry).read_text())
    for region in args.region or sorted(REGIONS):
        registered = {
            match["vram"] for match in registry["matches"] if match["region"] == region
        }
        counts = recarve_region(region, repo, registered, args.limit, args.dry_run,
                                args.error_log)
        print(f"{region}:")
        for key in sorted(counts):
            print(f"  {key:26s} {counts[key]}")
    print("\nEvery rewritten file is still an UNVERIFIED draft. Run")
    print("tools/batch_match.py --register-existing to let the oracle judge them.")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    raise SystemExit(main())
