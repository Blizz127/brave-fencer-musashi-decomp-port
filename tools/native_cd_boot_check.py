#!/usr/bin/env python3
"""Check the bounded native startup with the pinned mounted CD profile.

The checker keeps parser/fixture evidence separate from a live run.  A live
run must use the four-argument native profile (EXE, CUE, BIN) and a real SDL
audio device; it never substitutes dummy audio or a synthetic ready state.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable


HEX = r"[0-9A-Fa-f]+"


@dataclass
class NativeMountedLog:
    text: str
    fields: dict[str, str] = field(default_factory=dict)
    sections: dict[str, dict[str, str]] = field(default_factory=dict)
    writes: list[tuple[int, int, int]] = field(default_factory=list)
    cpu: list[tuple[int, list[int]]] = field(default_factory=list)
    event_states: dict[int, dict[str, str]] = field(default_factory=dict)
    lines: list[str] = field(default_factory=list)
    parse_errors: list[str] = field(default_factory=list)


def _fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line))


def parse_mounted_log(text: str) -> NativeMountedLog:
    parsed = NativeMountedLog(text=text, lines=text.splitlines())
    for line in parsed.lines:
        # Keep an aggregate for teardown/status fields emitted on their own
        # lines, while section maps below preserve repeated owner keys.
        parsed.fields.update(_fields(line))
        if "CD_WRITE address=" in line:
            match = re.search(
                rf"CD_WRITE address=({HEX}) value=({HEX}) cycle=(\d+)", line
            )
            if match:
                parsed.writes.append(tuple(
                    int(value, 16) if index < 2 else int(value)
                    for index, value in enumerate(match.groups())
                ))
            else:
                parsed.parse_errors.append("malformed CD_WRITE number")
        if "CPU_TRACE pc=" in line:
            match = re.search(r"CPU_TRACE pc=(\w+) .* regs=([0-9A-Fa-f,]+)", line)
            if match:
                registers = [int(value, 16) for value in match.group(2).split(",")]
                parsed.cpu.append((int(match.group(1), 16), registers))
            else:
                parsed.parse_errors.append("malformed CPU_TRACE number")
        if "EVENT_STATE index=" in line:
            values = _fields(line)
            try:
                parsed.event_states[int(values["index"])] = values
            except (KeyError, ValueError):
                pass
        for name in (
            "SOURCE_CLOCK", "DEVICE_EPOCH", "CD_OWNER", "CD_IRQ",
            "SPU_OWNER", "VIDEO_TIMING", "SIO", "INPUT_OWNER", "STOP",
            "guest", "startup", "OWNED_DEVICES",
        ):
            marker = f"{name} " if name != "startup" else "startup="
            if marker in line:
                values = _fields(line)
                parsed.sections[name] = values
    return parsed


def _require(errors: list[str], condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


def _has_contiguous(values: Iterable[tuple[int, int]], wanted: list[tuple[int, int]]) -> bool:
    values = list(values)
    for start in range(len(values) - len(wanted) + 1):
        if values[start:start + len(wanted)] == wanted:
            return True
    return False


def validate_mounted_log(text: str) -> list[str]:
    """Return source/owner violations; an empty list is a PASS."""
    parsed = parse_mounted_log(text)
    errors: list[str] = list(parsed.parse_errors)
    f = parsed.fields
    section = parsed.sections
    source_clock = section.get("SOURCE_CLOCK", {})
    epoch = section.get("DEVICE_EPOCH", {})
    cd_owner = section.get("CD_OWNER", {})
    cd_irq = section.get("CD_IRQ", {})
    spu = section.get("SPU_OWNER", {})
    input_owner = section.get("INPUT_OWNER", {})
    stop = section.get("STOP", {})

    _require(errors, "VIDEO_SOURCE mode=0 policy=NTSC selected_before_worker=1" in text,
             "mounted NTSC source was not selected before the worker")
    _require(errors, stop.get("status") == "0" and stop.get("pc") == "8002c8f4" and
             stop.get("target") == "8002c8f4", "bounded startup did not stop at 8002C8F4")
    _require(errors, f.get("startup") == "PARTIAL" and
             f.get("incoming_ra") == "NATIVE_ZERO" and f.get("menu") == "NOT_REACHED",
             "startup/menu boundary is not the mounted bounded profile")

    source_cpu = [(registers, pc) for pc, registers in parsed.cpu
                  if pc == 0x800101EC and len(registers) == 32]
    _require(errors, len(source_cpu) == 1 and source_cpu[0][0][2] == 1,
             "source return checkpoint 800101EC with v0=1 was not observed")

    _require(errors, len(parsed.writes) > 0, "no mounted CD writes were observed")
    if parsed.writes:
        _require(errors, all(left[2] < right[2]
                             for left, right in zip(parsed.writes, parsed.writes[1:])),
                 "CD write cycles are not strictly increasing (early deadline/reset)")
    # The final matrix commit also writes 0x80 through 1F801801 while bank 3
    # is selected; only the three command-byte values belong to this list.
    bank = None
    command_values: list[int] = []
    command_cycles: dict[int, int] = {}
    unknown_commands: list[int] = []
    ack_cycles: list[int] = []
    ack_writes = 0
    enable_writes = 0
    for address, value, cycle in parsed.writes:
        if address == 0x1F801800:
            bank = value & 3
        elif address == 0x1F801801 and bank == 0:
            if value in (0x01, 0x0A, 0x0C):
                command_values.append(value)
                command_cycles[value] = cycle
            else:
                unknown_commands.append(value)
        elif address == 0x1F801803 and bank == 1 and value == 0x07:
            ack_cycles.append(cycle)
            ack_writes += 1
        elif address == 0x1F801802 and bank == 1 and value == 0x07:
            enable_writes += 1
    _require(errors, command_values == [0x01, 0x0A, 0x0C],
             "mounted CD command order is not exactly 01, 0A, 0C")
    _require(errors, not unknown_commands,
             "unsupported bank-0 CD command was accepted")
    if (0x01 in command_cycles and 0x0A in command_cycles and
            0x0C in command_cycles and len(ack_cycles) == 4):
        _require(errors, ack_cycles[0] >= command_cycles[0x01] + 0x800 and
                 ack_cycles[1] >= command_cycles[0x0A] + 0x800 and
                 ack_cycles[3] >= command_cycles[0x0C] + 0x800,
                 "a CD command was acknowledged before its 0x800 deadline")
        _require(errors, sum(cycle < command_cycles[0x0A] for cycle in ack_cycles) == 1 and
                 sum(cycle < command_cycles[0x0C] for cycle in ack_cycles) == 3,
                 "CD ACK chronology does not bracket 01, 0A, 0C")
        _require(errors, command_cycles[0x0C] >= command_cycles[0x0A] + 0x800 + 4100000,
                 "reset command was accepted before the 0A reset deadline")
    else:
        errors.append("missing command cycle for reset deadline")
    _require(errors, ack_writes == 4 and enable_writes == 4,
             "mounted CD did not show four bank-1 ACK/enable receipts")
    matrix_tail = [(address, value) for address, value, _ in parsed.writes[-7:]]
    _require(errors, _has_contiguous(matrix_tail, [
        (0x1F801800, 0x02), (0x1F801802, 0x80), (0x1F801803, 0x00),
        (0x1F801800, 0x03), (0x1F801801, 0x80), (0x1F801802, 0x00),
        (0x1F801803, 0x20),
    ]), "source CD matrix writes did not publish the identity matrix")

    _require(errors, cd_owner.get("fault") == "0" and
             cd_owner.get("phase") == "0" and cd_owner.get("fifo") == "0" and
             cd_owner.get("command") == "0c" and
             cd_owner.get("irq_requests") == "4" and cd_owner.get("if") == "00" and
             cd_owner.get("enable") == "07" and
             cd_owner.get("common_delay_written") == "1" and
             cd_owner.get("common_delay") == "00001325",
             "CD owner did not end healthy after four internal IRQ receipts")
    _require(errors, cd_irq.get("frame_busy") == "0" and
             cd_irq.get("frame_faulted") == "0" and
             cd_irq.get("instructions", "0").isdigit() and
             int(cd_irq.get("instructions", "0")) > 0 and
             cd_irq.get("call") == "ffffffff" and cd_irq.get("target") == "ffffffff",
             "CD IRQ callback frame was not cleanly returned")

    _require(errors, source_clock.get("faulted") == "0" and
             source_clock.get("scope") == "STARTUP_INPUT_CD_IRQ_SOURCE_ONLY" and
             source_clock.get("devices_advanced") == "DEVICE_EPOCH",
             "shared source clock is not healthy mounted epoch")
    epoch_cycle = _safe_int(epoch.get("cycles"), 10)
    source_cycle = _safe_int(source_clock.get("cycles"), 10)
    cd_cycle = _safe_int(cd_owner.get("cycle"), 10)
    spu_cycle = _safe_int(spu.get("cycle"), 10)
    sio_line = next((line for line in parsed.lines if "SIO profile=" in line), "")
    sio_fields = _fields(sio_line)
    sio_cycle = _safe_int(sio_fields.get("cycle"), 10)
    timer_cycle = _safe_int(section.get("VIDEO_TIMING", {}).get("timer1_cycle"), 10)
    _require(errors, epoch.get("profile") == "PACED_HOST_TIME" and
             epoch.get("cd_advanced") == "1" and epoch.get("faulted") == "0" and
             _positive_u32(epoch.get("frequency")) and
             source_cycle is not None and source_cycle > 0 and
             epoch_cycle is not None and source_cycle <= epoch_cycle and
             None not in (epoch_cycle, cd_cycle, spu_cycle, sio_cycle, timer_cycle) and
             len({epoch_cycle, cd_cycle, spu_cycle, sio_cycle, timer_cycle}) == 1,
             "mounted device epoch did not advance the CD owner")

    samples = _safe_int(spu.get("samples"), 10)
    submitted = _safe_int(spu.get("submitted"), 10)
    _require(errors, spu.get("main_left") == "7ffe" and
             spu.get("main_right") == "7ffe" and spu.get("cd_left") == "3fff" and
             spu.get("cd_right") == "3fff" and spu.get("control") == "c001" and
             spu.get("faulted") == "0" and
             spu.get("profile") == "SETTLED_BIOS_MAIN_MUTE" and epoch_cycle is not None and
             samples == epoch_cycle // 768 and submitted == samples and samples > 0,
             "SPU current gains/control are not the source-published values")
    video = section.get("VIDEO_TIMING", {})
    _require(errors, video.get("faulted") == "0" and video.get("running") == "1",
             "Timer1/VBlank timing owner is not healthy")
    _require(errors, input_owner.get("installed") == "1" and
             input_owner.get("faulted") == "0" and
             input_owner.get("frame_busy") == "0" and
             input_owner.get("frame_faulted") == "0",
             "final input owner state is not healthy")

    # Natural input polling is evidence when emitted, but its count is host
    # timing dependent and is deliberately not a seven-poll quota.
    if sio_line:
        _require(errors, sio_fields.get("bound") == "1" and
                 sio_fields.get("faulted") == "0" and
                 _nonnegative_int(sio_fields.get("keyboard_polls")),
                 "reported natural input polling is unhealthy")
    slot2 = re.search(r"\bslot2=([0-9A-Fa-f]+)", text)
    if slot2:
        slot2_value = _safe_int(slot2.group(1), 16)
        _require(errors, slot2_value == 0x80045290,
                 "reported slot2 does not match the source callback")
    if 2 in parsed.event_states:
        _require(errors, parsed.event_states[2].get("callback") == "800617f4",
             "reported event slot2 callback is not the source callback")

    _require(errors, f.get("irq_faulted") == "0" and stop.get("I_MASK") == "000d" and
             section.get("guest", {}).get("guard") == "0001" and
             section.get("guest", {}).get("slot0") == "80042ce8" and
             section.get("guest", {}).get("slot3") == "80042e08" and
             f.get("scheduler_removed") == "1" and f.get("card_removed") == "1" and
             f.get("bu_removed") == "1" and f.get("input_removed") == "1" and
             f.get("gpu_removed") == "1" and
             "scheduler removal refused" not in text,
             "scheduler/guest IRQ teardown state is not healthy")
    _require(errors, section.get("OWNED_DEVICES", {}).get("removed") == "1",
             "owned device teardown marker is missing")

    return errors


def _safe_int(value: str | None, base: int = 10) -> int | None:
    try:
        return int(value, base) if value is not None else None
    except (TypeError, ValueError):
        return None


def _positive_u32(value: str | None) -> bool:
    parsed = _safe_int(value, 10)
    return parsed is not None and 0 < parsed <= 0xFFFFFFFF


def _nonnegative_int(value: str | None) -> bool:
    parsed = _safe_int(value, 10)
    return parsed is not None and parsed >= 0


def _run(command: list[str], *, cwd: Path, env: dict[str, str], timeout: int = 30) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=timeout)


def _negative_inputs(binary: Path, exe: Path, cue: Path, image: Path, cwd: Path,
                     env: dict[str, str]) -> list[str]:
    errors: list[str] = []
    wrong_exe = cwd / "wrong.exe"
    wrong_exe.write_bytes(b"not-a-pinned-exe\n")
    rejected = _run([str(binary), str(wrong_exe), str(cue), str(image)], cwd=cwd, env=env)
    output = rejected.stdout + rejected.stderr
    if rejected.returncode != 2 or "pinned retail EXE rejected before startup" not in output:
        errors.append("wrong EXE was not rejected before startup")
    if "Initialising Psy-X" in output:
        errors.append("wrong EXE reached Psy-X startup")

    wrong_cue = cwd / "wrong.cue"
    wrong_bin = cwd / "wrong.bin"
    wrong_cue.write_text("FILE \"wrong.bin\" BINARY\n", encoding="ascii")
    wrong_bin.write_bytes(b"wrong-media")
    rejected = _run([str(binary), str(exe), str(wrong_cue), str(wrong_bin)], cwd=cwd, env=env)
    output = rejected.stdout + rejected.stderr
    if rejected.returncode != 2 or "pinned media rejected before startup" not in output:
        errors.append("wrong media was not rejected before startup")
    if "Initialising Psy-X" in output:
        errors.append("wrong media reached Psy-X startup")
    return errors


def run_live(binary: Path, exe: Path, cue: Path, image: Path) -> int:
    xvfb = shutil.which("xvfb-run")
    if not xvfb:
        print("mounted native CD: no isolated display; NOT_RUN", file=sys.stderr)
        return 77
    with tempfile.TemporaryDirectory(prefix="musashi-mounted-cd-check-") as temp:
        cwd = Path(temp)
        env = os.environ.copy()
        env["SDL_VIDEODRIVER"] = "x11"
        runtime = Path(f"/run/user/{os.getuid()}")
        if (runtime / "pipewire-0").exists():
            env["XDG_RUNTIME_DIR"] = str(runtime)
        errors = _negative_inputs(binary, exe, cue, image, cwd, env)
        if errors:
            print("mounted native CD: " + "; ".join(errors), file=sys.stderr)
            return 1
        run = _run([xvfb, "-a", str(binary), str(exe), str(cue), str(image)],
                   cwd=cwd, env=env)
        output = run.stdout + run.stderr
        if run.returncode == 77:
            print(output, end="", file=sys.stderr)
            print("mounted native CD: NOT_RUN (real device/display unavailable)", file=sys.stderr)
            return 77
        errors = validate_mounted_log(output)
        if run.returncode != 2:
            errors.append(f"native binary returned {run.returncode}, expected bounded 2")
        if errors:
            print(output, end="", file=sys.stderr)
            print("mounted native CD: " + "; ".join(errors), file=sys.stderr)
            return 1
        print(output, end="")
        print("MOUNTED_NATIVE_CD_CHECK: PASS evidence=LIVE_NATIVE mounted_media=1 menu=NOT_REACHED")
        return 0


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) != 4:
        print("usage: native_cd_boot_check.py <binary> <pinned exe> <pinned cue> <pinned bin>",
              file=sys.stderr)
        return 2
    binary, exe, cue, image = (Path(value).resolve() for value in args)
    return run_live(binary, exe, cue, image)


if __name__ == "__main__":
    raise SystemExit(main())
