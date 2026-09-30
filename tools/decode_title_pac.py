"""Decode SC01 member 0's code stream from its pinned retail PAC container.

The independent token interpretation follows source 80018730: a 1024-byte
ring starting at slot1, LSB-first flag bytes, and 16-bit little-endian matches.
It refuses unknown dictionary history, missing terminators and oversized output.
This is extraction evidence, not a substitute for guest execution.
"""
from pathlib import Path
import hashlib
import struct

SC01_SHA256 = '0f3574599202f5fe1564ba516d749772c2afc8984a2c240449b60269fe3c8bab'
BASE = 0x80128158


def decode(data: bytes, limit: int = 0xD0000) -> tuple[bytes, int]:
    ring = [None] * 1024
    cursor, offset = 1, 0
    output = bytearray()
    while offset < len(data):
        flags = data[offset]
        offset += 1
        for bit in range(8):
            if flags & (1 << bit):
                if offset == len(data):
                    raise ValueError('truncated literal')
                values = [data[offset]]
                offset += 1
                back, count = 0, 1
            else:
                if offset + 2 > len(data):
                    raise ValueError('truncated match')
                token = int.from_bytes(data[offset:offset + 2], 'little')
                offset += 2
                back, count = token & 1023, (token >> 10) + 2
                if not back:
                    return bytes(output), offset
                values = None
            if len(output) + count > limit:
                raise ValueError('output exceeds bounded RAM span')
            for _ in range(count):
                value = values[0] if values is not None else ring[back]
                if value is None:
                    raise ValueError('match depends on unknown dictionary history')
                output.append(value)
                ring[cursor] = value
                cursor = (cursor + 1) & 1023
                back = (back + 1) & 1023
    raise ValueError('missing terminator')


def title_code(archive: Path) -> bytes:
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != SC01_SHA256:
        raise ValueError('SC01 retail hash mismatch')
    start_sector, member_size = struct.unpack_from('<II', data, 8)
    if start_sector != 1 or member_size != 897024:
        raise ValueError('unexpected SC01 member 0 extent')
    magic, kind, field, length = struct.unpack_from('<4I', data, 0x800)
    if (magic, kind, field, length) != (0x00434150, 4, 0, 291252):
        raise ValueError('unexpected title PAC header')
    if any(data[0x810:0x1000]):
        raise ValueError('header sector padding is not zero')
    decoded, consumed = decode(data[0x1000:0x800 + length])
    return decoded
