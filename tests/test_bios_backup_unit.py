"""Selected no-card BIOS state machine with an explicit fake MMIO fixture."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_backup_unit_requires_real_card_timeout_completion():
    with tempfile.TemporaryDirectory(prefix="musashi-bu-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"),
            *[str(ROOT / name) for name in (
                "tests/bios_backup_unit_probe.c", "pc_port/bios_backup_unit.c",
                "pc_port/bios_card.c", "pc_port/bios_kernel.c",
                "pc_port/bios_events.c")], "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe)], check=True, timeout=10)
