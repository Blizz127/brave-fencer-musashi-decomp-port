"""Pinned clear-image ranges: EXE, asm and the published manifest agree.

Words come from the local pinned EXE and asm/main.s (skipped when absent);
retail code is no longer exported in src/ (tests/test_no_retail_words.py).
"""
from retail_local import check_published_ranges

EXPECTED = {
    "80059888.c": (0x80059888, 0x8005991C, 37),
    "80059760.c": (0x80059760, 0x80059888, 74),
    "8005af68.c": (0x8005AF68, 0x8005B1C4, 151),
}


def test_clear_image_ranges_match_asm_and_manifest():
    check_published_ranges("clear-image-source-exports.json", EXPECTED, 262)
