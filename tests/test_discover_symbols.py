from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import discover_symbols  # noqa: E402
from retail_common import RetailError  # noqa: E402


BASE = 0x80100158


def word(*words: int) -> bytes:
    return b"".join(struct.pack("<I", w) for w in words)


def addiu_sp(imm: int) -> int:
    """addiu $sp, $sp, imm — opcode 9, rs=rt=29."""
    return (9 << 26) | (29 << 21) | (29 << 16) | (imm & 0xFFFF)


def jal(target: int) -> int:
    return (3 << 26) | ((target >> 2) & 0x03FFFFFF)


NOP = 0


class PrologueTests(unittest.TestCase):
    def test_finds_a_stack_frame_prologue(self) -> None:
        data = word(NOP, addiu_sp(-0x18), NOP)
        self.assertEqual(discover_symbols.find_prologue_offsets(data), [4])

    def test_positive_stack_adjustment_is_an_epilogue_not_a_prologue(self) -> None:
        """addiu $sp, $sp, +N tears the frame down; it is not a function start."""
        self.assertEqual(discover_symbols.find_prologue_offsets(word(addiu_sp(0x18))), [])

    def test_addiu_on_another_register_is_ignored(self) -> None:
        other = (9 << 26) | (4 << 21) | (4 << 16) | 0xFFE8
        self.assertEqual(discover_symbols.find_prologue_offsets(word(other)), [])


class JalTests(unittest.TestCase):
    def test_decodes_an_absolute_target(self) -> None:
        self.assertEqual(discover_symbols.find_jal_targets(word(jal(0x80100200))), [0x80100200])

    def test_duplicate_targets_collapse(self) -> None:
        data = word(jal(0x80100200), NOP, jal(0x80100200))
        self.assertEqual(discover_symbols.find_jal_targets(data), [0x80100200])


class DiscoveryTests(unittest.TestCase):
    """Prologues alone undercount: a frameless leaf never adjusts $sp, so it is
    invisible unless something calls it. Call targets supply those."""

    def test_combines_prologues_and_call_targets(self) -> None:
        # A frameless leaf at +0x08 that only a jal reveals.
        data = word(addiu_sp(-0x10), jal(BASE + 8), NOP, NOP)
        found = discover_symbols.discover_function_starts(data, BASE)
        self.assertIn(BASE + 0, found)
        self.assertIn(BASE + 8, found)

    def test_results_are_sorted_and_deduplicated(self) -> None:
        data = word(addiu_sp(-0x10), jal(BASE + 0), NOP, addiu_sp(-0x10))
        found = discover_symbols.discover_function_starts(data, BASE)
        self.assertEqual(found, sorted(set(found)))

    def test_call_targets_outside_the_member_are_excluded(self) -> None:
        """Calls into the executable or another overlay are not our symbols."""
        data = word(jal(0x80010204), jal(BASE + 4), NOP, NOP)
        found = discover_symbols.discover_function_starts(data, BASE)
        self.assertNotIn(0x80010204, found)
        self.assertIn(BASE + 4, found)

    def test_unaligned_base_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            discover_symbols.discover_function_starts(word(NOP), BASE + 2)

    def test_empty_member_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            discover_symbols.discover_function_starts(b"", BASE)


class RenderTests(unittest.TestCase):
    def test_renders_splat_symbol_addrs_syntax(self) -> None:
        rendered = discover_symbols.render_symbol_addrs([0x80100158])
        self.assertIn("func_80100158 = 0x80100158; // type:func", rendered)

    def test_every_address_gets_a_line(self) -> None:
        rendered = discover_symbols.render_symbol_addrs([0x80100158, 0x80100200])
        self.assertEqual(len([l for l in rendered.splitlines() if "type:func" in l]), 2)


class RegionTests(unittest.TestCase):
    """A member may be a container: PAC chunks of data ahead of the code.

    Scanning a data chunk invents function starts out of bytes that merely look
    like a prologue, so discovery is restricted to the code region and the base
    refers to that region's first byte.
    """

    def test_only_the_requested_region_is_scanned(self) -> None:
        # A prologue-shaped word in the skipped prefix must not be reported.
        data = word(addiu_sp(-0x10), addiu_sp(-0x20))
        found = discover_symbols.discover_function_starts(data, BASE, from_offset=4)
        self.assertEqual(found, [BASE])

    def test_base_addresses_the_start_of_the_region(self) -> None:
        data = word(NOP, addiu_sp(-0x10))
        self.assertEqual(discover_symbols.discover_function_starts(data, BASE, from_offset=4), [BASE])

    def test_call_targets_are_bounded_by_the_region_not_the_file(self) -> None:
        data = word(NOP, jal(BASE + 4), NOP, NOP)
        found = discover_symbols.discover_function_starts(data, BASE, from_offset=4)
        self.assertIn(BASE + 4, found)

    def test_offset_past_the_member_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            discover_symbols.discover_function_starts(word(NOP), BASE, from_offset=64)

    def test_unaligned_offset_is_refused(self) -> None:
        with self.assertRaises(RetailError):
            discover_symbols.discover_function_starts(word(NOP, NOP), BASE, from_offset=2)


if __name__ == "__main__":
    unittest.main()
