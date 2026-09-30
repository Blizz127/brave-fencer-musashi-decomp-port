"""Pinned camera ranges: EXE, asm and the published manifest agree.

Words come from the local pinned EXE and asm/main.s (skipped when absent);
retail code is no longer exported in src/ (tests/test_no_retail_words.py).
"""
import hashlib

from retail_local import check_published_ranges, manifest, ROOT

EXPECTED = {
    "800144d4.c": (0x800144D4, 0x80014554, 32),
    "80053308.c": (0x80053308, 0x80053328, 8),
    "80053f6c.c": (0x80053F6C, 0x80054340, 245),
    "80053b20.c": (0x80053B20, 0x80053BD8, 46),
    "8004787c.c": (0x8004787C, 0x800478B8, 15),
    "800478b8.c": (0x800478B8, 0x80047948, 36),
    "80047948.c": (0x80047948, 0x800479E8, 40),
    "80054340.c": (0x80054340, 0x80054430, 60),
    "80054430.c": (0x80054430, 0x800544F8, 50),
    "800544f8.c": (0x800544F8, 0x80054514, 7),
    "80047d3c.c": (0x80047D3C, 0x80047DC0, 33),
}


def test_camera_ranges_match_asm_and_manifest():
    assert sum(count for _, _, count in EXPECTED.values()) == 572
    check_published_ranges("camera-source-exports.json", EXPECTED, 572)


def test_80053308_c_body_matches_published_digest():
    """The decompiled C body the manifest recorded is still the source."""
    entry = next(item for item in manifest("camera-source-exports.json")["exports"]
                 if item["address"] == "80053308")
    text = (ROOT / "src/main/80053308.c").read_text()
    body = text[text.index("#include"):]
    assert hashlib.sha256(body.encode()).hexdigest() == entry["preserved_else_sha256"]
    assert entry["preserved_else_sha256"] == (
        "e58f1e5b00e9ac6062f2bd623e8b2f80e58804f2722586aa2d85e1ddf215444c"
    )
