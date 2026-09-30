#!/usr/bin/env python3
"""Unpack the PAC chunks inside a .CD archive.

`SC01.CD` is a .CD archive whose members are PAC containers, so this sits on top
of `extract_cd.py` rather than replacing it. See docs/ASSETS.md for how the
layout was derived.

A PAC chunk is a 16-byte header followed by its payload:

    +0  4  magic "PAC\\0"
    +4  1  type
    +5  1  flag
    +6  2  observed zero
    +8  4  small value, usually zero
    +12 4  total chunk length including this header, unpadded
    +16 .. payload

The stride to the next chunk is that length rounded up to 0x800.

The walk is required to close on the member's exact declared end. That is the
check that proved the format: an off-by-one stride still produces chunks that
look plausible one at a time, and only fails to add up. Refusing a walk that
leaves a gap is what separates a parse from a guess.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import extract_cd
from retail_common import RetailError, write_json_atomic

SECTOR = 0x800
CHUNK_HEADER = 16
MAGIC = b"PAC\x00"


def _round_up(value: int) -> int:
    return (value + SECTOR - 1) & ~(SECTOR - 1)


@dataclass(frozen=True)
class PacChunk:
    index: int
    offset: int  # absolute, within the archive
    kind: int
    flag: int
    field8: int
    length: int  # declared, including the header
    stride: int  # length padded to a sector

    @property
    def payload_offset(self) -> int:
        return self.offset + CHUNK_HEADER

    @property
    def payload_size(self) -> int:
        return self.length - CHUNK_HEADER


def is_pac_member(data: bytes) -> bool:
    """True when a member begins with a PAC header.

    Eight members of SC01.CD begin with `.sqv` instead. They are not PAC
    containers and no length field leads through them, so they are classified
    out rather than forced through the walk.
    """

    return data[:4] == MAGIC


def parse_pac_chunks(data: bytes, member_offset: int) -> list[PacChunk]:
    """Walk one member's chunks, refusing anything that does not add up.

    `member_offset` is where this member starts in the archive; it is added to
    the reported offsets so a chunk can be located in the original file.
    """

    end = len(data)
    if end == 0:
        raise RetailError("member is empty; refusing to treat it as a PAC container")
    if not is_pac_member(data):
        raise RetailError(
            f"member at 0x{member_offset:X} does not begin with {MAGIC!r}; "
            "it is not a PAC container"
        )

    chunks: list[PacChunk] = []
    cursor = 0
    while cursor < end:
        if cursor + CHUNK_HEADER > end:
            raise RetailError(
                f"chunk {len(chunks)} header at 0x{member_offset + cursor:X} is truncated: "
                f"{end - cursor} bytes remain, need {CHUNK_HEADER}"
            )
        if data[cursor : cursor + 4] != MAGIC:
            raise RetailError(
                f"expected {MAGIC!r} for chunk {len(chunks)} at "
                f"0x{member_offset + cursor:X}, got {data[cursor:cursor + 4]!r}; "
                "the walk has lost alignment"
            )

        kind, flag = data[cursor + 4], data[cursor + 5]
        field8, length = struct.unpack_from("<2I", data, cursor + 8)

        # A length that does not cover its own header cannot advance the walk.
        if length < CHUNK_HEADER:
            raise RetailError(
                f"chunk {len(chunks)} at 0x{member_offset + cursor:X} declares length "
                f"{length}, which is smaller than its {CHUNK_HEADER}-byte header"
            )
        stride = _round_up(length)
        if cursor + stride > end:
            raise RetailError(
                f"chunk {len(chunks)} at 0x{member_offset + cursor:X} runs past the end "
                f"of its member: length 0x{length:X} pads to 0x{stride:X} but only "
                f"0x{end - cursor:X} bytes remain"
            )

        chunks.append(
            PacChunk(
                index=len(chunks),
                offset=member_offset + cursor,
                kind=kind,
                flag=flag,
                field8=field8,
                length=length,
                stride=stride,
            )
        )
        cursor += stride

    # Guaranteed by the overrun check above, but stated because it is the
    # property that validates the format rather than an incidental one.
    if cursor != end:
        raise RetailError(
            f"walk ended at 0x{member_offset + cursor:X} but the member ends at "
            f"0x{member_offset + end:X}; the layout was misread"
        )
    return chunks


def extract_archive(archive: Path, output_dir: Path) -> dict[str, Any]:
    """Write every PAC chunk out, recording members that are not PAC."""

    if not archive.is_file():
        raise RetailError(f"archive not found: {archive}")
    if output_dir.exists():
        raise RetailError(f"output directory already exists: {output_dir}; choose an absent path")

    data = archive.read_bytes()
    entries = extract_cd.parse_cd_header(data[:SECTOR], len(data))

    output_dir.mkdir(parents=True)
    members: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []
    total_chunks = 0
    covered = 0

    for entry in entries:
        body = data[entry.offset : entry.offset + entry.size]
        if len(body) != entry.size:
            raise RetailError(
                f"short read for member {entry.index}: wanted {entry.size} bytes, got {len(body)}"
            )
        if not is_pac_member(body):
            skipped.append(
                {"index": entry.index, "size_bytes": entry.size, "magic": body[:4].hex()}
            )
            continue

        chunks = parse_pac_chunks(body, entry.offset)
        records: list[dict[str, Any]] = []
        for chunk in chunks:
            start = chunk.offset - entry.offset + CHUNK_HEADER
            payload = body[start : start + chunk.payload_size]
            name = f"{entry.index:04d}_{chunk.index:03d}.bin"
            (output_dir / name).write_bytes(payload)
            records.append(
                {
                    "chunk": chunk.index,
                    "offset": chunk.offset,
                    "type": chunk.kind,
                    "flag": chunk.flag,
                    "field8": chunk.field8,
                    "length": chunk.length,
                    "payload_bytes": chunk.payload_size,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "path": name,
                }
            )
            covered += chunk.stride
        total_chunks += len(chunks)
        members.append({"index": entry.index, "size_bytes": entry.size, "chunks": records})

    if not members:
        raise RetailError(f"{archive.name} contains no PAC members; nothing was extracted")

    pac_bytes = sum(m["size_bytes"] for m in members)
    return {
        "schema": 1,
        "archive": archive.name,
        "member_count": len(entries),
        "pac_members": len(members),
        "chunk_count": total_chunks,
        "pac_region_bytes": pac_bytes,
        "covered_bytes": covered,
        "skipped_members": skipped,
        "members": members,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="a .CD archive, e.g. extracted/disc/files/SC01.CD")
    parser.add_argument("--output", type=Path, required=True, help="absent directory for the chunks")
    parser.add_argument("--manifest", type=Path, help="optional JSON record of what was extracted")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        result = extract_archive(args.archive, args.output)
        pac_bytes = result["pac_region_bytes"]
        share = 100.0 * result["covered_bytes"] / pac_bytes if pac_bytes else 0.0
        print(
            f"EXTRACTED {result['archive']}: {result['chunk_count']} chunks across "
            f"{result['pac_members']}/{result['member_count']} members -> {args.output}"
        )
        print(f"  coverage of the PAC region: {result['covered_bytes']}/{pac_bytes} = {share:.4f}%")
        for skip in result["skipped_members"]:
            print(f"  skipped member {skip['index']:>3}: magic {skip['magic']}, {skip['size_bytes']} bytes")
        if args.manifest:
            write_json_atomic(args.manifest, result)
            print(f"  manifest={args.manifest}")
        return 0
    except RetailError as exc:
        print(f"extract_pac: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
