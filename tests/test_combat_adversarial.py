"""Adversarial stress-test suite for Musashi combat, edge-pulsing, hitbox pool, and enemy state machines."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_combat_adversarial_probe():
    with tempfile.TemporaryDirectory(prefix="musashi-combat-adv-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/combat_adversarial_probe.c"),
            str(ROOT / "pc_port/sio_controller.c"),
            str(ROOT / "pc_port/boot_memory.c"),
            "-lcrypto", "-lm", "-o", str(probe),
        ], check=True, timeout=30)
        completed = subprocess.run([str(probe)], capture_output=True, text=True, check=True, timeout=10)
        assert "ALL ADVERSARIAL COMBAT PROBE TESTS PASSED CLEANLY." in completed.stdout
        assert "PASS: test_sio_rapid_edge_pulsing" in completed.stdout
        assert "PASS: test_combat_state_machine_and_edge_latches" in completed.stdout
        assert "PASS: test_hitbox_pool_bounds_and_lifetimes" in completed.stdout
        assert "PASS: test_enemy_damage_and_death_handling" in completed.stdout
