#!/usr/bin/env python3
"""Unpack a .CD archive from the disc.

Most of the game's code and data does not live in SLUS_007.26. It streams from
.CD archives, so reaching it starts here.

Layout, confirmed against MAIN.CD: a 0x800 header holding a u32 entry count, a
u32 pad, then eight bytes per entry — a u32 start sector and a u32 size. Offsets
are sector-scaled and relative to the archive itself, and members are padded out
to sector boundaries.

Every refusal below matters more than it looks. A misparsed archive does not
error, it yields plausible-looking garbage, so the header is checked against the
real file size rather than trusted.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from retail_common import RetailError, write_json_atomic

SECTOR = 0x800
HEADER_PREFIX = 8  # u32 count + u32 pad
MAX_ENTRIES = (SECTOR - HEADER_PREFIX) // 8


@dataclass(frozen=True)
class CdEntry:
    index: int
    offset: int
    size: int


def parse_cd_header(data: bytes, archive_size: int) -> list[CdEntry]:
    """Read the archive's directory, validated against its real size."""

    if len(data) < SECTOR:
        raise RetailError(
            f"archive header is truncated: need {SECTOR} bytes, got {len(data)}"
        )

    count, _pad = struct.unpack_from("<2I", data, 0)
    if count == 0:
        raise RetailError("archive header declares no entries; refusing to treat it as valid")
    if count > MAX_ENTRIES:
        raise RetailError(
            f"archive header declares {count} entries but only {MAX_ENTRIES} fit in a "
            f"{SECTOR}-byte header; the header is probably not a .CD directory"
        )

    entries: list[CdEntry] = []
    for index in range(count):
        sector, size = struct.unpack_from("<2I", data, HEADER_PREFIX + index * 8)
        if sector == 0:
            raise RetailError(
                f"entry {index} starts at sector 0, which is the header itself"
            )
        offset = sector * SECTOR
        if offset + size > archive_size:
            raise RetailError(
                f"entry {index} runs past the end of the archive: "
                f"0x{offset:X}+{size} exceeds {archive_size} bytes"
            )
        entries.append(CdEntry(index=index, offset=offset, size=size))
    return entries


def extract_archive(archive: Path, output_dir: Path) -> list[dict[str, Any]]:
    """Write every member out, returning a record of what was produced.

    Members are named by index. The archive carries no filenames, so inventing
    them would be fabrication; the index is what the game itself uses to address
    them.
    """

    if not archive.is_file():
        raise RetailError(f"archive not found: {archive}")
    if output_dir.exists():
        raise RetailError(f"output directory already exists: {output_dir}; choose an absent path")

    archive_size = archive.stat().st_size
    with archive.open("rb") as stream:
        entries = parse_cd_header(stream.read(SECTOR), archive_size)

        output_dir.mkdir(parents=True)
        written: list[dict[str, Any]] = []
        for entry in entries:
            stream.seek(entry.offset)
            payload = stream.read(entry.size)
            if len(payload) != entry.size:
                raise RetailError(
                    f"short read for entry {entry.index}: wanted {entry.size} bytes, "
                    f"got {len(payload)}"
                )
            member = output_dir / f"{entry.index:04d}.bin"
            member.write_bytes(payload)
            written.append(
                {
                    "index": entry.index,
                    "offset": entry.offset,
                    "size_bytes": entry.size,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "path": member.name,
                }
            )
    return written


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="a .CD archive, e.g. extracted/disc/files/MAIN.CD")
    parser.add_argument("--output", type=Path, required=True, help="absent directory to write members into")
    parser.add_argument("--manifest", type=Path, help="optional JSON record of what was extracted")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        written = extract_archive(args.archive, args.output)
        total = sum(record["size_bytes"] for record in written)
        print(f"EXTRACTED {args.archive.name}: {len(written)} members, {total} bytes -> {args.output}")
        for record in written:
            print(
                f"  [{record['index']:>3}] offset=0x{record['offset']:<8X} "
                f"size={record['size_bytes']:<9} {record['sha256'][:16]}..."
            )
        if args.manifest:
            write_json_atomic(
                args.manifest,
                {
                    "schema": 1,
                    "archive": args.archive.name,
                    "member_count": len(written),
                    "members": written,
                },
            )
            print(f"  manifest={args.manifest}")
        return 0
    except RetailError as exc:
        print(f"extract_cd: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
