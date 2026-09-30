#!/usr/bin/env python3
"""Re-verify every function in provenance/matches.json through the oracle.

The registry is a claim: each entry says its source rebuilds to retail bytes.
This rebuilds and re-compares all of them, so the claim is re-earned rather
than remembered. It exists because a batch once destroyed a committed match and
nothing else noticed; see docs/MATCHING.md.

Exit 0 only when every entry still matches.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
from pathlib import Path
from typing import Any

import build_candidate
import match_function
from retail_common import RetailError, load_json


def plan(entry: dict[str, Any], targets: dict[str, Any], candidate: Path) -> tuple[list[str], list[str]]:
    """Argument vectors for the build and the comparison of one entry."""

    region = entry["region"]
    target = targets.get(region)
    if target is None:
        raise RetailError(f"{entry['name']}: region {region!r} is not a registered target")

    build = [
        str(entry["source"]),
        "--symbol", str(entry["name"]),
        "--link-base", f"0x{int(entry['vram']):X}",
        "--output", str(candidate),
    ]
    # Most of the executable is identified as -O2; an entry only needs this
    # when it does not match at that default, which build_candidate.py itself
    # already assumes.
    optimization = entry.get("optimization")
    if optimization:
        # The "=" form is required: argparse reads a bare "-O0" as another
        # flag rather than this one's value, since it starts with "-".
        build += [f"--optimization={optimization}"]
    # Switch jump tables live in .rodata; the function's lui/lw of the table
    # only match when the table is placed at its retail address.
    rodata_base = entry.get("rodata_base")
    if rodata_base is not None:
        build += ["--rodata-base", f"0x{int(rodata_base):X}"]
    # Sony objects assembled with GNU as rather than ASPSX (see
    # build_candidate.ASSEMBLERS); absent means the default maspsx path.
    assembler = entry.get("assembler")
    if assembler is not None:
        if assembler not in build_candidate.ASSEMBLERS:
            raise RetailError(f"{entry['name']}: unknown assembler {assembler!r}")
        build += ["--assembler", str(assembler)]
    match = [
        "--vram", f"0x{int(entry['vram']):X}",
        "--size", f"0x{int(entry['size']):X}",
        "--candidate", str(candidate),
    ]
    kind = target.get("kind")
    if kind == "executable":
        pass
    elif kind == "blob":
        match += [
            "--retail-file", str(target["file"]),
            "--base", f"0x{int(target['base']):X}",
            "--sha256", str(target["sha256"]),
        ]
    else:
        raise RetailError(f"{entry['name']}: target kind {kind!r} is not understood")
    return build, match


def _quiet(fn, argv: list[str]) -> tuple[int, str]:
    """Run one tool's main() with its output captured instead of printed.

    argparse calls sys.exit() on a bad argument list rather than returning, so
    a malformed argv here would otherwise raise SystemExit and abort the
    whole sweep with no diagnostic — the captured output would still be sitting
    in the discarded StringIO, printed nowhere. One bad entry must fail as
    one entry, not take down every entry after it.
    """

    sink = io.StringIO()
    try:
        with contextlib.redirect_stdout(sink), contextlib.redirect_stderr(sink):
            code = fn(argv)
    except SystemExit as exc:
        code = exc.code if isinstance(exc.code, int) else 1
    return code, sink.getvalue()


def verify_one(entry: dict[str, Any], targets: dict[str, Any]) -> tuple[str, str]:
    """Rebuild and compare one entry; returns (verdict line, tool output)."""

    with tempfile.TemporaryDirectory(prefix="verify_registry.") as scratch:
        candidate = Path(scratch) / "candidate.bin"
        build_argv, match_argv = plan(entry, targets, candidate)
        code, out = _quiet(build_candidate.main, build_argv)
        if code != 0 or not candidate.is_file():
            return f"BUILD FAILED  {entry['name']}  ({entry['source']})", out
        code, out = _quiet(match_function.main, match_argv)
        if code == 0:
            return f"MATCH         {entry['name']}", out
        return f"MISMATCH      {entry['name']}  ({entry['source']})", out


def _verify_worker(job: tuple[dict[str, Any], dict[str, Any]]) -> tuple[str, str]:
    return verify_one(*job)


def verify_all(registry: Path, verbose: bool, jobs: int = 1) -> int:
    data = load_json(registry)
    targets = data.get("targets", {})
    entries = data.get("matches", [])
    if not entries:
        raise RetailError(f"{registry} lists no matches")

    work = [(entry, targets) for entry in entries]
    if jobs > 1:
        import multiprocessing
        with multiprocessing.Pool(jobs) as pool:
            results = pool.imap(_verify_worker, work, chunksize=8)
            results = list(results)
    else:
        results = map(_verify_worker, work)
    failures = 0
    for verdict, out in results:
        print(verdict, flush=True)
        if not verdict.startswith("MATCH"):
            failures += 1
            if verbose:
                print(out)
    print(f"\n{len(entries) - failures}/{len(entries)} registry entries re-verified")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, help="default: provenance/matches.json")
    parser.add_argument("--verbose", action="store_true", help="print tool output on failure")
    parser.add_argument("--jobs", type=int, default=1, help="entries to rebuild in parallel")
    args = parser.parse_args(argv)
    repo = Path(__file__).resolve().parents[1]
    registry = args.registry or repo / "provenance/matches.json"
    try:
        return 1 if verify_all(registry, args.verbose, args.jobs) else 0
    except RetailError as exc:
        print(f"verify_registry: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
