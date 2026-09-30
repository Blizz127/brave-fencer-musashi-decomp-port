"""Pinned draw follow-up ranges: EXE, asm and the published manifest agree.

Words come from the local pinned EXE and asm/main.s (skipped when absent);
retail code is no longer exported in src/ (tests/test_no_retail_words.py).
"""
from retail_local import check_published_ranges

EXPECTED = {
    "80053218.c": (0x80053218, 0x80053290, 30),
    "80014774.c": (0x80014774, 0x800147B8, 17),
    "800147b8.c": (0x800147B8, 0x80014928, 92),
}


def test_draw_followup_ranges_match_asm_and_manifest():
    check_published_ranges("draw-followup-source-exports.json", EXPECTED, 139)
