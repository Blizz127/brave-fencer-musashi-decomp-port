#!/usr/bin/env python3
"""Refuse to ship retail content.

Checks every file under a staging directory:
  names   no disc images, boot executables or memory-card saves
          (*.cue *.bin *.iso *.img *.chd *.ccd *.mds *.mdf *.mcd *.mcr *.srm,
          SLUS_*/SCUS_*/SLPS_*/SLES_* style names, SYSTEM.CNF)
  hashes  no file identical (SHA-256) to the user's EXE or to any file
          extracted from their disc (--exe, --extracted)
  runs    no file containing a run of >= --run-bytes (default 64) consecutive
          bytes of the EXE (or of --run-source files). Low-entropy stretches
          (padding, zero fill) are ignored, so common byte patterns don't trip it.

The references stay on the user's machine; only hashes and windows are held
in memory. Exit status 0 = clean, 1 = retail content found, 2 = usage.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
from pathlib import Path

BAD_EXT = {".cue", ".bin", ".iso", ".img", ".chd", ".ccd", ".mds", ".mdf",
           ".mcd", ".mcr", ".srm", ".gme", ".psv"}
BAD_NAME = re.compile(r"^(S[CL][UEP][SMS]|SLKA|SCPS|SLPM|PAPX)[_-]?\d{3}\.?\d{2}(;1)?$", re.I)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def low_entropy(window: bytes) -> bool:
    return len(set(window)) < 8


class RunIndex:
    """Windows of `w` bytes taken every `w` bytes of each source; any common
    run of >= 2w-1 bytes must contain one of them."""

    def __init__(self, run: int):
        self.w = max(8, run // 2)
        self.run = run
        self.windows: dict[bytes, tuple[int, int]] = {}
        self.sources: list[tuple[str, bytes]] = []

    def add(self, name: str, data: bytes) -> None:
        sid = len(self.sources)
        self.sources.append((name, data))
        for off in range(0, len(data) - self.w + 1, self.w):
            win = data[off:off + self.w]
            if not low_entropy(win):
                self.windows.setdefault(win, (sid, off))

    def scan(self, data: bytes):
        """Yields (staged_offset, source_name, source_offset, run_length)."""
        w, seen_until = self.w, -1
        get = self.windows.get
        for pos in range(0, len(data) - w + 1):
            if pos < seen_until:
                continue
            hit = get(data[pos:pos + w])
            if hit is None:
                continue
            sid, off = hit
            src = self.sources[sid][1]
            a, b = pos, off                   # extend backwards
            while a > 0 and b > 0 and data[a - 1] == src[b - 1]:
                a -= 1
                b -= 1
            e, f = pos + w, off + w           # and forwards
            while e < len(data) and f < len(src) and data[e] == src[f]:
                e += 1
                f += 1
            if e - a >= self.run and not low_entropy(data[a:e]):
                yield a, self.sources[sid][0], b, e - a
                seen_until = e


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stage", required=True, type=Path)
    ap.add_argument("--exe", type=Path, action="append", default=[],
                    help="the user's boot EXE (SLUS_007.26); repeatable")
    ap.add_argument("--extracted", type=Path, action="append", default=[],
                    help="directory of files extracted from the user's disc; repeatable")
    ap.add_argument("--run-source", type=Path, action="append", default=[],
                    help="extra files to run-check against (besides --exe)")
    ap.add_argument("--run-bytes", type=int, default=64)
    ap.add_argument("--allow-no-reference", action="store_true",
                    help="only run the name check when no retail reference is given")
    args = ap.parse_args(argv)

    if not args.stage.is_dir():
        print(f"retail_guard: {args.stage} is not a directory", file=sys.stderr)
        return 2
    if not (args.exe or args.extracted) and not args.allow_no_reference:
        print("retail_guard: give --exe and/or --extracted (the user's own disc files), "
              "or --allow-no-reference for a name-only check", file=sys.stderr)
        return 2

    ref_hashes: dict[str, str] = {}
    for exe in args.exe:
        ref_hashes[sha256(exe)] = str(exe)
    for d in args.extracted:
        for p in sorted(d.rglob("*")):
            if p.is_file() and p.stat().st_size > 0:
                ref_hashes.setdefault(sha256(p), str(p))
    index = RunIndex(args.run_bytes)
    for src in list(args.exe) + list(args.run_source):
        index.add(str(src), src.read_bytes())

    problems = []
    staged = sorted(p for p in args.stage.rglob("*") if p.is_file())
    for p in staged:
        rel = p.relative_to(args.stage)
        if p.suffix.lower() in BAD_EXT or BAD_NAME.match(p.name) or p.name.upper() == "SYSTEM.CNF":
            problems.append(f"{rel}: disc image / retail file name")
        if ref_hashes:
            h = sha256(p)
            if h in ref_hashes:
                problems.append(f"{rel}: identical to retail file {ref_hashes[h]}")
                continue
        if index.windows:
            for off, src, soff, length in index.scan(p.read_bytes()):
                problems.append(f"{rel}: {length} bytes at 0x{off:x} match {src} at 0x{soff:x}")
                break
    for line in problems:
        print("RETAIL CONTENT: " + line)
    print(f"retail_guard: {len(staged)} files checked, {len(problems)} problem(s)"
          + ("" if (args.exe or args.extracted) else " (name check only)"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
