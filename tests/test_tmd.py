from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import tmd  # noqa: E402
from retail_common import RetailError  # noqa: E402


def build(primitives, n_vert=4, n_normal=4, ident=tmd.TMD_ID, flags=0, nobj=1):
    """Assemble a synthetic TMD whose offsets follow the real convention.

    Offsets in the object table are relative to the table's own start, i.e.
    base + 12, which is what the real models use.
    """
    prim_blob = b"".join(primitives)
    table_start = 12
    prim_top = (12 + nobj * 28) - table_start
    vert_top = prim_top + len(prim_blob)
    normal_top = vert_top + n_vert * 8
    body_end = normal_top + n_normal * 8

    header = struct.pack("<3I", ident, flags, nobj)
    entry = struct.pack("<7I", vert_top, n_vert, normal_top, n_normal, prim_top, len(primitives), 0)
    body = prim_blob + bytes(n_vert * 8) + bytes(n_normal * 8)
    return header + entry + body


def packet(mode, flag, rgb, pairs):
    """One primitive packet: header word, colour word, then a word per vertex."""
    ilen = 1 + len(pairs)
    out = bytes([0, ilen, flag, mode]) + bytes([rgb[0], rgb[1], rgb[2], mode])
    for normal_index, vertex_index in pairs:
        out += struct.pack("<2H", normal_index, vertex_index)
    return out


GOURAUD_TRI = tmd.MODE_POLYGON | tmd.MODE_GOURAUD                    # 0x30
GOURAUD_QUAD = tmd.MODE_POLYGON | tmd.MODE_GOURAUD | tmd.MODE_QUAD   # 0x38


class HeaderTests(unittest.TestCase):
    def test_parses_a_minimal_model(self) -> None:
        data = build([packet(GOURAUD_TRI, 0, (1, 2, 3), [(0, 0), (1, 1), (2, 2)])])
        model = tmd.parse(data)
        self.assertEqual(len(model.objects), 1)
        self.assertEqual(len(model.objects[0].primitives), 1)

    def test_wrong_id_is_refused(self) -> None:
        data = build([packet(GOURAUD_TRI, 0, (1, 2, 3), [(0, 0), (1, 1), (2, 2)])], ident=0x99)
        with self.assertRaises(RetailError):
            tmd.parse(data)

    def test_object_table_past_end_of_file_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            tmd.parse(struct.pack("<3I", tmd.TMD_ID, 0, 4))

    def test_zero_objects_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            tmd.parse(struct.pack("<3I", tmd.TMD_ID, 0, 0))


class OffsetTests(unittest.TestCase):
    """Offsets resolve against the object table, not the file base.

    Reading them as base-relative puts the primitive block inside the header;
    reading them as header-end-relative overshoots by exactly 12 bytes. Both
    were tried against the real models before this rule was settled.
    """

    def test_offsets_are_relative_to_the_object_table(self) -> None:
        self.assertEqual(tmd.OBJECT_TABLE_OFFSET, 12)

    def test_primitive_walk_not_ending_at_vert_top_is_refused(self) -> None:
        data = bytearray(build([packet(GOURAUD_TRI, 0, (1, 2, 3), [(0, 0), (1, 1), (2, 2)])]))
        # Claim one more primitive than the block holds.
        struct.pack_into("<I", data, 12 + 5 * 4, 2)
        with self.assertRaises(RetailError):
            tmd.parse(bytes(data))

    def test_vertex_block_not_ending_at_normal_top_is_refused(self) -> None:
        data = bytearray(build([packet(GOURAUD_TRI, 0, (1, 2, 3), [(0, 0), (1, 1), (2, 2)])]))
        struct.pack_into("<I", data, 12 + 1 * 4, 99)   # n_vert disagrees with normal_top
        with self.assertRaises(RetailError):
            tmd.parse(bytes(data))


class PrimitiveTests(unittest.TestCase):
    def test_reads_colour_and_index_pairs(self) -> None:
        data = build([packet(GOURAUD_QUAD, 0, (240, 197, 152), [(0, 1), (1, 2), (2, 0), (3, 3)])])
        prim = tmd.parse(data).objects[0].primitives[0]
        self.assertEqual(prim.rgb, (240, 197, 152))
        self.assertEqual(prim.vertex_indices, [1, 2, 0, 3])
        self.assertEqual(prim.normal_indices, [0, 1, 2, 3])

    def test_quad_and_triangle_are_distinguished(self) -> None:
        data = build([
            packet(GOURAUD_TRI, 0, (1, 1, 1), [(0, 0), (1, 1), (2, 2)]),
            packet(GOURAUD_QUAD, 0, (2, 2, 2), [(0, 0), (1, 1), (2, 2), (3, 3)]),
        ])
        prims = tmd.parse(data).objects[0].primitives
        self.assertEqual([p.vertex_count for p in prims], [3, 4])
        self.assertFalse(prims[0].is_quad)
        self.assertTrue(prims[1].is_quad)

    def test_non_polygon_mode_is_refused(self) -> None:
        data = build([packet(0x00, 0, (1, 2, 3), [(0, 0), (1, 1), (2, 2)])])
        with self.assertRaises(RetailError):
            tmd.parse(data)

    def test_textured_primitive_is_refused_rather_than_misparsed(self) -> None:
        """Textured packets carry UVs and a different payload size."""
        data = build([packet(GOURAUD_QUAD | tmd.MODE_TEXTURED, 0, (1, 2, 3),
                             [(0, 0), (1, 1), (2, 2), (3, 3)])])
        with self.assertRaises(RetailError):
            tmd.parse(data)

    def test_per_vertex_colour_variant_is_refused(self) -> None:
        """flag bit 0x04 means two words per vertex, a layout this does not read."""
        data = build([packet(GOURAUD_TRI, tmd.FLAG_NO_LIGHT, (1, 2, 3), [(0, 0), (1, 1), (2, 2)])])
        with self.assertRaises(RetailError):
            tmd.parse(data)

    def test_vertex_index_out_of_range_is_refused(self) -> None:
        data = build([packet(GOURAUD_TRI, 0, (1, 2, 3), [(0, 0), (1, 1), (2, 99)])], n_vert=4)
        with self.assertRaises(RetailError):
            tmd.parse(data)


class VertexTests(unittest.TestCase):
    def test_vertices_are_signed_16_bit_triples(self) -> None:
        data = bytearray(build([packet(GOURAUD_TRI, 0, (1, 2, 3), [(0, 0), (1, 1), (2, 2)])]))
        model = tmd.parse(bytes(data))
        self.assertEqual(len(model.objects[0].vertices), 4)
        self.assertEqual(model.objects[0].vertices[0], (0, 0, 0))


if __name__ == "__main__":
    unittest.main()
