"""The startup serial owner configures idle lines without inventing transfers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_idle_sio_configuration_and_refusal():
    with tempfile.TemporaryDirectory(prefix="musashi-sio-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/sio_controller_probe.c"),
            str(ROOT / "pc_port/sio_controller.c"), "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe)], check=True, timeout=10)
