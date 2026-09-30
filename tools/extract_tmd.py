#!/usr/bin/env python3
"""Cut a TMD model out of a disc archive and pin it by hash.

Retail data is addressed by digest everywhere else in this project, and models
are no different: the renderer reads a standalone pinned file rather than
reaching into SC01.CD at a magic offset.

A TMD carries no total length, so the size is measured by walking the model's
own structure rather than guessing at a bound. Anything that does not walk
cleanly is refused, because a range that merely starts with the right id would
otherwise be copied out as a plausible-looking model.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path
from typing import Any

import tmd
from retail_common import RetailError, write_json_atomic


def measure(data: bytes) -> int:
    """Length of the TMD starting at the front of `data`.

    Parsing validates the structure; the end is the furthest point any object's
    blocks reach, which for these models is the end of the normal block.
    """

    model = tmd.parse(data)
    end = 0
    _ident, _flags, n_object = struct.unpack_from("<3I", data, 0)
    for index in range(n_object):
        entry = tmd.OBJECT_TABLE_OFFSET + index * tmd.OBJECT_ENTRY_SIZE
        _vt, _nv, normal_top, n_normal, _pt, _np, _scale = struct.unpack_from("<7I", data, entry)
        end = max(end, tmd.OBJECT_TABLE_OFFSET + normal_top + n_normal * tmd.VECTOR_SIZE)
    if end > len(data):
        raise RetailError(f"model claims to end at 0x{end:X} but only {len(data)} bytes are present")
    return end


def extract(archive: Path, offset: int, output: Path) -> dict[str, Any]:
    """Write the model at `offset` to `output`, returning its record."""

    if not archive.is_file():
        raise RetailError(f"archive not found: {archive}")
    if output.exists():
        raise RetailError(f"output already exists: {output}; choose an absent path")
    size = archive.stat().st_size
    if offset < 0 or offset >= size:
        raise RetailError(f"offset 0x{offset:X} is outside the {size}-byte archive")

    with archive.open("rb") as stream:
        stream.seek(offset)
        window = stream.read(size - offset)

    length = measure(window)
    payload = window[:length]

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)

    model = tmd.parse(payload)
    obj = model.objects[0]
    return {
        "archive": archive.name,
        "offset": offset,
        "size_bytes": length,
        "sha256": hashlib.sha256(payload).hexdigest(),
        "objects": len(model.objects),
        "vertices": len(obj.vertices),
        "normals": len(obj.normals),
        "primitives": len(obj.primitives),
        "path": str(output),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="e.g. extracted/disc/files/SC01.CD")
    parser.add_argument("--offset", required=True, help="model offset, e.g. 0xA97000")
    parser.add_argument("--output", type=Path, required=True, help="absent path to write the model to")
    parser.add_argument("--manifest", type=Path, help="optional JSON record")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        try:
            offset = int(str(args.offset), 0)
        except ValueError as exc:
            raise RetailError(f"could not parse --offset: {exc}") from exc

        record = extract(args.archive, offset, args.output)
        print(f"EXTRACTED {record['archive']} @0x{record['offset']:X} -> {record['path']}")
        print(f"  {record['size_bytes']} bytes  sha256={record['sha256']}")
        print(
            f"  objects={record['objects']} vertices={record['vertices']} "
            f"normals={record['normals']} primitives={record['primitives']}"
        )
        if args.manifest:
            write_json_atomic(args.manifest, {"schema": 1, "model": record})
            print(f"  manifest={args.manifest}")
        return 0
    except RetailError as exc:
        print(f"extract_tmd: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
