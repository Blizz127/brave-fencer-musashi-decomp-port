"""Input list ownership; executor/MMIO fixture is not hardware parity proof."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_input_descriptor_ownership_and_checked_execution():
    with tempfile.TemporaryDirectory(prefix="musashi-bios-input-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-ffunction-sections", "-Wl,--gc-sections",
            "-I", str(ROOT / "pc_port/include"),
            *[str(ROOT / name) for name in (
                "tests/bios_input_probe.c", "pc_port/bios_input.c",
                "pc_port/boot_memory.c", "pc_port/bios_card.c",
                "pc_port/bios_kernel.c", "pc_port/bios_events.c")],
            "-lcrypto", "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe)], check=True, timeout=10)
