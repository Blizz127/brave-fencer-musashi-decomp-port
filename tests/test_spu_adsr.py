"""Independent vectors for the bounded PsyCross ADSR adaptation."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def test_spu_adsr_vectors():
    with tempfile.TemporaryDirectory(prefix="musashi-spu-adsr-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-O2",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/spu_adsr_probe.c"), "-o", str(probe),
        ], check=True, timeout=30)
        result = subprocess.run([str(probe)], check=True, timeout=10,
                                capture_output=True, text=True)
        assert result.stdout == "SPU_ADSR_PASS\n"
