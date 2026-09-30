"""Pinned draw-environment ranges: EXE, asm and the published manifest agree.

Words come from the local pinned EXE and asm/main.s (skipped when absent);
retail code is no longer exported in src/ (tests/test_no_retail_words.py).
"""
import hashlib

from retail_local import check_published_ranges, manifest, ROOT

EXPECTED = {
    "80058a4c.c": (0x80058A4C, 0x80058B04, 46),
    "80058b04.c": (0x80058B04, 0x80058B40, 15),
    "80014960.c": (0x80014960, 0x80014998, 14),
    "80014998.c": (0x80014998, 0x800149E0, 18),
}


def test_draw_env_ranges_match_asm_and_manifest():
    check_published_ranges("draw-env-source-exports.json", EXPECTED, 93)


def test_80014960_c_body_matches_published_digest():
    """The decompiled C body the manifest recorded is still the source."""
    entry = next(item for item in manifest("draw-env-source-exports.json")["exports"]
                 if item["address"] == "80014960")
    text = (ROOT / "src/main/80014960.c").read_text()
    body = text[text.index("#include"):]
    assert hashlib.sha256(body.encode()).hexdigest() == entry["preserved_c_sha256"]
