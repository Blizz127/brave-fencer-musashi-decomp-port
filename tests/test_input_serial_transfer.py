"""Source-backed first DATA/01 input serial transfer stage."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image


def test_input_serial_first_data_stage_and_refusals():
    with tempfile.TemporaryDirectory(prefix="musashi-input-serial-") as temp:
        root = Path(temp)
        generated = root / "generated"
        generated.mkdir()
        _generate_formatter_includes(generated)
        probe = root / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-O2", "-Wno-parentheses", "-ffunction-sections", "-fdata-sections",
            "-Wl,--gc-sections", "-I", str(ROOT / "include"),
            "-I", str(ROOT / "pc_port/include"), "-I", str(generated),
            str(ROOT / "tests/input_serial_transfer_probe.c"),
            str(ROOT / "pc_port/boot_memory.c"),
            str(ROOT / "pc_port/sio_controller.c"),
            str(ROOT / "pc_port/timer2.c"), "-lcrypto", "-o", str(probe),
        ], check=True, timeout=30)
        _sync_code_image(generated)
        subprocess.run([str(probe)], check=True, timeout=15)
