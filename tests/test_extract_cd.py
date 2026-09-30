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

import extract_cd  # noqa: E402
from retail_common import RetailError  # noqa: E402


SECTOR = 0x800


def header(entries: list[tuple[int, int]], count: int | None = None) -> bytes:
    """Build a 0x800 archive header: u32 count, u32 pad, then 8 bytes per entry."""

    blob = bytearray(SECTOR)
    struct.pack_into("<2I", blob, 0, count if count is not None else len(entries), 0)
    for index, (sector, size) in enumerate(entries):
        struct.pack_into("<2I", blob, 8 + index * 8, sector, size)
    return bytes(blob)


class ParseTests(unittest.TestCase):
    def test_parses_entries_with_sector_scaled_offsets(self) -> None:
        data = header([(1, 100), (12, 200)])
        parsed = extract_cd.parse_cd_header(data, archive_size=64 * SECTOR)
        self.assertEqual(len(parsed), 2)
        self.assertEqual((parsed[0].offset, parsed[0].size), (SECTOR, 100))
        self.assertEqual((parsed[1].offset, parsed[1].size), (12 * SECTOR, 200))

    def test_entries_carry_their_index(self) -> None:
        parsed = extract_cd.parse_cd_header(header([(1, 10), (2, 20)]), archive_size=64 * SECTOR)
        self.assertEqual([entry.index for entry in parsed], [0, 1])


class RefusalTests(unittest.TestCase):
    """Fail closed. A misparsed archive silently yields plausible garbage."""

    def test_truncated_header_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            extract_cd.parse_cd_header(b"\x00" * (SECTOR - 1), archive_size=64 * SECTOR)

    def test_entry_running_past_the_archive_end_is_refused(self) -> None:
        # One sector in, but claiming more bytes than the archive holds.
        data = header([(1, 10 * SECTOR)])
        with self.assertRaises(RetailError):
            extract_cd.parse_cd_header(data, archive_size=4 * SECTOR)

    def test_count_beyond_header_capacity_is_refused(self) -> None:
        # (0x800 - 8) / 8 == 255 entries fit; 256 cannot.
        with self.assertRaises(RetailError):
            extract_cd.parse_cd_header(header([], count=256), archive_size=64 * SECTOR)

    def test_zero_count_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            extract_cd.parse_cd_header(header([], count=0), archive_size=64 * SECTOR)

    def test_entry_starting_inside_the_header_is_refused(self) -> None:
        """Sector 0 is the header itself, so no member can begin there."""

        with self.assertRaises(RetailError):
            extract_cd.parse_cd_header(header([(0, 16)]), archive_size=64 * SECTOR)


class ExtractTests(unittest.TestCase):
    def _archive(self, root: Path) -> tuple[Path, list[bytes]]:
        members = [b"first member", b"second member payload"]
        blob = bytearray(header([(1, len(members[0])), (2, len(members[1]))]))
        blob += bytearray(SECTOR * 2)
        blob[SECTOR : SECTOR + len(members[0])] = members[0]
        blob[SECTOR * 2 : SECTOR * 2 + len(members[1])] = members[1]
        path = root / "TEST.CD"
        path.write_bytes(bytes(blob))
        return path, members

    def test_writes_each_member_with_its_exact_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, members = self._archive(root)
            out = root / "out"
            written = extract_cd.extract_archive(archive, out)
            self.assertEqual(len(written), 2)
            self.assertEqual((out / "0000.bin").read_bytes(), members[0])
            self.assertEqual((out / "0001.bin").read_bytes(), members[1])

    def test_reports_size_and_sha256_per_member(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, members = self._archive(root)
            written = extract_cd.extract_archive(archive, root / "out")
            self.assertEqual(written[0]["size_bytes"], len(members[0]))
            self.assertEqual(
                written[0]["sha256"], hashlib.sha256(members[0]).hexdigest()
            )

    def test_refuses_to_overwrite_an_existing_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, _ = self._archive(root)
            out = root / "out"
            out.mkdir()
            with self.assertRaises(RetailError):
                extract_cd.extract_archive(archive, out)


if __name__ == "__main__":
    unittest.main()
