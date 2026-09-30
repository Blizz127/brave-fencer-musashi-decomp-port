#!/usr/bin/env python3
"""Find TMD models inside a PAC chunk.

Chunks carry no directory of what they hold, and the model rendered in P2 was
located by scanning for its own signature. This makes that repeatable: every
word-aligned offset whose leading word is the TMD id is tried, and only those
that `tmd.py` parses cleanly and `extract_tmd.measure` closes on count.

The id word is 0x41, which is far too common to trust on its own. The whole
structure has to hold together — object table inside the data, vertex block
ending exactly at the normal block, every primitive index in range — before an
offset is reported. That is the same bar `tools/tmd.py` sets for rendering.

Scanning resumes after a found model rather than inside it, so a model's own
body cannot produce nested false positives.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import extract_tmd
import tmd
from retail_common import RetailError

WORD = 4


@dataclass(frozen=True)
class TmdHit:
    offset: int
    length: int
    objects: int
    primitives: int


def _try_parse(data: bytes, offset: int) -> TmdHit | None:
    window = data[offset:]
    try:
        model = tmd.parse(window)
        length = extract_tmd.measure(window)
    except RetailError:
        return None
    except (struct.error, IndexError, ValueError):
        return None
    return TmdHit(
        offset=offset,
        length=length,
        objects=len(model.objects),
        primitives=sum(len(o.primitives) for o in model.objects),
    )


def scan(data: bytes) -> list[TmdHit]:
    """Every parseable TMD in `data`, in order, non-overlapping."""

    hits: list[TmdHit] = []
    offset = 0
    id_word = struct.pack("<I", tmd.TMD_ID)
    while offset + tmd.OBJECT_TABLE_OFFSET <= len(data):
        if data[offset : offset + WORD] != id_word:
            offset += WORD
            continue
        hit = _try_parse(data, offset)
        if hit is None:
            offset += WORD
            continue
        hits.append(hit)
        # Resume on the next word boundary past the model.
        offset += (hit.length + WORD - 1) // WORD * WORD
    return hits


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("chunks", type=Path, nargs="+", help="extracted chunk files to scan")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    total = 0
    for chunk in args.chunks:
        if not chunk.is_file():
            print(f"find_tmd: ERROR: not a file: {chunk}", file=sys.stderr)
            return 2
        hits = scan(chunk.read_bytes())
        total += len(hits)
        for hit in hits:
            print(
                f"{chunk} offset=0x{hit.offset:X} length={hit.length} "
                f"objects={hit.objects} primitives={hit.primitives}"
            )
    print(f"FOUND {total} TMD models in {len(args.chunks)} chunks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
