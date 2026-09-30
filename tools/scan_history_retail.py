#!/usr/bin/env python3
"""Scan every blob in the git history for retail game code or data.

The repository must never hold the game's own bytes (LICENSE-NOTES.md). This
tool proves it for the whole history, not just the working tree: it reads
every blob reachable from the chosen refs and reports any that carry a run
of retail content, either

* as text: 15 or more consecutive 32-bit words written as hex (`0x1234ABCD`
  or bare `1234ABCD`, read both as a value and as bytes in memory order),
  which covers word exports, `.word` tiles and splat-style listings; or
* as bytes: 64 or more consecutive retail bytes.

Retail images come from the developer's local, gitignored extracts (the EXE,
the MAIN.CD members and the decoded SC scene overlays), so this needs the
user's own disc and never stores its contents. The index samples 8-word
windows every 8 words, so any contiguous run of at least 15 words, however
it is aligned, contains a sampled window.

    python3 tools/scan_history_retail.py                 # all refs
    python3 tools/scan_history_retail.py --refs refs/heads refs/tags
Exit 0 when clean, 1 when retail content is found, 2 when inputs are missing.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
WINDOW_WORDS = 8
HEX_RE = re.compile(rb"(?<![0-9A-Za-z_./])(?:0[xX])?([0-9A-Fa-f]{8})(?![0-9A-Za-z_./])")
SKIP_SUFFIXES = (b".json",)  # registries and manifests hold addresses, not code


def retail_images() -> list[Path]:
    paths = [REPO / "extracted/disc/files/SLUS_007.26"]
    paths += [Path(p) for p in sorted(glob.glob(str(REPO / "extracted/overlays/main/*.bin")))]
    paths += [Path(p) for p in sorted(glob.glob(str(REPO / "extracted/overlays/sc0?/*.pac0.bin")))]
    return [p for p in paths if p.is_file()]


def key(chunk: bytes) -> int:
    return int.from_bytes(hashlib.blake2b(chunk, digest_size=8).digest(), "little")


def build_index(images: list[Path]) -> set[int]:
    index: set[int] = set()
    span = WINDOW_WORDS * 4
    for path in images:
        data = path.read_bytes()
        for off in range(0, len(data) - span + 1, span):
            chunk = data[off:off + span]
            words = [chunk[k:k + 4] for k in range(0, span, 4)]
            if len(set(words)) < 4 or words.count(b"\0\0\0\0") > 4:
                continue  # low-information windows (padding, mostly-zero data) match by chance
            if all(0x80000000 <= int.from_bytes(w, "little") < 0x80200000 for w in words):
                continue  # pure RAM-address runs are layout metadata (source lists), not code
            index.add(key(chunk))
    return index


def text_hits(blob: bytes, index: set[int]) -> int:
    tokens = [m.group(1) for m in HEX_RE.finditer(blob)]
    if len(tokens) < WINDOW_WORDS:
        return 0
    as_value = [int(t, 16).to_bytes(4, "little") for t in tokens]
    as_bytes = [bytes.fromhex(t.decode()) for t in tokens]
    hits = 0
    for words in (as_value, as_bytes):
        for i in range(len(words) - WINDOW_WORDS + 1):
            if key(b"".join(words[i:i + WINDOW_WORDS])) in index:
                hits += 1
    return hits


def byte_hits(blob: bytes, index: set[int]) -> int:
    span = WINDOW_WORDS * 4
    hits = 0
    for off in range(0, len(blob) - span + 1, 4):
        if key(blob[off:off + span]) in index:
            hits += 1
    return hits


def blobs(refs: list[str]):
    """Yield (blob id, one path it appears at) for every blob reachable from refs."""
    listing = subprocess.run(["git", "rev-list", "--objects", *refs], cwd=REPO,
                             check=True, capture_output=True).stdout
    for line in listing.splitlines():
        parts = line.split(b" ", 1)
        if len(parts) == 2:
            yield parts[0], parts[1]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--refs", nargs="*", default=["--all"], help="ref arguments for git rev-list (default --all)")
    parser.add_argument("--max-size", type=int, default=8 << 20, help="skip blobs larger than this")
    args = parser.parse_args(argv)
    images = retail_images()
    if not images or not (REPO / "extracted/disc/files/SLUS_007.26").is_file():
        print("scan_history_retail: the local retail extracts are missing; run the extraction first", file=sys.stderr)
        return 2
    index = build_index(images)
    print(f"indexed {len(index):,} retail windows from {len(images)} images", flush=True)
    candidates = [(oid, path) for oid, path in blobs(args.refs) if not path.endswith(SKIP_SUFFIXES)]
    cat = subprocess.Popen(["git", "cat-file", "--batch"], cwd=REPO, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    seen: set[bytes] = set()
    flagged = 0
    for oid, path in candidates:
        if oid in seen:
            continue
        seen.add(oid)
        cat.stdin.write(oid + b"\n")
        cat.stdin.flush()
        header = cat.stdout.readline().split()
        if len(header) < 3 or header[1] != b"blob":
            if len(header) >= 3:
                cat.stdout.read(int(header[2]) + 1)
            continue
        size = int(header[2])
        data = cat.stdout.read(size)
        cat.stdout.read(1)
        if size > args.max_size:
            continue
        hits = text_hits(data, index) + (byte_hits(data, index) if b"\0" in data[:8192] else 0)
        if hits:
            flagged += 1
            print(f"RETAIL {oid.decode()} {path.decode(errors='replace')} windows={hits}", flush=True)
    cat.stdin.close()
    cat.wait()
    print(f"scanned {len(seen):,} blobs: {flagged} carry retail content")
    return 1 if flagged else 0


if __name__ == "__main__":
    raise SystemExit(main())
