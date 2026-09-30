from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import find_tmd  # noqa: E402
import tmd  # noqa: E402


def tiny_tmd() -> bytes:
    """One object, three vertices, one normal, one flat triangle."""

    n_vert, n_normal = 3, 1
    verts = b"".join(struct.pack("<4h", x, y, 0, 0) for x, y in ((0, 0), (10, 0), (0, 10)))
    normals = struct.pack("<4h", 0, 0, 4096, 0)
    prim = bytes([0x03, 0x04, 0x00, 0x20]) + bytes([200, 100, 50, 0]) \
        + struct.pack("<2H", 0, 0) + struct.pack("<2H", 0, 1) + struct.pack("<2H", 0, 2)
    table_end = tmd.OBJECT_TABLE_OFFSET + tmd.OBJECT_ENTRY_SIZE
    prim_top = table_end - tmd.OBJECT_TABLE_OFFSET
    vert_top = prim_top + len(prim)
    normal_top = vert_top + len(verts)
    entry = struct.pack("<7I", vert_top, n_vert, normal_top, n_normal, prim_top, 1, 0)
    return struct.pack("<3I", tmd.TMD_ID, 0, 1) + entry + prim + verts + normals


class ScanTests(unittest.TestCase):
    def test_a_model_at_the_front_is_found(self) -> None:
        hits = find_tmd.scan(tiny_tmd())
        self.assertEqual([h.offset for h in hits], [0])

    def test_a_model_embedded_in_a_chunk_is_found_at_its_offset(self) -> None:
        data = b"\x00" * 0x40 + tiny_tmd() + b"\xFF" * 16
        hits = find_tmd.scan(data)
        self.assertEqual([h.offset for h in hits], [0x40])
        self.assertEqual(hits[0].length, len(tiny_tmd()))

    def test_the_found_length_matches_the_model(self) -> None:
        hits = find_tmd.scan(tiny_tmd())
        self.assertEqual(hits[0].length, len(tiny_tmd()))
        self.assertEqual(hits[0].primitives, 1)

    def test_the_id_word_alone_is_not_a_hit(self) -> None:
        """0x41 is a common word. Only a structure that parses to its end counts."""

        data = struct.pack("<I", tmd.TMD_ID) + b"\x00" * 64
        self.assertEqual(find_tmd.scan(data), [])

    def test_a_model_truncated_by_the_chunk_end_is_not_a_hit(self) -> None:
        self.assertEqual(find_tmd.scan(tiny_tmd()[:-4]), [])

    def test_only_word_aligned_offsets_are_considered(self) -> None:
        data = b"\x00" * 2 + tiny_tmd()
        self.assertEqual(find_tmd.scan(data), [])

    def test_two_models_are_found_in_order(self) -> None:
        one = tiny_tmd()
        pad = b"\x00" * ((-len(one)) % 4)
        hits = find_tmd.scan(one + pad + one)
        self.assertEqual([h.offset for h in hits], [0, len(one) + len(pad)])

    def test_scanning_resumes_after_a_model_not_inside_it(self) -> None:
        """A model's own body must not yield nested false positives."""

        one = tiny_tmd()
        hits = find_tmd.scan(one)
        self.assertEqual(len(hits), 1)


if __name__ == "__main__":
    unittest.main()
