"""Pinned projection ranges: EXE, asm and the published manifest agree.

Words come from the local pinned EXE and asm/main.s (skipped when absent);
retail code is no longer exported in src/ (tests/test_no_retail_words.py).
"""
from retail_local import check_published_ranges

EXPECTED = {
    "8005283c.c": (0x8005283C, 0x80052AA0, 153),
    "80052d00.c": (0x80052D00, 0x80052D90, 36),
    "80052bec.c": (0x80052BEC, 0x80052D00, 69),
}


def test_projection_ranges_match_asm_and_manifest():
    check_published_ranges("projection-source-exports.json", EXPECTED, 258)
