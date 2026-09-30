"""Direct compiled tests for the pinned immutable mounted-media owner."""
from pathlib import Path
import hashlib
import subprocess
import tempfile

import pytest

ROOT = Path(__file__).resolve().parents[1]
CUE_SHA256 = "85326d3f735d603a9d37f0e0754a37a52cc9d0bf6b8b4ce566c4fa54f6fdee5c"
BIN_SHA256 = "0a53702937d74e20d99fee9a29a80f7e91da762e66958819d531173939a50879"
BIN_SIZE = 416021760


def _pinned_media():
    cue = ROOT / "extracted/disc/disc.cue"
    binary = ROOT / "extracted/disc/disc.bin"
    if not cue.exists() or not binary.exists():
        pytest.skip("licensed pinned disc image is unavailable")
    assert cue.stat().st_size == 250, "present cue is not the pinned payload"
    assert binary.stat().st_size == BIN_SIZE, "present BIN is not the pinned payload"
    assert hashlib.sha256(cue.read_bytes()).hexdigest() == CUE_SHA256, (
        "present cue is not the pinned payload"
    )
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == BIN_SHA256, (
        "present BIN is not the pinned payload"
    )
    return cue, binary


def test_pinned_media_owner_direct_compile():
    cue, binary = _pinned_media()
    with tempfile.TemporaryDirectory(prefix="musashi-disc-media-") as temp:
        probe = Path(temp) / "disc_media_probe"
        subprocess.run(
            [
                "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                "-I", str(ROOT / "pc_port/include"),
                str(ROOT / "tests/disc_media_probe.c"),
                str(ROOT / "pc_port/disc_media.c"),
                "-lcrypto", "-o", str(probe),
            ],
            check=True,
            timeout=30,
        )
        subprocess.run([str(probe), str(cue), str(binary)], check=True,
                       timeout=30)
