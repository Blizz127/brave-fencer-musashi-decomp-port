from __future__ import annotations

import hashlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import extract_tmd  # noqa: E402
import tmd  # noqa: E402
from retail_common import RetailError  # noqa: E402


def minimal_model() -> bytes:
    """A one-triangle TMD, laid out the way the real models are."""
    n_vert = n_normal = 3
    packet = bytes([0, 4, 0, 0x30]) + bytes([9, 9, 9, 0x30])
    for slot in range(3):
        packet += struct.pack("<2H", slot, slot)
    prim_top = (12 + 28) - 12
    vert_top = prim_top + len(packet)
    normal_top = vert_top + n_vert * 8
    header = struct.pack("<3I", tmd.TMD_ID, 0, 1)
    entry = struct.pack("<7I", vert_top, n_vert, normal_top, n_normal, prim_top, 1, 0)
    return header + entry + packet + bytes(n_vert * 8) + bytes(n_normal * 8)


class LengthTests(unittest.TestCase):
    """A TMD carries no total length, so it is measured by walking it."""

    def test_measures_a_model_from_its_own_structure(self) -> None:
        blob = minimal_model()
        # Trailing bytes must not be included in the extracted model.
        self.assertEqual(extract_tmd.measure(blob + b"\xAA" * 64), len(blob))

    def test_a_range_that_is_not_a_tmd_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            extract_tmd.measure(b"\x00" * 128)

    def test_a_truncated_model_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            extract_tmd.measure(minimal_model()[:20])


class ExtractTests(unittest.TestCase):
    def _archive(self, root: Path, offset: int = 0x800) -> tuple[Path, bytes]:
        model = minimal_model()
        blob = bytearray(b"\x5A" * offset) + bytearray(model) + bytearray(b"\x5A" * 64)
        path = root / "ARCHIVE.CD"
        path.write_bytes(bytes(blob))
        return path, model

    def test_writes_exactly_the_model_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, model = self._archive(root)
            out = root / "model.tmd"
            record = extract_tmd.extract(archive, 0x800, out)
            self.assertEqual(out.read_bytes(), model)
            self.assertEqual(record["size_bytes"], len(model))
            self.assertEqual(record["sha256"], hashlib.sha256(model).hexdigest())

    def test_records_the_offset_it_came_from(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, _ = self._archive(root)
            record = extract_tmd.extract(archive, 0x800, root / "m.tmd")
            self.assertEqual(record["offset"], 0x800)

    def test_an_offset_that_is_not_a_tmd_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, _ = self._archive(root)
            with self.assertRaises(RetailError):
                extract_tmd.extract(archive, 0x0, root / "m.tmd")

    def test_refuses_to_overwrite_an_existing_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, _ = self._archive(root)
            out = root / "m.tmd"
            out.write_bytes(b"existing")
            with self.assertRaises(RetailError):
                extract_tmd.extract(archive, 0x800, out)

    def test_offset_past_the_archive_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, _ = self._archive(root)
            with self.assertRaises(RetailError):
                extract_tmd.extract(archive, 0x900000, root / "m.tmd")


if __name__ == "__main__":
    unittest.main()
