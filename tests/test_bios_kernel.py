"""Native BIOS CD ownership and cleanup through the shared kernel services."""
from pathlib import Path
import hashlib
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_bios_cd_resource_lifecycle():
    with tempfile.TemporaryDirectory(prefix="musashi-bios-kernel-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/bios_kernel_probe.c"),
            str(ROOT / "pc_port/bios_kernel.c"),
            str(ROOT / "pc_port/bios_events.c"), "-o", str(probe),
        ], check=True, timeout=30)
        run = [str(probe)]
        fixture = ROOT / "artifacts/bios-remove-20260905/retail-run3"
        snapshots = [fixture / "before.ram", fixture / "after.ram"]
        if all(path.exists() for path in snapshots):
            hashes = ["2516dfa6a77d505e088210d1fe02e52cb5f087fc483bc8ae74c892cde2f7467b",
                      "c1838bc592abe991577ef93dd5d29cd9f5e366d941d91a96c26ba08d8a6fff3a"]
            for path, expected in zip(snapshots, hashes):
                assert hashlib.sha256(path.read_bytes()).hexdigest() == expected
            run += [str(path) for path in snapshots]
        subprocess.run(run, check=True, timeout=10)


def test_bios_kernel_handler_authority_and_live_mutation():
    with tempfile.TemporaryDirectory(prefix="musashi-bios-kernel-handler-") as temp:
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/bios_kernel_handler_probe.c"),
            str(ROOT / "pc_port/bios_kernel.c"),
            str(ROOT / "pc_port/bios_events.c"), "-o", str(probe),
        ], check=True, timeout=30)
        subprocess.run([str(probe)], check=True, timeout=10)
