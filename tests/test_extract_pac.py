from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import extract_pac  # noqa: E402
from retail_common import RetailError  # noqa: E402

SECTOR = extract_pac.SECTOR
HEADER = extract_pac.CHUNK_HEADER


def chunk(payload: bytes, kind: int = 1, flag: int = 0, field8: int = 0,
          declared: int | None = None) -> bytes:
    """One PAC chunk: a 16-byte header, its payload, padded to a sector."""

    length = HEADER + len(payload) if declared is None else declared
    head = b"PAC\x00" + bytes([kind, flag, 0, 0]) + struct.pack("<2I", field8, length)
    body = head + payload
    pad = (-len(body)) % SECTOR
    return body + b"\x00" * pad


class WalkTests(unittest.TestCase):
    def test_a_single_chunk_is_walked(self) -> None:
        chunks = extract_pac.parse_pac_chunks(chunk(b"\xAA" * 32), 0)
        self.assertEqual(len(chunks), 1)
        self.assertEqual(chunks[0].payload_size, 32)
        self.assertEqual(chunks[0].kind, 1)

    def test_consecutive_chunks_are_walked_in_order(self) -> None:
        data = chunk(b"\x11" * 16, kind=0) + chunk(b"\x22" * 4000, kind=7)
        chunks = extract_pac.parse_pac_chunks(data, 0)
        self.assertEqual([c.kind for c in chunks], [0, 7])
        self.assertEqual([c.index for c in chunks], [0, 1])

    def test_stride_is_the_length_rounded_up_to_a_sector(self) -> None:
        """The length is unpadded; a chunk still occupies whole sectors."""

        data = chunk(b"\x11" * 4000) + chunk(b"\x22" * 8)
        chunks = extract_pac.parse_pac_chunks(data, 0)
        self.assertEqual(chunks[0].offset, 0)
        self.assertEqual(chunks[1].offset, 2 * SECTOR)

    def test_payload_offset_skips_the_header(self) -> None:
        chunks = extract_pac.parse_pac_chunks(chunk(b"\xAA" * 32), 0)
        self.assertEqual(chunks[0].payload_offset, HEADER)

    def test_member_offset_is_added_to_reported_offsets(self) -> None:
        """Chunks are reported at their position in the archive, not the member."""

        chunks = extract_pac.parse_pac_chunks(chunk(b"\xAA" * 32), 0x1E1000)
        self.assertEqual(chunks[0].offset, 0x1E1000)

    def test_header_fields_are_reported(self) -> None:
        chunks = extract_pac.parse_pac_chunks(chunk(b"\xAA", kind=3, flag=1, field8=0x2F), 0)
        self.assertEqual((chunks[0].kind, chunks[0].flag, chunks[0].field8), (3, 1, 0x2F))


class RefusalTests(unittest.TestCase):
    """A misparsed archive yields plausible garbage, so every doubt is a refusal."""

    def test_a_member_without_the_magic_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            extract_pac.parse_pac_chunks(b"\x00" * SECTOR, 0)

    def test_a_lost_magic_mid_walk_is_refused(self) -> None:
        data = chunk(b"\xAA" * 32) + b"\xFF" * SECTOR
        with self.assertRaises(RetailError):
            extract_pac.parse_pac_chunks(data, 0)

    def test_a_length_shorter_than_the_header_is_refused(self) -> None:
        """A zero or tiny length would otherwise loop forever."""

        with self.assertRaises(RetailError):
            extract_pac.parse_pac_chunks(chunk(b"", declared=0), 0)

    def test_a_length_running_past_the_member_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            extract_pac.parse_pac_chunks(chunk(b"\xAA" * 32, declared=SECTOR * 9), 0)

    def test_a_walk_that_does_not_close_on_the_member_end_is_refused(self) -> None:
        """Trailing bytes no chunk claims mean the layout was misread."""

        with self.assertRaises(RetailError):
            extract_pac.parse_pac_chunks(chunk(b"\xAA" * 32) + b"\x00" * SECTOR, 0)

    def test_a_truncated_member_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            extract_pac.parse_pac_chunks(chunk(b"\xAA" * 32)[: SECTOR - 8], 0)

    def test_an_empty_member_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            extract_pac.parse_pac_chunks(b"", 0)


class ClassifyTests(unittest.TestCase):
    """Eight members of SC01.CD are not PAC containers and must not be forced."""

    def test_a_pac_member_is_recognised(self) -> None:
        self.assertTrue(extract_pac.is_pac_member(chunk(b"\xAA")))

    def test_a_sqv_member_is_not_a_pac_member(self) -> None:
        self.assertFalse(extract_pac.is_pac_member(b".sqv" + b"\x00" * 64))

    def test_a_member_too_short_to_classify_is_not_a_pac_member(self) -> None:
        self.assertFalse(extract_pac.is_pac_member(b"PA"))


if __name__ == "__main__":
    unittest.main()
