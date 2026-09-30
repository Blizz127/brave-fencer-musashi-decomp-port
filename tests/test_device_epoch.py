from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_device_epoch_order_elapsed_time_and_refusals():
    with tempfile.TemporaryDirectory(prefix="musashi-device-epoch-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/device_epoch_probe.c"),
            str(ROOT / "pc_port/device_epoch.c"), "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe)], check=True, timeout=10)
