import hashlib
import json
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.check_gpu_dma_native import (
    validate_gpu_dma_artifacts,
    validate_gpu_dma_log,
)


def valid_log() -> str:
    return """\
native_boot: DRAW_SOURCE pc=800525dc a0=00000140 a1=000000f0 a2=00000004 a3=00000000 sp=801fff70 ra=80014650 v0=00000000 stack10=00000000 readable=1 callback_guard=1 gpu_reset_sequence=0
native_boot: DRAW_SOURCE pc=800526a8 a0=800727f4 a1=ffffffff a2=00000014 a3=00000010 sp=801fff18 ra=800526a8 v0=00000000 stack10=000000f0 readable=1 callback_guard=1 gpu_reset_sequence=1
native_boot: DRAW_SOURCE pc=800526fc a0=800727f4 a1=ffffffff a2=00000014 a3=00000010 sp=801fff18 ra=800526a8 v0=800a6440 stack10=000000f0 readable=1 callback_guard=1 gpu_reset_sequence=1
native_boot: DRAW_PACKET address=800a6454 tag=06ffffff words=7 valid=1 data=06ffffff,e3000000,e4000000,e5000000,e1000000,e2000000,e6000000
native_boot: DRAW_DEVICE pc=8005b870 address=1f8010a8 a0=00000000 v1=00000000
native_boot: DRAW_DEVICE pc=8005b720 address=1f801814 a0=800a6454 v1=04000002
native_boot: DRAW_DEVICE pc=8005b730 address=1f8010a0 a0=800a6454 v1=04000002
native_boot: GPU_DMA2 stage=START readable=1 cycle=1000 due=1008 starts=1 transfers=0 fetched=6 accepted=6 start_madr=000a6454 madr=00ffffff bcr=00000000 chcr=01000401 nodes=1 headers=1 payload=6 accepted_in_transfer=6 fault=0 fault_node=00000000 fault_word=0 timing=REFERENCE_SYNCHRONOUS_SUBMIT_DEFERRED_COMPLETION hardware_parity=UNPROVEN
native_boot: GPU_DMA2 stage=COMPLETE readable=1 cycle=1008 due=1008 starts=1 transfers=1 fetched=6 accepted=6 start_madr=000a6454 madr=00ffffff bcr=00000000 chcr=00000401 nodes=1 headers=1 payload=6 accepted_in_transfer=6 fault=0 fault_node=00000000 fault_word=0 timing=REFERENCE_SYNCHRONOUS_SUBMIT_DEFERRED_COMPLETION hardware_parity=UNPROVEN
native_boot: GPU_DMA2 stage=SOURCE_QUEUE_RETURN readable=1 cycle=1010 due=1008 starts=1 transfers=1 fetched=6 accepted=6 start_madr=000a6454 madr=00ffffff bcr=00000000 chcr=00000401 nodes=1 headers=1 payload=6 accepted_in_transfer=6 fault=0 fault_node=00000000 fault_word=0 timing=REFERENCE_SYNCHRONOUS_SUBMIT_DEFERRED_COMPLETION hardware_parity=UNPROVEN
native_boot: DRAW_QUEUE_RETURN pc=80059dfc v0=00000000 sp=801ffef8 ra=80059dfc I_MASK=000d
native_boot: DISPLAY_GP1 pc=8005b690 word=05000000 address=1f801814 sp=801ffef0 ra=8005a088 cache_valid=1 cache_before=00
native_boot: DISPLAY_CACHE opcode=05 address=80078879 valid=1 value=00
native_boot: EXECUTION_BOUNDARY reason=REFUSED
native_boot: CPU_BOUNDARY pc=8005b690 npc=8005b694 v0=1f801814 a0=05000000 a1=00000000 sp=801ffef0 ra=8005a088
native_boot: SOURCE_CLOCK cycles=900 faulted=0 devices_advanced=DEVICE_EPOCH
native_boot: DEVICE_EPOCH cycles=1010 source_edges=3 cuts=10 waits=0 frequency=1000000000 faulted=0 profile=PACED_HOST_TIME cd_advanced=1
native_boot: CD_OWNER cycle=1010 fault=0
native_boot: CD_IRQ frame_busy=0 frame_faulted=0
native_boot: SPU_OWNER cycle=1010 samples=1 faulted=0
native_boot: VIDEO_TIMING timer1_cycle=1010 count=1 subcycle=1 running=1 faulted=0 profile=NTSC_2146_EVENT_SERVICED gpu_parity=0
native_boot: DRAW_OWNER dma_direction=2 chcr=00000401 display_disabled=1 gpu_faulted=0 queue_valid=1 busy=00000001 saved_mask=0000000d history=8005b710,800a6454,00000000
native_boot: SIO profile=DIGITAL bound=1 cycle=1010 faulted=0
native_boot: INPUT_OWNER at=8005b690 installed=1 faulted=0 frame_busy=0 frame_faulted=0 timer3=0
native_boot: STOP status=0 pc=8005b690 target=8005b690 IRQ_installed=1 I_STAT=0000 I_MASK=000d DPCR=333bbb33 DICR=00900000 irq_faulted=0
native_boot: irq_faulted=0
native_boot: GPUSTAT=56802000 GPUREAD=00000400 DMA2_CHCR=00000401 valid=1
native_boot: GPU_ENV window=00000000 area_start=00000000 area_end=00000000 offset=00000000 mask=0 gp0_accepted=6 faulted=0
native_boot: GPU_ENV_BACKEND window=00000000 area_start=00000000 area_end=00000000 offset=00000000 mask=0 corners=0,0,0,0 faulted=0
native_boot: GPU_DMA2 stage=STOP readable=1 cycle=1010 due=1008 starts=1 transfers=1 fetched=6 accepted=6 start_madr=000a6454 madr=00ffffff bcr=00000000 chcr=00000401 nodes=1 headers=1 payload=6 accepted_in_transfer=6 fault=0 fault_node=00000000 fault_word=0 timing=REFERENCE_SYNCHRONOUS_SUBMIT_DEFERRED_COMPLETION hardware_parity=UNPROVEN
native_boot: GPU_DMA2 removed=1
native_boot: scheduler_removed=1 card_removed=1 bu_removed=1 input_removed=1
native_boot: graphics_init=RETURNED blank_presented=1 gpu_removed=1
native_boot: OWNED_DEVICES removed=1
native_boot: startup=PARTIAL incoming_ra=NATIVE_ZERO menu=NOT_REACHED
"""


def mutate(text: str, old: str, new: str) -> str:
    assert old in text, old
    return text.replace(old, new, 1)


def test_synthetic_final_gpu_dma_capture_passes():
    assert validate_gpu_dma_log(valid_log()) == []


@pytest.mark.parametrize(
    "old,new",
    [
        ("stage=COMPLETE", "stage=COMPLETE_MISSING"),
        ("accepted=6 start_madr", "accepted=5 start_madr"),
        ("headers=1 payload=6", "headers=2 payload=6"),
        ("due=1008 starts=1", "due=1007 starts=1"),
        ("stage=COMPLETE readable=1 cycle=1008", "stage=COMPLETE readable=1 cycle=1007"),
        ("saved_mask=0000000d", "saved_mask=0000000c"),
        ("history=8005b710,800a6454,00000000", "history=8005b710,800a6454,00000001"),
        ("a0=05000000 a1", "a0=06000000 a1"),
        ("gp0_accepted=6", "gp0_accepted=5"),
        ("GPU_ENV window=00000000 area_start=00000000 area_end=00000000 offset=00000000 mask=0 gp0_accepted=6 faulted=0",
         "GPU_ENV window=00000000 area_start=00000000 area_end=00000000 offset=00000000 mask=0 gp0_accepted=6 faulted=1"),
        ("gpu_faulted=0", "gpu_faulted=1"),
        ("gpu_removed=1\n", "gpu_removed=0\n"),
        ("GPU_DMA2 removed=1", "GPU_DMA2 removed=0"),
        ("menu=NOT_REACHED", "menu=REACHED"),
    ],
)
def test_capture_mutations_are_rejected(old, new):
    assert validate_gpu_dma_log(mutate(valid_log(), old, new))


@pytest.mark.parametrize(
    "old,new",
    [
        ("pc=8005b690 npc=8005b694", "pc=8005b690 npc=8005b698"),
        ("a1=00000000 sp=801ffef0 ra=8005a088", "a1=00000000 sp=801ffef4 ra=8005a088"),
        ("pc=80059dfc v0=00000000 sp=801ffef8 ra=80059dfc",
         "pc=80059df8 v0=00000000 sp=801ffef8 ra=80059dfc"),
        ("pc=80059dfc v0=00000000 sp=801ffef8 ra=80059dfc",
         "pc=80059dfc v0=00000000 sp=801ffef8 ra=80059df8"),
        ("pc=8005b730 address=1f8010a0 a0=800a6454",
         "pc=8005b730 address=1f8010a0 a0=800a6455"),
        ("stage=SOURCE_QUEUE_RETURN readable=1 cycle=1010 due=1008",
         "stage=SOURCE_QUEUE_RETURN readable=1 cycle=1007 due=1008"),
        ("stage=COMPLETE readable=1 cycle=1008 due=1008 starts=1 transfers=1 fetched=6 accepted=6 start_madr=000a6454 madr=00ffffff bcr=00000000 chcr=00000401 nodes=1",
         "stage=COMPLETE readable=1 cycle=1008 due=1008 starts=1 transfers=1 fetched=6 accepted=6 start_madr=000a6454 madr=00ffffff bcr=00000000 chcr=00000401 nodes=2"),
        ("hardware_parity=UNPROVEN\n",
         "hardware_parity=PROVEN\n"),
        ("I_MASK=000d DPCR=333bbb33 DICR=00900000",
         "I_MASK=000c DPCR=333bbb33 DICR=00900000"),
        ("I_MASK=000d DPCR=333bbb33 DICR=00900000",
         "I_MASK=000d DPCR=333bbb32 DICR=00900000"),
        ("I_MASK=000d DPCR=333bbb33 DICR=00900000",
         "I_MASK=000d DPCR=333bbb33 DICR=00800000"),
        ("CD_IRQ frame_busy=0 frame_faulted=0",
         "CD_IRQ frame_busy=0 frame_faulted=1"),
        ("INPUT_OWNER at=8005b690 installed=1 faulted=0",
         "INPUT_OWNER at=8005b690 installed=0 faulted=0"),
        ("GPUSTAT=56802000", "GPUSTAT=56002000"),
        ("GPU_DMA2 stage=STOP readable=1 cycle=1010",
         "GPU_DMA2 stage=STOP readable=1 cycle=1011"),
    ],
)
def test_capture_boundary_and_owner_mutations_are_rejected(old, new):
    assert validate_gpu_dma_log(mutate(valid_log(), old, new))


def test_duplicate_cd_irq_and_source_device_rows_are_rejected():
    text = valid_log()
    irq = next(line for line in text.splitlines(True) if "CD_IRQ " in line)
    assert validate_gpu_dma_log(text + irq)
    device = next(line for line in text.splitlines(True) if "pc=8005b730" in line)
    assert validate_gpu_dma_log(text + device)


@pytest.mark.parametrize(
    "extra",
    [
        "native_boot: irq_faulted=1\n",
        "native_boot: frame_busy=1 frame_faulted=0\n",
        "native_boot: menu=REACHED\n",
    ],
)
def test_appended_conflicting_health_claims_are_rejected(extra):
    assert validate_gpu_dma_log(valid_log() + extra)


def test_cd_irq_missing_busy_field_is_rejected():
    assert validate_gpu_dma_log(
        mutate(valid_log(), "CD_IRQ frame_busy=0 frame_faulted=0", "CD_IRQ frame_faulted=0")
    )


def test_premature_completion_and_duplicate_stage_are_rejected():
    premature = mutate(valid_log(), "stage=COMPLETE readable=1 cycle=1008",
                       "stage=COMPLETE readable=1 cycle=1007")
    assert validate_gpu_dma_log(premature)
    duplicate = valid_log() + next(
        line for line in valid_log().splitlines(True) if "stage=STOP" in line)
    assert validate_gpu_dma_log(duplicate)


def test_fake_gp1_command_cache_and_malformed_decimal_are_rejected():
    fake_cache = valid_log() + "native_boot: GPU_COMMAND_CACHE gp1=05000000\n"
    assert validate_gpu_dma_log(fake_cache)
    malformed = mutate(valid_log(), "cycle=1010 due=1008 starts=1",
                       "cycle=10x due=1008 starts=1")
    assert validate_gpu_dma_log(malformed)


@pytest.mark.parametrize(
    "mutator",
    [
        lambda text: text.replace("native_boot: DISPLAY_CACHE opcode=05 address=80078879 valid=1 value=00\n", "", 1),
        lambda text: text.replace("value=00\n", "value=01\n", 1),
        lambda text: text + "native_boot: DISPLAY_CACHE opcode=05 address=80078879 valid=1 value=00\n",
    ],
)
def test_gp1_cache_transition_is_required_and_unique(mutator):
    assert validate_gpu_dma_log(mutator(valid_log()))


def test_healthy_phase_variation_does_not_require_vblank_count():
    varied = valid_log().replace("source_edges=3 cuts=10", "source_edges=97 cuts=11")
    varied = varied.replace("gpu_parity=0", "gpu_parity=1")
    assert validate_gpu_dma_log(varied) == []


def test_artifact_hashes_and_metadata_are_checked(tmp_path):
    log = tmp_path / "gpu-display.log"
    binary = tmp_path / "gpu-display-native-boot"
    run = tmp_path / "gpu-display-run.json"
    log.write_text(valid_log(), encoding="utf-8")
    binary.write_bytes(b"synthetic native binary")
    run.write_text(json.dumps({
        "exit": 2,
        "menu": "NOT_REACHED",
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
    }), encoding="utf-8")
    assert validate_gpu_dma_artifacts(tmp_path) == []
    run.write_text(run.read_text(encoding="utf-8").replace('"exit": 2', '"exit": 0'),
                   encoding="utf-8")
    assert validate_gpu_dma_artifacts(tmp_path)


def test_checker_cli_states_nonclaims(tmp_path):
    log = tmp_path / "gpu-display.log"
    binary = tmp_path / "gpu-display-native-boot"
    run = tmp_path / "gpu-display-run.json"
    log.write_text(valid_log(), encoding="utf-8")
    binary.write_bytes(b"synthetic native binary")
    run.write_text(json.dumps({
        "exit": 2,
        "menu": "NOT_REACHED",
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
    }), encoding="utf-8")
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools/check_gpu_dma_native.py"), str(tmp_path)],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "menu=NOT_REACHED" in result.stdout
    assert "render=NOT_CLAIMED" in result.stdout
    assert "hardware_parity=UNPROVEN" in result.stdout


def test_retained_final_gpu_display_capture_if_available():
    candidates = [
        ROOT / "artifacts/spu-startup-20260906/native",
        ROOT / "artifacts/spu-startup-20260906/native/gpu-display-final",
    ]
    available = [path for path in candidates if (path / "gpu-display.log").exists()]
    if not available:
        pytest.skip("final gpu-display native capture is not retained yet")
    for artifact_dir in available:
        assert validate_gpu_dma_artifacts(artifact_dir) == []
