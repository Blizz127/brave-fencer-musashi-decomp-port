"""Pinned-media TOC owner fixtures, separate from native boot evidence."""
from pathlib import Path
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_pinned_toc_owner(tmp_path):
    cue = ROOT / "extracted/disc/disc.cue"
    image = ROOT / "extracted/disc/disc.bin"
    if not cue.exists() or not image.exists():
        pytest.skip("licensed pinned media unavailable")
    probe = tmp_path / "probe"
    subprocess.run([
        "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "pc_port/include"),
        str(ROOT / "tests/cd_toc_probe.c"),
        str(ROOT / "pc_port/cd_controller.c"),
        str(ROOT / "pc_port/disc_media.c"), "-lcrypto", "-o", str(probe),
    ], check=True, timeout=30)
    subprocess.run([str(probe), str(cue), str(image)], check=True, timeout=30)
