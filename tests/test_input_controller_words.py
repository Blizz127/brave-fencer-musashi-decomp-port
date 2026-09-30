"""Compiled source-word tests for the bounded controller callback stage."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent))
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]


def test_input_controller_words_bound_stage_and_scratch_ownership():
    with tempfile.TemporaryDirectory(prefix="musashi-input-controller-") as temp:
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
            str(ROOT / "tests/input_controller_words_probe.c"),
            str(ROOT / "pc_port/boot_memory.c"), "-lcrypto", "-o", str(probe),
        ], check=True, timeout=30)
        _sync_code_image(generated)
        subprocess.run([str(probe)], check=True, timeout=10)
