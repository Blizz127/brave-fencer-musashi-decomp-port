"""Owned drive tests use a real pinned mount and an explicit hardware fixture."""
from pathlib import Path
import subprocess
import tempfile
import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_cd_owned_direct_compile():
    cue = ROOT / "extracted/disc/disc.cue"
    binary = ROOT / "extracted/disc/disc.bin"
    if not cue.exists() or not binary.exists():
        pytest.skip("licensed pinned media unavailable")
    with tempfile.TemporaryDirectory(prefix="musashi-cd-owned-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/cd_owned_probe.c"),
            str(ROOT / "pc_port/cd_controller.c"),
            str(ROOT / "pc_port/disc_media.c"), "-lcrypto", "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe), str(cue), str(binary)], check=True, timeout=30)
