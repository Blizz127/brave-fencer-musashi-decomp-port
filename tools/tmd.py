#!/usr/bin/env python3
"""Read a TMD model, the PlayStation's 3D format.

Every rule below was derived from the models on this disc and checked against
two of them, because the format notes in circulation did not match the bytes.

**Offsets in the object table are relative to the table's own start**, which is
`base + 12`. Reading them as file-relative puts the primitive block inside the
header; reading them as header-end-relative overshoots `vert_top` by exactly 12
bytes. That identical overshoot on two models of very different sizes is what
gave the rule away.

The layout is then self-checking, and all four checks are enforced here:
`prim_top` lands at the end of the object table, walking `n_primitive` packets
ends exactly at `vert_top`, the vertex block ends exactly at `normal_top`, and
every packet is a polygon.

Only untextured light-source-shaded polygons are read. A packet's payload shape
depends on its flag: with `flag & 0x04` each vertex carries its own colour and
occupies two words instead of one, and a textured packet carries UVs. Those are
refused rather than misread, because a misparsed model does not error, it
produces plausible-looking garbage geometry.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from retail_common import RetailError

TMD_ID = 0x41
OBJECT_TABLE_OFFSET = 12          # offsets resolve against here, not the file base
OBJECT_ENTRY_SIZE = 28
VECTOR_SIZE = 8                   # x, y, z, pad — each a signed 16-bit value
WORD = 4

MODE_POLYGON = 0x20
MODE_GOURAUD = 0x10
MODE_QUAD = 0x08
MODE_TEXTURED = 0x04
MODE_SEMITRANS = 0x02

FLAG_NO_LIGHT = 0x04              # per-vertex colour; two words per vertex

MAX_OBJECTS = 4096
MAX_PRIMITIVES = 65536


@dataclass(frozen=True)
class TmdPrimitive:
    mode: int
    flag: int
    rgb: tuple[int, int, int]
    normal_indices: list[int]
    vertex_indices: list[int]

    @property
    def is_quad(self) -> bool:
        return bool(self.mode & MODE_QUAD)

    @property
    def vertex_count(self) -> int:
        return 4 if self.is_quad else 3


@dataclass(frozen=True)
class TmdObject:
    vertices: list[tuple[int, int, int]]
    normals: list[tuple[int, int, int]]
    primitives: list[TmdPrimitive]


@dataclass(frozen=True)
class TmdModel:
    flags: int
    objects: list[TmdObject]


def _u32(data: bytes, offset: int) -> int:
    if offset + WORD > len(data):
        raise RetailError(f"read past end of model at offset 0x{offset:X}")
    return struct.unpack_from("<I", data, offset)[0]


def _vectors(data: bytes, start: int, count: int, what: str) -> list[tuple[int, int, int]]:
    end = start + count * VECTOR_SIZE
    if end > len(data):
        raise RetailError(f"{what} block 0x{start:X}+{count * VECTOR_SIZE} runs past the model end")
    out = []
    for index in range(count):
        x, y, z = struct.unpack_from("<3h", data, start + index * VECTOR_SIZE)
        out.append((x, y, z))
    return out


def _parse_primitives(data: bytes, start: int, count: int, end: int,
                      n_vert: int, n_normal: int) -> list[TmdPrimitive]:
    """Walk `count` packets from `start`, which must land exactly on `end`."""

    primitives: list[TmdPrimitive] = []
    offset = start
    for index in range(count):
        if offset + 2 * WORD > len(data):
            raise RetailError(f"primitive {index} header runs past the model end")
        _olen, ilen, flag, mode = data[offset], data[offset + 1], data[offset + 2], data[offset + 3]

        if (mode >> 5) != 1:
            raise RetailError(f"primitive {index} mode 0x{mode:02X} is not a polygon")
        if mode & MODE_TEXTURED:
            raise RetailError(
                f"primitive {index} is textured (mode 0x{mode:02X}); textured packets carry UVs "
                "and a different payload, which is not read here"
            )
        if flag & FLAG_NO_LIGHT:
            raise RetailError(
                f"primitive {index} has flag 0x{flag:02X}: per-vertex colour uses two words per "
                "vertex, which is not read here"
            )

        vertex_count = 4 if (mode & MODE_QUAD) else 3
        expected_ilen = 1 + vertex_count
        if ilen != expected_ilen:
            raise RetailError(
                f"primitive {index} declares ilen {ilen}, expected {expected_ilen} for a "
                f"{vertex_count}-vertex untextured packet"
            )

        red, green, blue = data[offset + 4], data[offset + 5], data[offset + 6]
        normals: list[int] = []
        vertices: list[int] = []
        for slot in range(vertex_count):
            normal_index, vertex_index = struct.unpack_from("<2H", data, offset + 8 + slot * WORD)
            if vertex_index >= n_vert:
                raise RetailError(
                    f"primitive {index} references vertex {vertex_index} of {n_vert}"
                )
            if normal_index >= n_normal:
                raise RetailError(
                    f"primitive {index} references normal {normal_index} of {n_normal}"
                )
            normals.append(normal_index)
            vertices.append(vertex_index)

        primitives.append(
            TmdPrimitive(mode=mode, flag=flag, rgb=(red, green, blue),
                         normal_indices=normals, vertex_indices=vertices)
        )
        offset += WORD + ilen * WORD

    if offset != end:
        raise RetailError(
            f"primitive walk ended at 0x{offset:X} but the vertex block starts at 0x{end:X}; "
            "the packet layout does not match the header"
        )
    return primitives


def parse(data: bytes) -> TmdModel:
    """Parse a whole TMD, refusing anything whose structure does not close."""

    if len(data) < OBJECT_TABLE_OFFSET:
        raise RetailError("model is shorter than a TMD header")
    ident, flags, n_object = struct.unpack_from("<3I", data, 0)
    if ident != TMD_ID:
        raise RetailError(f"not a TMD: id is 0x{ident:X}, expected 0x{TMD_ID:X}")
    if n_object == 0:
        raise RetailError("model declares no objects")
    if n_object > MAX_OBJECTS:
        raise RetailError(f"model declares {n_object} objects, which is implausible")

    table_end = OBJECT_TABLE_OFFSET + n_object * OBJECT_ENTRY_SIZE
    if table_end > len(data):
        raise RetailError(
            f"object table needs {table_end} bytes but the model is {len(data)}"
        )

    objects: list[TmdObject] = []
    for index in range(n_object):
        entry = OBJECT_TABLE_OFFSET + index * OBJECT_ENTRY_SIZE
        vert_top, n_vert, normal_top, n_normal, prim_top, n_prim, _scale = struct.unpack_from(
            "<7I", data, entry
        )
        if n_prim > MAX_PRIMITIVES:
            raise RetailError(f"object {index} declares {n_prim} primitives, which is implausible")

        # Offsets resolve against the object table, not the file base.
        vert_start = OBJECT_TABLE_OFFSET + vert_top
        normal_start = OBJECT_TABLE_OFFSET + normal_top
        prim_start = OBJECT_TABLE_OFFSET + prim_top

        if vert_start + n_vert * VECTOR_SIZE != normal_start:
            raise RetailError(
                f"object {index}: {n_vert} vertices from 0x{vert_start:X} do not end at the "
                f"normal block 0x{normal_start:X}"
            )

        primitives = _parse_primitives(
            data, prim_start, n_prim, vert_start, n_vert, n_normal
        )
        objects.append(
            TmdObject(
                vertices=_vectors(data, vert_start, n_vert, "vertex"),
                normals=_vectors(data, normal_start, n_normal, "normal"),
                primitives=primitives,
            )
        )
    return TmdModel(flags=flags, objects=objects)


def parse_file(path: Path) -> TmdModel:
    if not path.is_file():
        raise RetailError(f"model not found: {path}")
    return parse(path.read_bytes())
