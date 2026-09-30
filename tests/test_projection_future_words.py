"""Pinned future projection continuation: EXE, asm and the manifest agree.

Words come from the local pinned EXE and asm/main.s (skipped when absent);
retail code is no longer exported in src/ (tests/test_no_retail_words.py).
"""
from retail_local import check_published_ranges

EXPECTED = {"80053178.c": (0x80053178, 0x80053218, 40)}


def test_future_projection_range_matches_asm_and_manifest():
    check_published_ranges("projection-offset-source-exports.json", EXPECTED, 40)
