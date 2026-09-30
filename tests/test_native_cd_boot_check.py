from pathlib import Path

import pytest

from tools.native_cd_boot_check import validate_mounted_log


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "artifacts/cd-native-owner-20260906/integration/mounted-check.log"


def _synthetic_valid_log() -> str:
    """Small parser-only record; no native or media claim is made by this fixture."""
    lines = [
        "native_boot: VIDEO_SOURCE mode=0 policy=NTSC selected_before_worker=1",
        "native_boot: CPU_TRACE pc=800101ec npc=800101f0 hi=0 lo=0 regs="
        + ",".join(["00000000", "00000000", "00000001"] + ["00000000"] * 29),
    ]

    def write(address: int, value: int, cycle: int) -> None:
        lines.append(f"native_boot: CD_WRITE address={address:08x} "
                     f"value={value:02x} cycle={cycle}")

    # Bank 0 commands are separated by the four source bank-1 ACK/enable
    # receipts. The 0C command is deliberately after the 0A reset deadline.
    write(0x1F801800, 1, 100)
    write(0x1F801800, 0, 200)
    write(0x1F801803, 0, 300)
    write(0x1F801800, 0, 400)
    write(0x1F801801, 1, 1000)
    write(0x1F801800, 1, 2000)
    write(0x1F801803, 7, 4000)
    write(0x1F801802, 7, 4100)
    write(0x1F801800, 0, 5000)
    write(0x1F801801, 0x0A, 6000)
    write(0x1F801800, 1, 7000)
    write(0x1F801803, 7, 9000)
    write(0x1F801802, 7, 9100)
    write(0x1F801800, 1, 9200)
    write(0x1F801803, 7, 10000)
    write(0x1F801802, 7, 10100)
    write(0x1F801800, 0, 10200)
    write(0x1F801801, 0x0C, 4111100)
    write(0x1F801800, 1, 4111200)
    write(0x1F801803, 7, 4114000)
    write(0x1F801802, 7, 4114100)
    write(0x1F801800, 2, 4114200)
    write(0x1F801802, 0x80, 4114300)
    write(0x1F801803, 0, 4114400)
    write(0x1F801800, 3, 4114500)
    write(0x1F801801, 0x80, 4114600)
    write(0x1F801802, 0, 4114700)
    write(0x1F801803, 0x20, 4114800)

    lines.extend([
        "native_boot: SOURCE_CLOCK cycles=900000 faulted=0 "
        "scope=STARTUP_INPUT_CD_IRQ_SOURCE_ONLY devices_advanced=DEVICE_EPOCH",
        "native_boot: DEVICE_EPOCH cycles=10000000 source_edges=12 cuts=1 "
        "waits=0 frequency=1000000000 faulted=0 profile=PACED_HOST_TIME cd_advanced=1",
        "native_boot: CD_OWNER cycle=10000000 due=9999000 command=0c phase=0 "
        "if=00 enable=07 fifo=0 fault=0 irq_requests=4 "
        "common_delay_written=1 common_delay=00001325",
        "native_boot: CD_IRQ frame_busy=0 frame_faulted=0 instructions=1 "
        "call=ffffffff target=ffffffff",
        "native_boot: SPU_OWNER cycle=10000000 samples=13020 main_left=7ffe "
        "main_right=7ffe cd_left=3fff cd_right=3fff control=c001 "
        "submitted=13020 queued=1 faulted=0 profile=SETTLED_BIOS_MAIN_MUTE "
        "audible_game=NOT_OBSERVED",
        "native_boot: VIDEO_TIMING timer1_cycle=10000000 count=0001 "
        "subcycle=1 running=1 faulted=0 profile=NTSC_2146_EVENT_SERVICED "
        "gpu_parity=0 gpu_reset_sequence=0",
        "native_boot: SIO profile=DIGITAL_PORT1_CARDS_ABSENT bound=1 "
        "cycle=10000000 transmitted=2 keyboard_polls=0 ack_pending=0 faulted=0",
        "native_boot: INPUT_OWNER at=8002c8f4 installed=1 faulted=0 "
        "frame_busy=0 frame_faulted=0 timer3=0",
        "native_boot: STOP status=0 pc=8002c8f4 target=8002c8f4 "
        "IRQ_installed=1 I_STAT=0000 I_MASK=000d DPCR=33333b33 "
        "DICR=00000000 TIMER1_MODE=0507 custom_vblank=1 deliveries=4",
        "native_boot: guest guard=0001 enabled=000d slot0=80042ce8 "
        "slot2=80045290 slot3=80042e08 tick=00000001",
        "native_boot: bios_cd_installed=0 bios_event_used=8 guest_irq_enabled=1 "
        "irq_faulted=0",
        "native_boot: scheduler_removed=1",
        "native_boot: card_removed=1 bu_removed=1 input_removed=1 gpu_removed=1",
        "native_boot: OWNED_DEVICES removed=1",
        "native_boot: startup=PARTIAL incoming_ra=NATIVE_ZERO menu=NOT_REACHED",
        "native_boot: EVENT_STATE index=2 handle=f1000002 flag=00000000 "
        "class=f4000001 status=00002000 spec=00000100 mode=00001000 "
        "callback=800617f4",
    ])
    return "\n".join(lines) + "\n"


def test_synthetic_mounted_log_passes_parser_only():
    assert validate_mounted_log(_synthetic_valid_log()) == []


@pytest.mark.parametrize(
    "name,needle,replacement",
    [
        ("missing_cd_owner", "native_boot: CD_OWNER", "native_boot: CD_OWNER_REMOVED"),
        ("fault", "fault=0", "fault=1"),
        ("phase", "phase=0", "phase=1"),
        ("fifo", "fifo=0", "fifo=1"),
        ("missing_command", "command=0c", "command=00"),
        ("wrong_gain", "main_left=7ffe", "main_left=0000"),
        ("early_reset_deadline", "cycle=4111100", "cycle=4107000"),
        ("source_cpu_return_unknown", "CPU_TRACE pc=800101ec", "CPU_TRACE pc=80010200"),
        ("malformed_number", "cycles=10000000", "cycles=not-a-number"),
    ],
)
def test_synthetic_mounted_log_mutants_are_rejected(name, needle, replacement):
    valid = _synthetic_valid_log()
    assert needle in valid, name
    mutant = valid.replace(needle, replacement, 1)
    assert validate_mounted_log(mutant), name


@pytest.mark.parametrize(
    "name,needle,replacement",
    [
        ("wrong_bank", "address=1f801800 value=01 cycle=7000",
         "address=1f801800 value=00 cycle=7000"),
        ("extra_unknown_command", "address=1f801801 value=0c cycle=4111100",
         "address=1f801801 value=09 cycle=4111100"),
        ("ack_too_soon", "address=1f801803 value=07 cycle=4000",
         "address=1f801803 value=07 cycle=1500"),
        ("missing_cleanup", "native_boot: OWNED_DEVICES removed=1", ""),
        ("mismatched_cycle", "SPU_OWNER cycle=10000000", "SPU_OWNER cycle=10000001"),
        ("mismatched_sample", "samples=13020", "samples=13019"),
        ("timer1_fault", "VIDEO_TIMING timer1_cycle=10000000 count=0001 "
         "subcycle=1 running=1 faulted=0", "VIDEO_TIMING timer1_cycle=10000000 count=0001 "
         "subcycle=1 running=1 faulted=1"),
    ],
)
def test_synthetic_mounted_log_structural_negatives(name, needle, replacement):
    valid = _synthetic_valid_log()
    assert needle in valid, name
    assert validate_mounted_log(valid.replace(needle, replacement, 1)), name


def test_preserved_live_log_is_optional_until_teardown_marker_exists():
    if not FIXTURE.exists():
        pytest.skip("preserved mounted native log is unavailable")
    log = FIXTURE.read_text(encoding="utf-8")
    if "OWNED_DEVICES removed=1" not in log:
        pytest.skip("preserved log predates the checked owned-device teardown marker")
    assert validate_mounted_log(log) == []
