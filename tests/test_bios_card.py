"""Bounded native ownership for the selected BIOS InitCARD prefix."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_bios_card_init_and_early_hook_contract():
    with tempfile.TemporaryDirectory(prefix="musashi-bios-card-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/bios_card_probe.c"),
            str(ROOT / "pc_port/bios_card.c"),
            str(ROOT / "pc_port/bios_kernel.c"),
            str(ROOT / "pc_port/bios_events.c"),
            "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe)], check=True, timeout=10)


def test_startcard_order_ownership_and_maintenance():
    with tempfile.TemporaryDirectory(prefix="musashi-bios-card-start-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/bios_card_start_probe.c"),
            str(ROOT / "pc_port/bios_card.c"),
            str(ROOT / "pc_port/bios_kernel.c"),
            str(ROOT / "pc_port/bios_events.c"), "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe)], check=True, timeout=10)
