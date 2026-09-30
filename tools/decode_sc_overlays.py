#!/usr/bin/env python3
"""Decode every SC scene overlay's LZSS code chunk into the local extract cache.

Each extracted SC member (extracted/overlays/scNN/MMMM.bin, produced from the
user's own disc) is a chain of PAC chunks. The type-4 chunk holds the
overlay's code, LZSS-compressed with the game's own scheme (see
tools/decode_title_pac.py and source 80018730). Its payload starts 0x800 past
the chunk header. The decoded image, which loads at 0x80128158, is written
next to the member as MMMM.pac0.bin, and extracted/overlays/sc_pac0.json
records each image's length and SHA-256. Everything written here is
retail-derived and stays under the gitignored extracted/ tree.

Decoding refuses a read of unknown ring history, a missing terminator, and a
stream that runs past its chunk's declared length.
"""

from __future__ import annotations

import glob
import hashlib
import json
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CODE_CHUNK_TYPE = 4


def decode(src: bytes, start: int, end: int) -> tuple[bytes, int]:
    ring: list[int | None] = [None] * 0x400
    cursor = 1
    out = bytearray()
    i = start
    while i < end:
        flags = src[i]
        i += 1
        for bit in range(8):
            if flags & (1 << bit):
                value = src[i]
                i += 1
                out.append(value)
                ring[cursor] = value
                cursor = (cursor + 1) & 0x3FF
                continue
            token = src[i] | (src[i + 1] << 8)
            i += 2
            read = token & 0x3FF
            if read == 0:
                return bytes(out), i
            for _ in range((token >> 10) + 2):
                value = ring[read]
                if value is None:
                    raise ValueError("unknown dictionary history")
                out.append(value)
                ring[cursor] = value
                cursor = (cursor + 1) & 0x3FF
                read = (read + 1) & 0x3FF
    raise ValueError("missing terminator")


def decode_member(path: Path) -> dict:
    data = path.read_bytes()
    offset = 0
    while offset + 16 <= len(data) and data[offset:offset + 4] == b"PAC\0":
        header = struct.unpack_from("<8I", data, offset)
        if header[1] == CODE_CHUNK_TYPE:
            image, stop = decode(data, offset + 0x800, offset + header[3])
            if stop > offset + header[3]:
                raise ValueError("stream ran past its chunk")
            target = path.with_suffix(".pac0.bin")
            target.write_bytes(image)
            return {"image": str(target.relative_to(REPO)), "src": str(path.relative_to(REPO)),
                    "chunk": offset, "length": len(image),
                    "sha256": hashlib.sha256(image).hexdigest()}
        step = (header[3] + 0x7FF) & ~0x7FF
        if not step:
            break
        offset += step
    raise ValueError("no code chunk")


def main() -> int:
    manifest: dict[str, dict] = {}
    failures = 0
    for name in sorted(glob.glob(str(REPO / "extracted/overlays/sc0?/[0-9][0-9][0-9][0-9].bin"))):
        path = Path(name)
        key = str(path.relative_to(REPO))
        # Keyed by decoded image; a member without one is keyed by its source.
        try:
            record = decode_member(path)
            manifest[record.pop("image")] = record
        except ValueError as exc:
            manifest[key] = {"error": str(exc)}
            failures += 1
    (REPO / "extracted/overlays/sc_pac0.json").write_text(json.dumps(manifest, indent=1))
    print(f"{len(manifest) - failures} decoded, {failures} without a decodable code chunk")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
