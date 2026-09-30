from pathlib import Path

import pytest

from tools.native_spu_boot_check import validate_spu_boot_log


ROOT = Path(__file__).resolve().parents[1]
LIVE_FIXTURE = ROOT / "artifacts/spu-startup-20260906/native/integration.log"


def _synthetic_valid_log() -> str:
    lines = [
        "native_boot: CPU_TRACE pc=800101ec cycle=100 regs="
        + ",".join(["00000000", "00000000", "00000001"] + ["00000000"] * 29),
        "native_boot: CPU_TRACE pc=8002c8f4 cycle=200",
        "native_boot: CPU_TRACE pc=8003a424 cycle=300",
        "native_boot: SPU_STAGE pc=8002c8f4 cycle=350 samples=0 "
        "main_left=7ffe main_right=7ffe control=c001 manual_halfwords=0 "
        "cursor=00000 transfer_address=0000 transfer_control=0000 "
        "event_handle=00000000 event_guard=00000000 faulted=0 "
        "manual_policy=SYNCHRONOUS_SOURCE_MODEL key_policy=POST_FRAME "
        "hardware_subphase=UNPROVEN",
    ]

    def write(address: int, value: int, cycle: int) -> None:
        lines.append(f"native_boot: SPU_WRITE address={address:08x} "
                     f"value={value:04x} cycle={cycle}")

    cycle = 400
    for address, value in [(0x1F801DA6, 0x0200)] + [(0x1F801DA8, 0x0707)] * 8:
        write(address, value, cycle)
        cycle += 1
    lines.extend([
        "native_boot: DMA_REGISTER channel=4 callback=8003aa18 "
        "target=80042f8c previous=00000000",
        "native_boot: EVENT_OPEN class=f0000009 spec=00000020 mode=00002000 "
        "callback=00000000 handle=f1000008",
        "native_boot: EVENT_ENABLE handle=f1000008 result=1",
        "native_boot: CPU_TRACE pc=8002c904 cycle=600",
        "native_boot: SPU_STAGE pc=8002c904 cycle=650 samples=0 "
        "main_left=0000 main_right=0000 control=c000 manual_halfwords=8 "
        "cursor=01010 transfer_address=0200 transfer_control=0004 "
        "event_handle=f1000008 event_guard=00000001 faulted=0 "
        "manual_policy=SYNCHRONOUS_SOURCE_MODEL key_policy=POST_FRAME "
        "hardware_subphase=UNPROVEN",
    ])
    for address, value in [(0x1F801DA6, 0x0200)] + [(0x1F801DA8, 0x0707)] * 8:
        write(address, value, cycle)
        cycle += 1
    lines.extend([
        "native_boot: CPU_TRACE pc=8003a424 cycle=750",
        "native_boot: CPU_TRACE pc=8003d530 cycle=800",
        "native_boot: SPU_STAGE pc=8002c90c cycle=900 samples=1 "
        "main_left=7ffe main_right=7ffe control=c000 manual_halfwords=16 "
        "cursor=01010 transfer_address=0200 transfer_control=0004 "
        "event_handle=f1000008 event_guard=00000001 faulted=0 "
        "manual_policy=SYNCHRONOUS_SOURCE_MODEL key_policy=POST_FRAME "
        "hardware_subphase=UNPROVEN",
        "native_boot: SPU_RAM pc=8002c90c offset=00001000 valid=1 "
        "bytes=" + "07" * 16,
    ])
    regs = "0000,0000,1000,0200,80ff,4000,0000,0000"
    for index in range(24):
        lines.append(f"native_boot: SPU_VOICE index={index} phase=0 envelope=0 "
                     "cursor=01000 repeat=00000 blocks=1 decoded=1 "
                     "pending_on=0 pending_off=0 endx=1 "
                     f"regs={regs}")
    lines.extend([
        "native_boot: CPU_TRACE pc=8002c90c cycle=1000",
        "native_boot: SOURCE_CLOCK cycles=900 faulted=0",
        "native_boot: DEVICE_EPOCH cycles=1000 source_edges=1 faulted=0",
        "native_boot: CD_OWNER cycle=1000 fault=0",
        "native_boot: CD_IRQ frame_busy=0 frame_faulted=0 instructions=1 "
        "call=ffffffff target=ffffffff",
        "native_boot: SPU_OWNER cycle=1000 samples=1 main_left=7ffe "
        "main_right=7ffe cd_left=0000 cd_right=0000 control=c000 "
        "submitted=1 faulted=0",
        "native_boot: VIDEO_TIMING timer1_cycle=1000 running=1 faulted=0",
        "native_boot: SIO profile=DIGITAL_PORT1_CARDS_ABSENT bound=1 "
        "cycle=1000 faulted=0",
        "native_boot: INPUT_OWNER installed=1 faulted=0 frame_busy=0 frame_faulted=0",
        "native_boot: STOP status=0 pc=8002c90c target=8002c90c",
        "native_boot: scheduler_removed=1",
        "native_boot: card_removed=1 bu_removed=1",
        "native_boot: input_removed=1",
        "native_boot: gpu_removed=1",
        "native_boot: OWNED_DEVICES removed=1",
    ])
    return "\n".join(lines) + "\n"


def test_synthetic_spu_startup_passes_parser_only():
    assert validate_spu_boot_log(_synthetic_valid_log()) == []


@pytest.mark.parametrize(
    "name,needle,replacement",
    [
        ("wrong_milestone", "pc=8003d530", "pc=8003d531"),
        ("late_cd_checkpoint", "pc=800101ec", "pc=800101ee"),
        ("missing_dma_registration", "native_boot: DMA_REGISTER channel",
         "native_boot: DMA_REGISTER_REMOVED channel"),
        ("duplicate_event", "EVENT_ENABLE handle=f1000008 result=1",
         "EVENT_ENABLE handle=f1000008 result=1\nnative_boot: EVENT_ENABLE handle=f1000008 result=1"),
        ("bad_manual_count", "manual_halfwords=16", "manual_halfwords=15"),
        ("bad_ram_upload", "bytes=" + "07" * 16, "bytes=" + "00" * 16),
        ("bad_voice_envelope", "envelope=0", "envelope=1"),
        ("mismatched_epoch", "SPU_OWNER cycle=1000", "SPU_OWNER cycle=1001"),
        ("missing_cleanup", "OWNED_DEVICES removed=1", "OWNED_DEVICES removed=0"),
        ("wrong_boundary", "target=8002c90c", "target=8002c8f4"),
        ("malformed_cycle", "DEVICE_EPOCH cycles=1000", "DEVICE_EPOCH cycles=bad"),
    ],
)
def test_synthetic_spu_mutants_are_rejected(name, needle, replacement):
    valid = _synthetic_valid_log()
    assert needle in valid, name
    assert validate_spu_boot_log(valid.replace(needle, replacement, 1)), name


def test_optional_native_integration_log_is_checked_when_present():
    if not LIVE_FIXTURE.exists():
        pytest.skip("optional native SPU integration log is unavailable")
    assert validate_spu_boot_log(LIVE_FIXTURE.read_text(encoding="utf-8")) == []


def test_direct_cli_starts_outside_repo_without_pythonpath(tmp_path):
    import os
    import subprocess
    import sys
    env = os.environ.copy()
    env.pop("PYTHONPATH", None)
    script = Path(__file__).resolve().parents[1] / "tools/native_spu_boot_check.py"
    run = subprocess.run([sys.executable, str(script)], cwd=tmp_path,
                         env=env, capture_output=True, text=True, timeout=10)
    assert run.returncode == 2
    assert "usage: native_spu_boot_check.py" in run.stderr
    assert "Traceback" not in run.stderr


@pytest.mark.parametrize("needle,replacement", [
    ("CD_OWNER cycle=1000 fault=0", "CD_OWNER cycle=1000 fault=1"),
    ("CD_OWNER cycle=1000", "CD_OWNER_REMOVED cycle=1000"),
    ("SIO profile=", "SIO_REMOVED profile="),
    ("VIDEO_TIMING timer1_cycle=1000", "VIDEO_TIMING timer1_cycle=999"),
    ("VIDEO_TIMING timer1_cycle=1000 running=1 faulted=0", "VIDEO_TIMING timer1_cycle=1000 running=1 faulted=1"),
    ("INPUT_OWNER installed=1 faulted=0", "INPUT_OWNER installed=1 faulted=1"),
    ("CD_IRQ frame_busy=0", "CD_IRQ frame_busy=1"),
    ("samples=1", "samples=0"),
    ("submitted=1", "submitted=0"),
    ("cd_left=0000", "cd_left=3fff"),
    ("blocks=1", "blocks=0"),
    ("decoded=1", "decoded=0"),
    ("phase=0", "phase=1"),
    ("pending_on=0", "pending_on=1"),
    ("SPU_VOICE index=0", "SPU_VOICE index=bad"),
    ("gpu_removed=1", "gpu_removed=0"),
    ("scheduler_removed=1", "scheduler_removed=0"),
])
def test_owner_and_voice_failures_rejected(needle, replacement):
    text = _synthetic_valid_log()
    assert needle in text
    assert validate_spu_boot_log(text.replace(needle, replacement, 1))


def test_duplicate_stage_and_missing_epoch_do_not_crash_parser():
    text = _synthetic_valid_log()
    stage = next(line for line in text.splitlines() if "SPU_STAGE pc=8002c90c" in line)
    assert validate_spu_boot_log(text + stage + "\n")
    assert validate_spu_boot_log("\n".join(line for line in text.splitlines()
                                           if "DEVICE_EPOCH" not in line))


def _synthetic_common_log():
    text = _synthetic_valid_log()
    extra = ["native_boot: CPU_TRACE pc=8003c598 cycle=1001"]
    writes = [(0x1F801D80, 0x3FFF), (0x1F801D82, 0x3FFF),
              (0x1F801DB0, 0x3FFF), (0x1F801DB2, 0x3FFF),
              (0x1F801DAA, 0xC000), (0x1F801DAA, 0xC001), (0x1F801DAA, 0xC001)]
    for index, (address, value) in enumerate(writes):
        extra.append(f"native_boot: SPU_WRITE address={address:08x} value={value:04x} cycle={1002 + index}")
    extra.extend([
        "native_boot: CPU_TRACE pc=8002c974 cycle=1010",
        "native_boot: SPU_STAGE pc=8002c974 cycle=1010 samples=1 "
        "main_left=7ffe main_right=7ffe control=c001 manual_halfwords=16 "
        "cursor=01010 transfer_address=0200 transfer_control=0004 "
        "event_handle=f1000008 event_guard=00000001 faulted=0 "
        "manual_policy=SYNCHRONOUS_SOURCE_MODEL key_policy=POST_FRAME hardware_subphase=UNPROVEN",
        "native_boot: SPU_RAM pc=8002c974 offset=01000 valid=1 bytes=" + "07" * 16,
    ])
    text = text.replace("native_boot: SOURCE_CLOCK", "\n".join(extra) + "\nnative_boot: SOURCE_CLOCK")
    text = text.replace("cycles=1000", "cycles=1100")
    for marker in ("CD_OWNER", "SPU_OWNER"):
        text = text.replace(f"{marker} cycle=1000", f"{marker} cycle=1100")
    text = text.replace("timer1_cycle=1000", "timer1_cycle=1100")
    text = text.replace("cycle=1000 faulted=0", "cycle=1100 faulted=0")
    text = text.replace("cd_left=0000 cd_right=0000 control=c000", "cd_left=3fff cd_right=3fff control=c001")
    return text.replace("STOP status=0 pc=8002c90c target=8002c90c", "STOP status=0 pc=8002c974 target=8002c974")


def test_common_profile_requires_explicit_selection():
    text = _synthetic_common_log()
    assert validate_spu_boot_log(text, common_attributes=True) == []
    assert validate_spu_boot_log(text)
    assert validate_spu_boot_log(_synthetic_valid_log(), common_attributes=True)


@pytest.mark.parametrize("needle,replacement", [
    ("pc=8003c598", "pc=8003c599"),
    ("address=1f801db0 value=3fff", "address=1f801db0 value=0000"),
    ("address=1f801daa value=c000", "address=1f801daa value=c004"),
    ("SPU_STAGE pc=8002c974 cycle=1010", "SPU_STAGE pc=8002c974 cycle=bad"),
    ("SPU_STAGE pc=8002c974 cycle=1010 samples=1", "SPU_STAGE pc=8002c974 cycle=1010 samples=0"),
    ("control=c001 manual_halfwords=16", "control=c001 manual_halfwords=17"),
    ("cd_left=3fff", "cd_left=0000"),
    ("target=8002c974", "target=8002c978"),
    ("SPU_RAM pc=8002c974 offset=01000 valid=1", "SPU_RAM pc=8002c974 offset=01000 valid=0"),
    ("value=c001 cycle=1008", "value=c001 cycle=1007"),
])
def test_common_profile_rejects_bad_source_and_owner(needle, replacement):
    text = _synthetic_common_log()
    assert needle in text
    assert validate_spu_boot_log(text.replace(needle, replacement, 1), common_attributes=True)


def test_optional_common_native_log():
    fixture = LIVE_FIXTURE.with_name("common-first.log")
    if not fixture.exists():
        pytest.skip("optional native common attribute log unavailable")
    assert validate_spu_boot_log(fixture.read_text(), common_attributes=True) == []


def test_common_cli_profile_is_forwarded(monkeypatch):
    from tools import native_spu_boot_check as checker
    calls = []
    monkeypatch.setattr(checker, "run_live", lambda *args, **kwargs: calls.append((args, kwargs)) or 0)
    assert checker.main(["--common-attributes", "binary", "exe", "cue", "bin"]) == 0
    assert calls[0][1] == {"common_attributes": True}


def _synthetic_reverb_log():
    from tools.native_spu_boot_check import _REVERB_COEFFICIENTS
    text = _synthetic_common_log()
    extra = [f"native_boot: CPU_TRACE pc={pc}" for pc in ("8003be24", "8002d1f0", "8003b954")]
    writes = [(0x1F801D84, 0), (0x1F801D86, 0)]
    writes += [(0x1F801DC0 + 2*i, value) for i, value in enumerate(_REVERB_COEFFICIENTS)]
    writes += [(0x1F801DA2, 0xF204)]
    for index, (address, value) in enumerate(writes):
        extra.append(f"native_boot: SPU_WRITE address={address:08x} value={value:04x} cycle={1101 + index}")
    coefficients = ",".join(f"{value:04x}" for value in _REVERB_COEFFICIENTS)
    for i, pc in enumerate(("8002c984", "8002c98c")):
        if i:
            extra.extend(["native_boot: CPU_TRACE pc=8003b280",
                          "native_boot: SPU_WRITE address=1f801daa value=c081 cycle=1500"])
        extra.extend([
            f"native_boot: CPU_TRACE pc={pc}",
            f"native_boot: SPU_STAGE pc={pc} cycle={1150+450*i} samples={1+i} control={'c081' if i else 'c001'} manual_halfwords=16 faulted=0",
            f"native_boot: SPU_REVERB pc={pc} base=f204 enabled={i} wet_left=0000 wet_right=0000 "
            f"cursor=79020 eon=000000 next_channel={i} frames={1+i} processed_left={i} processed_right=0 "
            f"ram_reads={10*i} ram_writes={4*i} unknown_reads=0 unsupported_steps=0 "
            f"unknown_history_left=0 unknown_history_right=0 bootstrap_halfwords=32768 coefficients={coefficients}",
        ])
    text = text.replace("native_boot: SOURCE_CLOCK", "\n".join(extra) + "\nnative_boot: SOURCE_CLOCK")
    text = text.replace("cycles=1100", "cycles=2000").replace("cycle=1100", "cycle=2000")
    text = text.replace("SPU_OWNER cycle=2000 samples=1", "SPU_OWNER cycle=2000 samples=2").replace("submitted=1", "submitted=2")
    text = text.replace("cd_left=3fff cd_right=3fff control=c001", "cd_left=3fff cd_right=3fff control=c081")
    return text.replace("STOP status=0 pc=8002c974 target=8002c974", "STOP status=0 pc=8002c98c target=8002c98c")


def test_reverb_profile_and_explicit_boundary():
    text = _synthetic_reverb_log()
    assert validate_spu_boot_log(text, reverb=True) == []
    assert validate_spu_boot_log(text, common_attributes=True)
    assert validate_spu_boot_log(_synthetic_common_log(), reverb=True)


@pytest.mark.parametrize("needle,replacement", [
    ("pc=8003be24", "pc=8003be25"),
    ("address=1f801dc0 value=00e3", "address=1f801dc0 value=0000"),
    ("address=1f801da2 value=f204", "address=1f801da2 value=fffe"),
    ("address=1f801daa value=c081", "address=1f801daa value=c001"),
    ("base=f204", "base=fffe"),
    ("enabled=1", "enabled=0"),
    ("frames=1", "frames=2"),
    ("processed_left=0", "processed_left=1"),
    ("bootstrap_halfwords=32768", "bootstrap_halfwords=0"),
    ("unknown_reads=0", "unknown_reads=bad"),
    ("ram_writes=4", "ram_writes=0"),
    ("cursor=79020", "cursor=78000"),
    ("coefficients=00e3", "coefficients=0000"),
    ("target=8002c98c", "target=8002c984"),
])
def test_reverb_profile_refuses_wrong_source_or_owner(needle, replacement):
    text = _synthetic_reverb_log()
    assert needle in text
    assert validate_spu_boot_log(text.replace(needle, replacement, 1), reverb=True)


def test_optional_reverb_native_log():
    fixture = LIVE_FIXTURE.with_name("reverb-live-check.log")
    if not fixture.exists():
        pytest.skip("optional native reverb log unavailable")
    assert validate_spu_boot_log(fixture.read_text(), reverb=True) == []


def _cpu_trace(pc, *, command=0, result=0, sp=0x801FFFC0):
    regs = [0] * 32
    regs[2], regs[4], regs[29], regs[30], regs[31] = result, command, sp, 0x801FFFC0, 0x800101FC
    return f"native_boot: CPU_TRACE pc={pc:08x} regs=" + ",".join(f"{r:08x}" for r in regs)


def _synthetic_sound_outer_log():
    """Parser-only authored fixture; no retained live log or captured RAM input."""
    text = _synthetic_reverb_log().replace(
        "native_boot: CPU_TRACE pc=8002c8f4 cycle=200", _cpu_trace(0x8002C8F4))
    # An immediate post-enable sample need not have done feedback work yet.
    text = text.replace("ram_reads=10 ram_writes=4", "ram_reads=10 ram_writes=0")
    extra = [_cpu_trace(pc) for pc in (0x80037D98, 0x8002CC00)]
    extra += ["native_boot: SPU_WRITE address=1f801d98 value=0000 cycle=2100",
              "native_boot: SPU_WRITE address=1f801d9a value=0000 cycle=2101"]
    extra += [_cpu_trace(pc) for pc in (0x80034C24, 0x800430DC)]
    packets = [(0x13, [], [2, 1, 4]), (0x14, [0], [2, 0x39, 0x20]),
               (0x14, [1], [2, 0, 2]), (0x14, [2], [2, 0x34, 0x32]),
               (0x14, [3], [2, 0x35, 0x32]), (0x14, [4], [2, 0x36, 0x14])]
    for i, (command, params, response) in enumerate(packets):
        cycle = 3000 + 4000*i
        extra += [_cpu_trace(0x80043830, command=command),
                  f"native_boot: CD_WRITE address=1f801800 value=00 cycle={cycle}"]
        for param in params:
            extra.append(f"native_boot: CD_WRITE address=1f801802 value={param:02x} cycle={cycle+1}")
        extra.append(f"native_boot: CD_WRITE address=1f801801 value={command:02x} cycle={cycle+2}")
        for j, value in enumerate(response):
            extra.append(f"native_boot: CD_RESPONSE address=1f801801 value={value:02x} cycle={cycle+2050+j}")
        extra.append(_cpu_trace(0x80043938, result=2))
    toc = " valid=1 tracks=4 address=800c7d30 bytes=3920000000020000343200003532000036140000"
    extra += [_cpu_trace(0x80034CD0, result=4), "native_boot: SOUND_TOC pc=80034cd0" + toc,
              _cpu_trace(0x8002CC34), _cpu_trace(0x800101FC, result=0xFFFFFFF0),
              "native_boot: SOUND_TOC pc=800101fc" + toc]
    reverb = next(line for line in text.splitlines() if "SPU_REVERB pc=8002c98c" in line)
    reverb = reverb.replace("pc=8002c98c", "pc=800101fc").replace("frames=2", "frames=52")
    reverb = reverb.replace("processed_left=1", "processed_left=26").replace("processed_right=0", "processed_right=25")
    reverb = reverb.replace("ram_reads=10 ram_writes=0", "ram_reads=100 ram_writes=40")
    extra.append(reverb)
    text = text.replace("native_boot: SOURCE_CLOCK", "\n".join(extra) + "\nnative_boot: SOURCE_CLOCK")
    text = text.replace("cycles=2000", "cycles=40000").replace("cycle=2000", "cycle=40000")
    text = text.replace("SPU_OWNER cycle=40000 samples=2", "SPU_OWNER cycle=40000 samples=52").replace("submitted=2", "submitted=52")
    text = text.replace("CD_OWNER cycle=40000 fault=0", "CD_OWNER cycle=40000 fault=0 command=14 phase=0 if=00 enable=07 fifo=0 irq_requests=10")
    text = text.replace("STOP status=0 pc=8002c98c target=8002c98c", "STOP status=0 pc=8001971c target=8001971c IRQ_installed=1 deliveries=11")
    text += "native_boot: bios_cd_installed=0 bios_event_used=9 guest_irq_enabled=1 irq_faulted=0\n"
    return text


def test_sound_outer_synthetic_profile():
    text = _synthetic_sound_outer_log()
    assert validate_spu_boot_log(text, sound_outer=True) == []
    assert validate_spu_boot_log(text, reverb=True)
    assert validate_spu_boot_log(_synthetic_reverb_log(), sound_outer=True)


@pytest.mark.parametrize("needle,replacement", [
    ("value=13 cycle=3002", "value=14 cycle=3002"),
    ("value=00 cycle=7001", "value=01 cycle=7001"),
    ("value=39 cycle=9051", "value=38 cycle=9051"),
    ("value=04 cycle=5052", "value=04 cycle=5000"),
    ("value=04 cycle=5052", "value=04 cycle=5050"),
    ("value=14 cycle=23002", "value=14 cycle=14000"),
    ("SOUND_TOC pc=80034cd0", "SOUND_TOC pc=80034cd1"),
    ("bytes=39200000", "bytes=39200001"),
    ("tracks=4", "tracks=5"),
    ("irq_requests=10", "irq_requests=9"),
    ("enable=07", "enable=00"),
    ("deliveries=11", "deliveries=10"),
    ("irq_faulted=0", "irq_faulted=1"),
    ("CD_IRQ frame_busy=0", "CD_IRQ frame_busy=1"),
    ("SPU_OWNER cycle=40000 samples=52", "SPU_OWNER cycle=40000 samples=53"),
    ("SPU_REVERB pc=800101fc base=f204", "SPU_REVERB pc=800101fc base=fffe"),
    ("ram_reads=100 ram_writes=40", "ram_reads=100 ram_writes=0"),
    ("frames=52", "frames=999"),
    ("processed_left=26", "processed_left=25"),
    ("ram_reads=100", "ram_reads=bad"),
    ("address=1f801d9a value=0000", "address=1f801d9a value=0001"),
    ("address=1f801801 value=02 cycle=5050", "address=1f801801 value=bad cycle=5050"),
])
def test_sound_outer_negative_fields(needle, replacement):
    text = _synthetic_sound_outer_log()
    assert needle in text
    assert validate_spu_boot_log(text.replace(needle, replacement, 1), sound_outer=True)


@pytest.mark.parametrize("pc", [0x8002C8F4, 0x80043830, 0x80043938, 0x800101FC])
@pytest.mark.parametrize("regs", ["00000000", ",".join(["0"] * 33), "garbage", ",".join(["100000000"] * 32)])
def test_sound_outer_malformed_regs_refuse_without_exception(pc, regs):
    import re
    text = _synthetic_sound_outer_log()
    text = re.sub(rf"(CPU_TRACE pc={pc:08x} regs=)[^\n]+", lambda m: m[1] + regs, text, count=1)
    assert validate_spu_boot_log(text, sound_outer=True)


def test_sound_outer_return_stack_and_ra_refuse():
    text = _synthetic_sound_outer_log()
    original = _cpu_trace(0x800101FC, result=0xFFFFFFF0)
    assert validate_spu_boot_log(text.replace(original, _cpu_trace(0x800101FC, result=0xFFFFFFF0, sp=0x801FFF00)), sound_outer=True)
    assert validate_spu_boot_log(text.replace(original, original[:-8] + "800101f8"), sound_outer=True)


def test_sound_outer_wait_order_and_response_location_refuse():
    text = _synthetic_sound_outer_log()
    wait = _cpu_trace(0x80043938, result=2) + "\n"
    moved = text.replace(wait, "", 1).replace("native_boot: CPU_TRACE pc=800430dc", wait + "native_boot: CPU_TRACE pc=800430dc", 1)
    assert validate_spu_boot_log(moved, sound_outer=True)
    response = "native_boot: CD_RESPONSE address=1f801801 value=04 cycle=5052\n"
    moved = text.replace(response, "", 1).replace(wait, wait + response, 1)
    assert validate_spu_boot_log(moved, sound_outer=True)


def test_sound_outer_cli_forwarding(monkeypatch):
    from tools import native_spu_boot_check as checker
    calls = []
    monkeypatch.setattr(checker, "run_live", lambda *args, **kwargs: calls.append((args, kwargs)) or 0)
    assert checker.main(["--sound-outer", "binary", "exe", "cue", "bin"]) == 0
    assert calls[0][1] == {"sound_outer": True}


def test_optional_sound_outer_native_log():
    fixture = LIVE_FIXTURE.with_name("outer-live-check-final.log")
    if not fixture.exists():
        pytest.skip("optional retained full sound initializer log unavailable")
    assert validate_spu_boot_log(fixture.read_text(), sound_outer=True) == []


@pytest.mark.parametrize("marker", ["SOUND_TOC pc=80034cd0", "SPU_REVERB pc=800101fc"])
def test_sound_outer_snapshot_must_follow_source_boundary(marker):
    text = _synthetic_sound_outer_log()
    row = next(line for line in text.splitlines() if marker in line) + "\n"
    assert validate_spu_boot_log(row + text.replace(row, "", 1), sound_outer=True)


def test_sound_outer_duplicate_owner_cannot_hide_fault():
    text = _synthetic_sound_outer_log()
    row = next(line for line in text.splitlines() if "CD_OWNER " in line)
    assert validate_spu_boot_log(text.replace(row, row.replace("fault=0", "fault=1") + "\n" + row), sound_outer=True)


def test_sound_outer_cli_live_validation_profile_forwarded(monkeypatch, tmp_path):
    from types import SimpleNamespace
    from tools import native_spu_boot_check as checker
    calls = []
    monkeypatch.setattr(checker.shutil, "which", lambda _: "/test/xvfb-run")
    monkeypatch.setattr(checker, "_negative_inputs", lambda *args: [])
    monkeypatch.setattr(checker, "_run", lambda *args, **kwargs: SimpleNamespace(
        returncode=2, stdout=_synthetic_sound_outer_log(), stderr=""))
    validate = checker.validate_spu_boot_log
    def observed(text, **kwargs):
        calls.append(kwargs)
        return validate(text, **kwargs)
    monkeypatch.setattr(checker, "validate_spu_boot_log", observed)
    assert checker.run_live(*(tmp_path / p for p in ("binary", "exe", "cue", "bin")), sound_outer=True) == 0
    assert calls == [{"common_attributes": False, "reverb": False, "sound_outer": True}]
