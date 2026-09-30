"""Direct deterministic tests for the bounded SPU/CD PCM owner."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_spu_cd_audio_direct_compile():
    with tempfile.TemporaryDirectory(prefix="musashi-spu-cd-audio-") as temp:
        probe = Path(temp) / "spu_cd_audio_probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/spu_cd_audio_probe.c"),
            str(ROOT / "pc_port/spu_cd_audio.c"),
            "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe)], check=True, timeout=10)
