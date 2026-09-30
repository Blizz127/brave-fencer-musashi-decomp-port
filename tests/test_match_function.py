from __future__ import annotations

import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import match_function  # noqa: E402
from retail_common import RetailError  # noqa: E402


TEXT_ADDRESS = 0x80010000
TEXT_SIZE = 0x200
HEADER_SIZE = 0x800


def synthetic_exe(path: Path, text: bytes) -> dict[str, object]:
    """Write a PS-X EXE whose text segment is exactly `text`."""

    header = bytearray(HEADER_SIZE)
    header[0:8] = b"PS-X EXE"
    struct.pack_into("<I", header, 0x18, TEXT_ADDRESS)
    struct.pack_into("<I", header, 0x1C, len(text))
    path.write_bytes(bytes(header) + text)
    return {"text_address": TEXT_ADDRESS, "text_size_bytes": len(text)}


def write_identity(root: Path, exe: Path, header: dict[str, object]) -> Path:
    path = root / "exe_identity.json"
    path.write_text(
        json.dumps(
            {
                "status": "observed",
                "path": str(exe),
                "sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
                "header": header,
            }
        ),
        encoding="utf-8",
    )
    return path


class OffsetTests(unittest.TestCase):
    HEADER = {"text_address": TEXT_ADDRESS, "text_size_bytes": TEXT_SIZE}

    def test_text_start_maps_to_end_of_exe_header(self) -> None:
        self.assertEqual(match_function.vram_to_offset(TEXT_ADDRESS, self.HEADER), HEADER_SIZE)

    def test_offset_is_linear_past_text_start(self) -> None:
        self.assertEqual(
            match_function.vram_to_offset(TEXT_ADDRESS + 0x100, self.HEADER), HEADER_SIZE + 0x100
        )

    def test_address_below_text_start_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            match_function.vram_to_offset(TEXT_ADDRESS - 4, self.HEADER)

    def test_address_past_text_end_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            match_function.vram_to_offset(TEXT_ADDRESS + TEXT_SIZE, self.HEADER)


class ExtractTests(unittest.TestCase):
    def test_extracts_exactly_the_requested_range(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            text = bytes(range(256)) * 2
            exe = root / "SLUS_007.26"
            header = synthetic_exe(exe, text)
            got = match_function.extract_retail_bytes(exe, header, TEXT_ADDRESS + 0x10, 0x20)
            self.assertEqual(got, text[0x10:0x30])

    def test_range_overrunning_text_end_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exe = root / "SLUS_007.26"
            header = synthetic_exe(exe, bytes(TEXT_SIZE))
            with self.assertRaises(RetailError):
                match_function.extract_retail_bytes(exe, header, TEXT_ADDRESS + TEXT_SIZE - 8, 0x10)

    def test_unaligned_size_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exe = root / "SLUS_007.26"
            header = synthetic_exe(exe, bytes(TEXT_SIZE))
            with self.assertRaises(RetailError):
                match_function.extract_retail_bytes(exe, header, TEXT_ADDRESS, 6)

    def test_unaligned_address_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exe = root / "SLUS_007.26"
            header = synthetic_exe(exe, bytes(TEXT_SIZE))
            with self.assertRaises(RetailError):
                match_function.extract_retail_bytes(exe, header, TEXT_ADDRESS + 2, 4)


class CompareTests(unittest.TestCase):
    def test_identical_input_is_a_match(self) -> None:
        data = struct.pack("<4I", 1, 2, 3, 4)
        report = match_function.compare_words(data, data)
        self.assertTrue(report["matching"])
        self.assertEqual(report["total_words"], 4)
        self.assertEqual(report["matched_words"], 4)
        self.assertIsNone(report["first_difference_word"])

    def test_single_differing_word_is_located(self) -> None:
        retail = struct.pack("<4I", 1, 2, 3, 4)
        candidate = struct.pack("<4I", 1, 2, 0xDEAD, 4)
        report = match_function.compare_words(candidate, retail)
        self.assertFalse(report["matching"])
        self.assertEqual(report["matched_words"], 3)
        self.assertEqual(report["first_difference_word"], 2)
        self.assertAlmostEqual(report["match_ratio"], 0.75, places=6)

    def test_length_mismatch_is_refused_rather_than_padded(self) -> None:
        with self.assertRaises(RetailError):
            match_function.compare_words(struct.pack("<2I", 1, 2), struct.pack("<3I", 1, 2, 3))

    def test_empty_range_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            match_function.compare_words(b"", b"")


class BlobTests(unittest.TestCase):
    """Overlay code is not in the executable, so the oracle must accept a blob.

    An overlay function's address lies outside SLUS_007.26's text range, so the
    pinned-executable path refuses it. A blob is accepted instead, but only with
    its load base and its hash, keeping the same fail-closed discipline: an
    unpinned blob could be anything.
    """

    BASE = 0x800CEDF8

    def _blob(self, root: Path) -> tuple[Path, bytes, str]:
        payload = struct.pack("<8I", *range(0x10, 0x18))
        path = root / "0007.bin"
        path.write_bytes(payload)
        return path, payload, hashlib.sha256(payload).hexdigest()

    def test_extracts_a_range_relative_to_the_load_base(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path, payload, _ = self._blob(root)
            got = match_function.extract_blob_bytes(path, self.BASE, self.BASE + 0x8, 0x8)
            self.assertEqual(got, payload[0x8:0x10])

    def test_address_below_the_load_base_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path, _, _ = self._blob(root)
            with self.assertRaises(RetailError):
                match_function.extract_blob_bytes(path, self.BASE, self.BASE - 4, 0x4)

    def test_range_past_the_blob_end_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path, payload, _ = self._blob(root)
            with self.assertRaises(RetailError):
                match_function.extract_blob_bytes(
                    path, self.BASE, self.BASE + len(payload) - 4, 0x10
                )

    def test_blob_mode_requires_a_base_and_a_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path, _, _ = self._blob(root)
            code = match_function.main(
                ["--retail-file", str(path), "--vram", hex(self.BASE), "--size", "0x8"]
            )
            self.assertEqual(code, 2)

    def test_hash_mismatch_on_the_blob_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path, _, _ = self._blob(root)
            code = match_function.main(
                [
                    "--retail-file", str(path),
                    "--base", hex(self.BASE),
                    "--sha256", "0" * 64,
                    "--vram", hex(self.BASE),
                    "--size", "0x8",
                ]
            )
            self.assertEqual(code, 2)

    def test_matching_candidate_against_a_pinned_blob_exits_zero(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path, payload, digest = self._blob(root)
            candidate = root / "c.bin"
            candidate.write_bytes(payload[:0x8])
            code = match_function.main(
                [
                    "--retail-file", str(path),
                    "--base", hex(self.BASE),
                    "--sha256", digest,
                    "--vram", hex(self.BASE),
                    "--size", "0x8",
                    "--candidate", str(candidate),
                ]
            )
            self.assertEqual(code, 0)


class CliTests(unittest.TestCase):
    def _fixture(self, root: Path) -> tuple[Path, bytes]:
        text = struct.pack("<8I", *range(8))
        exe = root / "SLUS_007.26"
        header = synthetic_exe(exe, text)
        return write_identity(root, exe, header), text

    def test_hash_mismatch_stops_before_reporting(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            identity, _ = self._fixture(root)
            (root / "SLUS_007.26").write_bytes(b"tampered")
            result = match_function.main(
                ["--identity", str(identity), "--vram", hex(TEXT_ADDRESS), "--size", "0x10"]
            )
            self.assertEqual(result, 2)

    def test_matching_candidate_exits_zero(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            identity, text = self._fixture(root)
            candidate = root / "candidate.bin"
            candidate.write_bytes(text[:0x10])
            result = match_function.main(
                [
                    "--identity", str(identity),
                    "--vram", hex(TEXT_ADDRESS),
                    "--size", "0x10",
                    "--candidate", str(candidate),
                ]
            )
            self.assertEqual(result, 0)

    def test_mismatching_candidate_exits_one(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            identity, text = self._fixture(root)
            candidate = root / "candidate.bin"
            candidate.write_bytes(b"\x00" * 0x10)
            result = match_function.main(
                [
                    "--identity", str(identity),
                    "--vram", hex(TEXT_ADDRESS),
                    "--size", "0x10",
                    "--candidate", str(candidate),
                ]
            )
            self.assertEqual(result, 1)

    def test_without_candidate_reports_range_digest_only(self) -> None:
        """No candidate means no comparison; the range is summarised, not dumped."""

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            identity, text = self._fixture(root)
            result = match_function.main(
                ["--identity", str(identity), "--vram", hex(TEXT_ADDRESS), "--size", "0x10"]
            )
            self.assertEqual(result, 0)


if __name__ == "__main__":
    unittest.main()
