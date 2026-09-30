"""Atomic DMA3 source-profile fixtures, separate from native execution proof."""
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_dma3_owners_and_atomic_copy(tmp_path):
    probe = tmp_path / "probe"
    subprocess.run([
        "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "pc_port/include"),
        str(ROOT / "tests/cd_dma3_probe.c"),
        str(ROOT / "pc_port/cd_dma3.c"), str(ROOT / "pc_port/dma_controller.c"),
        "-o", str(probe),
    ], check=True, timeout=30)
    run = subprocess.run([str(probe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout == "CD_DMA3_SOURCE_PROFILE_PASS\n"


def test_dma3_profile_matches_actual_45090_words():
    exe = (ROOT / "extracted/disc/files/SLUS_007.26").read_bytes()
    asm = (ROOT / "asm/main.s").read_text()
    rows = re.findall(r"/\* [0-9A-F]+ (80045[01][0-9A-F]{2}) ([0-9A-F]{8}) \*/", asm)
    actual = {int(pc, 16): bytes.fromhex(word) for pc, word in rows
              if 0x80045090 <= int(pc, 16) < 0x80045190}
    assert len(actual) == 64
    base = struct.unpack_from("<I", exe, 0x18)[0]
    for pc, word in actual.items():
        assert exe[0x800+pc-base:0x804+pc-base] == word
    for pc, word in {
        0x800450B8: 0x34C60943,  # DELAY20943 (upper2 loaded at45098)
        0x800450E4: 0x34428000,  # DPCRenable
        0x800450F8: 0xAC440000,  # actual caller destination
        0x80045108: 0x00A22825,  # words|10000
        0x8004512C: 0x3C021100,  # busy+trigger
        0x8004513C: 0xAC620000,  # CHCRstore
        0x8004516C: 0x00441024,  # test busybit
    }.items():
        assert struct.unpack("<I", actual[pc])[0] == word


def test_dma3_pinned_pvd_through_actual_cd_fifo(tmp_path):
    """Real immutable media/sector/FIFO owners; test-owned command/clock fixture."""
    import pytest
    cue, image = ROOT / "extracted/disc/disc.cue", ROOT / "extracted/disc/disc.bin"
    if not cue.exists() or not image.exists():
        pytest.skip("licensed pinned media unavailable")
    probe = tmp_path / "mounted"
    subprocess.run([
        "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DDMA3_PINNED_MEDIA",
        "-I", str(ROOT / "pc_port/include"), str(ROOT / "tests/cd_dma3_probe.c"),
        str(ROOT / "pc_port/cd_dma3.c"), str(ROOT / "pc_port/dma_controller.c"),
        str(ROOT / "pc_port/cd_controller.c"), str(ROOT / "pc_port/disc_media.c"),
        "-lcrypto", "-o", str(probe),
    ], check=True, timeout=30)
    subprocess.run([str(probe), str(cue), str(image)], check=True, timeout=30)
