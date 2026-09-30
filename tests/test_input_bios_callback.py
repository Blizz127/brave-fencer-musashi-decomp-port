from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image


def test_input_bios_callback_bounded_verifier_handler_and_refusals():
    with tempfile.TemporaryDirectory(prefix="musashi-input-bios-callback-") as temp:
        root = Path(temp)
        generated = root / "generated"
        generated.mkdir()
        _generate_formatter_includes(generated)
        probe = root / "probe"
        subprocess.run([
            "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
            "-Wno-parentheses", "-ffunction-sections", "-fdata-sections",
            "-Wl,--gc-sections", "-I", str(ROOT / "include"),
            "-I", str(ROOT / "pc_port/include"), "-I", str(generated),
            str(ROOT / "tests/input_bios_callback_probe.c"),
            str(ROOT / "pc_port/boot_memory.c"),
            str(ROOT / "pc_port/mips_formatter.c"), "-lcrypto",
            "-o", str(probe),
        ], check=True, timeout=30)
        _sync_code_image(generated)
        subprocess.run([str(probe)], check=True, timeout=10)
