"""Opening code comes from MAIN member4, preserved in source-loaded RAM.

Words come from the local disc extract (skipped when absent); retail code is
no longer exported in src/ (tests/test_no_retail_words.py).
"""
import struct

import pytest

from retail_local import ROOT, exe_words, overlay

MEMBER4_BASE = 0x800CEDF8 - 0x800  # file offset = vram - MEMBER4_BASE


def member4_words(start, end):
    raw = overlay('main/0004')[start - MEMBER4_BASE:end - MEMBER4_BASE]
    return list(struct.unpack(f'<{len(raw) // 4}I', raw))


def jal_targets(words):
    return [0x80000000 | ((w & 0x3FFFFFF) << 2) for w in words if w >> 26 == 3]


def test_opening_member_is_the_archive_member():
    path = ROOT / 'extracted/disc/files/MAIN.CD'
    if not path.is_file():
        pytest.skip('local MAIN.CD extract unavailable')
    archive = path.read_bytes()
    sector, size = struct.unpack_from('<II', archive, 8 + 4 * 8)
    member = archive[sector*2048:sector*2048+size]
    assert member == overlay('main/0004')
    # The EXE's actual jal target is an entry, not a branch in member7.
    assert struct.unpack_from('<I', member, 0x800+0xD0)[0] == 0x3C02800C
    demo = overlay('main/0007')
    assert struct.unpack_from('<I', demo, 0xD0)[0] == 0x10400007


def test_idle_continuation_is_a_complete_retail_function():
    """The observed idle dispatch must include CF300 through its return slot."""
    words = member4_words(0x800CF300, 0x800CF370)
    assert len(words) == 28
    assert jal_targets(words) == [0x800CF3E8, 0x80059888, 0x800118AC]
    assert words[-2:] == [0x03E00008, 0]


def test_post_fade_continuation_is_a_complete_retail_function():
    words = member4_words(0x800CF370, 0x800CF3A4)
    assert jal_targets(words) == [0x800146B0, 0x800118AC]
    assert words[-2:] == [0x03E00008, 0]


def test_next_scene_continuation_is_a_complete_retail_function():
    words = member4_words(0x800CF3A4, 0x800CF3E8)
    assert jal_targets(words) == [0x800183E0]
    assert words[-2:] == [0x03E00008, 0]


def test_post_opening_transition_includes_shared_epilogue():
    words = exe_words(0x80011380, 0x80011680)
    assert len(words) == 192
    # The disassembly's separate 11664 label is this function's return path.
    assert words[-7:] == [0x03C0E821, 0x8FBF0020, 0x8FBE001C,
                          0x8FB00018, 0x27BD0028, 0x03E00008, 0]
    jumps = [0x80000000 | ((w & 0x3FFFFFF) << 2) for w in words if w >> 26 == 2]
    assert 0x80011664 in jumps
    assert all(0x80011380 <= address < 0x80011680 for address in jumps)
