from pathlib import Path
import json
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.check_draw_mmio_native import (
    validate_draw_mmio_artifacts,
    validate_draw_mmio_log,
)


ARTIFACTS = ROOT / "artifacts/spu-startup-20260906/native"
LOG = ARTIFACTS / "draw-mmio.log"


def _retained_log() -> str:
    assert LOG.is_file()
    return LOG.read_text(encoding="utf-8")


def _replace_in_line(text: str, marker: str, old: str, new: str) -> str:
    lines = text.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if marker in line and old in line:
            lines[index] = line.replace(old, new, 1)
            return "".join(lines)
    raise AssertionError(f"mutation needle not found: {marker} {old}")


def test_retained_draw_mmio_checkpoint_passes_and_artifact_hashes_match():
    assert validate_draw_mmio_log(_retained_log()) == []
    assert validate_draw_mmio_artifacts(ARTIFACTS) == []


@pytest.mark.parametrize(
    "name,marker,old,new",
    [
        ("missing_packet", "DRAW_PACKET ", "native_boot: DRAW_PACKET ", "native_boot: DRAW_PACKET_REMOVED "),
        ("wrong_draw_width", "DRAW_SOURCE pc=800525dc", "a0=00000140", "a0=00000141"),
        ("wrong_reset_v0", "DRAW_SOURCE pc=800526a8", "v0=00000000", "v0=00000001"),
        ("wrong_gp1", "DRAW_DEVICE pc=8005b720", "address=1f801814", "address=1f801810"),
        ("wrong_madr", "DRAW_DEVICE pc=8005b730", "address=1f8010a0", "address=1f8010a4"),
        ("wrong_direction", "DRAW_OWNER ", "dma_direction=2", "dma_direction=1"),
        ("wrong_chcr", "DRAW_OWNER ", "chcr=00000401", "chcr=00000400"),
        ("queue_not_busy", "DRAW_OWNER ", "busy=00000001", "busy=00000000"),
        ("wrong_saved_mask", "DRAW_OWNER ", "saved_mask=0000000d", "saved_mask=0000000c"),
        ("wrong_history", "DRAW_OWNER ", "history=00000000,00000000,00000000", "history=00000000,00000001,00000000"),
        ("gpu_fault", "DRAW_OWNER ", "gpu_faulted=0", "gpu_faulted=1"),
        ("wrong_stop_target", "STOP ", "target=8005b730", "target=8005b734"),
        ("missing_cleanup", "OWNED_DEVICES ", "OWNED_DEVICES removed=1", "OWNED_DEVICES removed=0"),
    ],
)
def test_draw_mmio_mutations_are_rejected(name, marker, old, new):
    mutated = _replace_in_line(_retained_log(), marker, old, new)
    assert validate_draw_mmio_log(mutated), name


def test_phase_and_decimal_epoch_variations_are_accepted():
    varied = _retained_log().replace("GPUSTAT=56802000", "GPUSTAT=d6802000", 1)
    varied = varied.replace("gpu_reset_sequence=34", "gpu_reset_sequence=35")
    varied = varied.replace("23372239", "24000000")
    assert validate_draw_mmio_log(varied) == []


@pytest.mark.parametrize("mutator", [
    lambda text: _replace_in_line(
        text, "DRAW_SOURCE pc=800525dc", "stack10=00000000", "stack10=00000001"),
    lambda text: _replace_in_line(
        text, "CPU_BOUNDARY ", "ra=8005b8c4", "ra=8005b8c0"),
    lambda text: _replace_in_line(
        text, "DEVICE_EPOCH ", "cycles=23372239", "cycles=23x72239"),
    lambda text: text + next(
        line for line in text.splitlines(True) if "DRAW_OWNER " in line),
    lambda text: text + "native_boot: scheduler_removed=1\n",
])
def test_draw_mmio_structural_mutations_are_rejected(mutator):
    assert validate_draw_mmio_log(mutator(_retained_log()))


def test_source_packet_device_stop_order_mutation_is_rejected():
    lines = _retained_log().splitlines(keepends=True)
    source = next(i for i, line in enumerate(lines) if "DRAW_SOURCE pc=800525dc" in line)
    packet = next(i for i, line in enumerate(lines) if "DRAW_PACKET " in line)
    lines[source], lines[packet] = lines[packet], lines[source]
    assert validate_draw_mmio_log("".join(lines))


def test_run_metadata_hash_mutations_are_rejected(tmp_path):
    for name in ("draw-mmio.log", "draw-mmio-run.json", "draw-mmio-native-boot"):
        shutil.copy2(ARTIFACTS / name, tmp_path / name)
    metadata_path = tmp_path / "draw-mmio-run.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    metadata["exit"] = 0
    metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
    assert validate_draw_mmio_artifacts(tmp_path)


def test_binary_hash_mutation_is_rejected(tmp_path):
    for name in ("draw-mmio.log", "draw-mmio-run.json", "draw-mmio-native-boot"):
        shutil.copy2(ARTIFACTS / name, tmp_path / name)
    binary = tmp_path / "draw-mmio-native-boot"
    data = bytearray(binary.read_bytes())
    data[0] ^= 0x01
    binary.write_bytes(data)
    assert validate_draw_mmio_artifacts(tmp_path)


def test_checker_cli_reports_explicit_nonclaims():
    script = ROOT / "tools/check_draw_mmio_native.py"
    run = subprocess.run(
        [sys.executable, str(script), str(ARTIFACTS)],
        cwd=ROOT / "tests",
        capture_output=True,
        text=True,
        check=False,
        timeout=10,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "menu=NOT_REACHED" in run.stdout
    assert "transfer=NOT_STARTED" in run.stdout
    assert "render_parity=NOT_CLAIMED" in run.stdout
    assert "retail_parity=NOT_CLAIMED" in run.stdout
