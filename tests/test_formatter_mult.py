from pathlib import Path
import subprocess
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]


def test_mult_hilo_and_retail_bcd(tmp_path):
    generated = tmp_path / "generated"
    generated.mkdir()
    _generate_formatter_includes(generated)
    probe = tmp_path / "probe"
    subprocess.run([
        "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-parentheses",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"), "-I", str(generated),
        str(ROOT / "tests/formatter_mult_probe.c"), str(ROOT / "pc_port/boot_memory.c"),
        "-o", str(probe),
    ], check=True, timeout=30)
    _sync_code_image(generated)
    run = subprocess.run([str(probe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout == "MULT_HILO_AND_RETAIL_BCD_PASS\n"
