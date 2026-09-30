#!/usr/bin/env python3
"""Check explicit bounded native SPU startup and complete sound-init profiles.

This checker parses the native trace emitted by the formatter/device owner.  It
does not reuse a captured emulator state and does not certify audible output.
The optional live mode uses the same pinned-media and real-device runner as the
mounted CD checker, while keeping the 0x8002C90C boundary separate.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

if __package__:
    from .native_cd_boot_check import _negative_inputs, _run
else:
    from native_cd_boot_check import _negative_inputs, _run


_MILESTONES = [0x8002C8F4, 0x8003A424, 0x8002C904,
               0x8003A424, 0x8003D530, 0x8002C90C]
_SPU_DA6 = 0x1F801DA6
_SPU_DA8 = 0x1F801DA8


@dataclass
class NativeSpuLog:
    text: str
    lines: list[str] = field(default_factory=list)
    cpu: list[tuple[int, int, list[int] | None]] = field(default_factory=list)
    spu_writes: list[tuple[int, int, int, int]] = field(default_factory=list)
    sections: dict[str, dict[str, str]] = field(default_factory=dict)
    events_open: list[dict[str, str]] = field(default_factory=list)
    events_enable: list[dict[str, str]] = field(default_factory=list)
    dma_registers: list[dict[str, str]] = field(default_factory=list)
    spu_stages: list[dict[str, str]] = field(default_factory=list)
    spu_ram: list[dict[str, str]] = field(default_factory=list)
    spu_voices: list[dict[str, str]] = field(default_factory=list)
    parse_errors: list[str] = field(default_factory=list)


def _fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line))


def _hex(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value.removeprefix("0x").removesuffix("ULL"), 16)
    except ValueError:
        return None


def _decimal(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value.removesuffix("ULL"), 10)
    except ValueError:
        return None


def _record_number_error(parsed: NativeSpuLog, line_no: int, marker: str,
                         values: dict[str, str], names: tuple[str, ...]) -> None:
    if any(_hex(values.get(name)) is None for name in names):
        parsed.parse_errors.append(f"line {line_no}: malformed {marker} number")


def parse_spu_boot_log(text: str) -> NativeSpuLog:
    parsed = NativeSpuLog(text=text, lines=text.splitlines())
    section_names = ("SOURCE_CLOCK", "DEVICE_EPOCH", "SPU_OWNER", "CD_OWNER",
                     "CD_IRQ", "SIO", "VIDEO_TIMING", "INPUT_OWNER", "STOP",
                     "OWNED_DEVICES", "DMA_OWNER", "EVENT_OWNER")
    for line_no, line in enumerate(parsed.lines):
        values = _fields(line)
        if "CPU_TRACE" in line:
            pc = _hex(values.get("pc"))
            if pc is None:
                parsed.parse_errors.append(f"line {line_no}: malformed CPU_TRACE pc")
                continue
            regs: list[int] | None = None
            if "regs" in values:
                try:
                    regs = [int(word, 16) for word in values["regs"].split(",")]
                except ValueError:
                    parsed.parse_errors.append(f"line {line_no}: malformed CPU_TRACE regs")
                if regs is not None and (len(regs) != 32 or
                                         any(not 0 <= value <= 0xFFFFFFFF for value in regs)):
                    parsed.parse_errors.append(f"line {line_no}: CPU_TRACE register count/range")
                    regs = None
            parsed.cpu.append((line_no, pc, regs))

        if re.search(r"\bSPU_(?:WRITE|READ)(?:\s|=)", line):
            marker = "SPU_WRITE" if re.search(r"\bSPU_WRITE(?:\s|=)", line) else "SPU_READ"
            address = _hex(values.get("address"))
            value = _hex(values.get("value"))
            cycle = _decimal(values.get("cycle"))
            if address is None or value is None or cycle is None:
                parsed.parse_errors.append(f"line {line_no}: malformed {marker} number")
            elif marker == "SPU_WRITE":
                parsed.spu_writes.append((line_no, address, value, cycle))

        if re.search(r"\bSPU_STAGE(?:\s|=)", line):
            parsed.spu_stages.append(values)
        if re.search(r"\bSPU_RAM(?:\s|=)", line):
            parsed.spu_ram.append(values)
        if re.search(r"\bSPU_VOICE(?:\s|=)", line):
            parsed.spu_voices.append(values)

        for name in section_names:
            if re.search(rf"\b{name}(?:\s|=)", line):
                parsed.sections[name] = values
        if re.search(r"\bEVENT_OPEN(?:\s|=)", line):
            parsed.events_open.append(values)
        if re.search(r"\bEVENT_ENABLE(?:\s|=)", line):
            parsed.events_enable.append(values)
        if re.search(r"\bDMA_REGISTER(?:\s|=)", line):
            parsed.dma_registers.append(values)
    return parsed


def _require(errors: list[str], condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


def _section_int(section: dict[str, str], key: str, *, base: int = 10) -> int | None:
    value = section.get(key)
    return _decimal(value) if base == 10 else _hex(value)


def _integer(value: str | None) -> int | None:
    """Parse a decimal diagnostic value, accepting the usual hex spelling."""
    result = _decimal(value)
    return result if result is not None else _hex(value)


def _source_event_open(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    result = []
    for row in rows:
        if (_hex(row.get("class")) == 0xF0000009 and
                _hex(row.get("spec")) == 0x20 and
                _hex(row.get("mode")) == 0x2000 and
                _hex(row.get("callback")) == 0):
            result.append(row)
    return result


def _source_dma_register(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    result = []
    for row in rows:
        channel = _decimal(row.get("channel"))
        if channel is None:
            channel = _hex(row.get("channel"))
        callback = _hex(row.get("callback"))
        target = row.get("target")
        if (channel == 4 and callback == 0x8003AA18 and
                (target is None or _hex(target) == 0x80042F8C)):
            result.append(row)
    return result


_REVERB_COEFFICIENTS = [int(word, 16) for word in (
    "00e3 00a9 6f60 4fa8 bce0 4510 bef0 a680 5680 52c0 0dfb 0b58 0d09 0a3c 0bd9 0973 "
    "0b59 08da 08d9 05e9 07ec 04b0 06ef 03d2 05ea 031d 031c 0238 0154 00aa 8000 8000"
).split()]


def _validate_reverb(parsed: NativeSpuLog, errors: list[str], stage_cycles: list[int | None], *, continuing: bool = False) -> None:
    pcs = [0x8002C974, 0x8003BE24, 0x8002D1F0, 0x8003B954, 0x8002C984, 0x8003B280, 0x8002C98C]
    _require(errors, [pc for _, pc, _ in parsed.cpu if pc in pcs] == pcs,
             "reverb source call/return order is missing or duplicated")
    start = next((line for line, pc, _ in parsed.cpu if pc == pcs[0]), -1)
    end = next((line for line, pc, _ in parsed.cpu if pc == pcs[-1]), -1)
    writes = [(line, address, value, cycle) for line, address, value, cycle in parsed.spu_writes if line > start and (not continuing or line < end)]
    expected = [(0x1F801D84, 0), (0x1F801D86, 0)]
    expected += [(0x1F801DC0 + 2*i, value) for i, value in enumerate(_REVERB_COEFFICIENTS)]
    expected += [(0x1F801DA2, 0xF204), (0x1F801DAA, 0xC081)]
    _require(errors, [(address, value) for _, address, value, _ in writes] == expected and
             all(line < end for line, _, _, _ in writes),
             "reverb source does not issue exactly36 ordered mode4 writes before return")
    _require(errors, all(a[3] < b[3] for a, b in zip(writes, writes[1:])),
             "reverb write cycles are not increasing")
    stages = [row for row in parsed.spu_stages if _hex(row.get("pc")) in (0x8002C984, 0x8002C98C)]
    _require(errors, [_hex(row.get("pc")) for row in stages] == [0x8002C984, 0x8002C98C],
             "reverb pre/post enable stages are missing or duplicated")
    diagnostics = [_fields(line) for line in parsed.lines if re.search(r"\bSPU_REVERB\b", line)]
    if continuing:
        diagnostics = [row for row in diagnostics if _hex(row.get("pc")) in (0x8002C984, 0x8002C98C)]
    _require(errors, [_hex(row.get("pc")) for row in diagnostics] == [0x8002C984, 0x8002C98C],
             "reverb owner diagnostics are missing or duplicated")
    if len(diagnostics) == 2:
        before = _decimal(diagnostics[0].get("ram_writes"))
        after = _decimal(diagnostics[1].get("ram_writes"))
        _require(errors, before == 0 and after is not None and after >= (0 if continuing else 1) and after % 4 == 0,
                 "native reverb checkpoint has no observed complete feedback RAM work")
    for i, stage in enumerate(stages[:2]):
        cycle = _decimal(stage.get("cycle"))
        samples = _decimal(stage.get("samples"))
        _require(errors, cycle is not None and stage_cycles and stage_cycles[-1] is not None and
                 cycle > stage_cycles[-1] and samples == cycle // 768 and
                 stage.get("faulted") == "0" and _hex(stage.get("control")) == (0xC081 if i else 0xC001) and
                 _decimal(stage.get("manual_halfwords")) == 16,
                 "reverb stage has inconsistent clock/control or invented manual transfers")
        stage_cycles.append(cycle)
        if i >= len(diagnostics):
            continue
        row = diagnostics[i]
        frames, left, right = (_decimal(row.get(k)) for k in ("frames", "processed_left", "processed_right"))
        _require(errors, _hex(row.get("base")) == 0xF204 and _decimal(row.get("enabled")) == i and
                 _hex(row.get("wet_left")) == _hex(row.get("wet_right")) == 0 and
                 _hex(row.get("eon")) == 0 and _decimal(row.get("bootstrap_halfwords")) == 32768 and
                 [_hex(v) for v in row.get("coefficients", "").split(",")] == _REVERB_COEFFICIENTS,
                 "reverb owner does not retain actual mode4 registers and source-backed BIOS clear")
        cursor = _hex(row.get("cursor"))
        _require(errors, cursor is not None and 0x79020 <= cursor < 0x80000 and cursor % 2 == 0 and
                 frames == samples and frames is not None and left is not None and right is not None and
                 left + right == max(frames - 1, 0) and
                 _decimal(row.get("next_channel")) == (max(frames - 1, 0) % 2),
                 "reverb processing is not on the shared sample clock/work area")
        for key in ("ram_reads", "ram_writes", "unknown_reads", "unsupported_steps",
                    "unknown_history_left", "unknown_history_right"):
            value = _decimal(row.get(key))
            _require(errors, value is not None and value >= 0,
                     f"reverb diagnostic {key} is missing or malformed")


def _validate_sound_outer(parsed: NativeSpuLog, errors: list[str]) -> None:
    # A later healthy summary must not overwrite an earlier fault record.
    for name in ("SOURCE_CLOCK", "DEVICE_EPOCH", "SPU_OWNER", "CD_OWNER", "CD_IRQ",
                 "SIO", "VIDEO_TIMING", "STOP", "OWNED_DEVICES"):
        _require(errors, sum(bool(re.search(rf"\b{name}(?:\s|=)", line))
                             for line in parsed.lines) == 1,
                 f"sound outer {name} summary is missing or duplicated")
    for line_number, line in enumerate(parsed.lines):
        values = _fields(line)
        if ("SOUND_TOC " not in line and
                not ("SPU_REVERB " in line and _hex(values.get("pc")) == 0x800101FC)):
            continue
        preceding = [(position, pc) for position, pc, _ in parsed.cpu if position < line_number]
        _require(errors, bool(preceding) and preceding[-1][1] == _hex(values.get("pc")),
                 "sound snapshot is not attached to its actual source CPU boundary")
    required = [0x8002C98C, 0x80037D98, 0x8002CC00, 0x80034C24,
                0x800430DC, 0x80034CD0, 0x8002CC34, 0x800101FC]
    _require(errors, [pc for _, pc, _ in parsed.cpu if pc in required] == required,
             "complete sound initializer source order is missing or duplicated")
    start = next((line for line, pc, _ in parsed.cpu if pc == 0x800430DC), len(parsed.lines))
    reverb_end = next((line for line, pc, _ in parsed.cpu if pc == 0x8002C98C), -1)
    _require(errors, [(address, value) for line, address, value, _ in parsed.spu_writes if line > reverb_end] ==
             [(0x1F801D98, 0), (0x1F801D9A, 0)], "outer source did not clear both actual EON registers")
    calls = [regs for _, pc, regs in parsed.cpu if pc == 0x80043830]
    _require(errors, len(calls) == 6 and all(regs is not None for regs in calls) and
             [regs[4] for regs in calls if regs is not None] == [0x13] + [0x14]*5,
             "source did not call GetTN then five GetTD queries")
    returns = [regs for _, pc, regs in parsed.cpu if pc == 0x80043938]
    _require(errors, len(returns) == 6 and all(regs is not None and regs[2] == 2 for regs in returns),
             "source wait did not complete all six TOC queries")
    entry_sp = [regs[29] for _, pc, regs in parsed.cpu if pc == 0x8002C8F4 and regs is not None]
    return_sp = [regs[29] for _, pc, regs in parsed.cpu if pc == 0x800101FC and regs is not None]
    _require(errors, len(entry_sp) == len(return_sp) == 1 and entry_sp == return_sp,
             "sound initializer did not restore its incoming source stack")
    entry_regs = [regs for _, pc, regs in parsed.cpu if pc == 0x8002C8F4 and regs is not None]
    final_regs = [regs for _, pc, regs in parsed.cpu if pc == 0x800101FC and regs is not None]
    _require(errors, len(entry_regs) == len(final_regs) == 1 and
             entry_regs[0][30] == final_regs[0][30] and
             entry_regs[0][31] == final_regs[0][31] == 0x800101FC and
             0x80000000 <= final_regs[0][29] < 0x80200000 and final_regs[0][29] % 8 == 0,
             "sound initializer caller frame/return address is not restored")
    end = next((line for line, pc, _ in parsed.cpu if pc == 0x80034CD0), -1)
    call_lines = [line for line, pc, _ in parsed.cpu if pc == 0x80043830]
    wait_lines = [line for line, pc, _ in parsed.cpu if pc == 0x80043938]
    flow = [(line, pc) for line, pc, _ in parsed.cpu if pc in (0x80043830, 0x80043938)]
    _require(errors, [pc for _, pc in flow] == [0x80043830, 0x80043938] * 6 and
             all(start < line < end for line, _ in flow),
             "TOC command/wait continuations are outside or out of source order")
    bank, parameters, queries = None, [], []
    previous_cycle = -1
    epoch_cycle = _decimal(parsed.sections.get("DEVICE_EPOCH", {}).get("cycles"))
    for line_number, line in enumerate(parsed.lines):
        if line_number <= start:
            continue
        fields = _fields(line)
        is_write = re.search(r"\bCD_WRITE\b", line) is not None
        is_response = re.search(r"\bCD_RESPONSE\b", line) is not None
        if is_write or is_response:
            cycle = _decimal(fields.get("cycle"))
            _require(errors, cycle is not None and cycle > previous_cycle and
                     epoch_cycle is not None and cycle <= epoch_cycle and line_number < end,
                     "TOC I/O cycles or source boundaries are out of order")
            if cycle is not None:
                previous_cycle = cycle
        if is_write:
            address, value, cycle = _hex(fields.get("address")), _hex(fields.get("value")), _decimal(fields.get("cycle"))
            if (address not in range(0x1F801800, 0x1F801804) or
                    value is None or not 0 <= value <= 255 or cycle is None):
                errors.append("malformed TOC CD write")
                continue
            if address == 0x1F801800:
                bank = value & 3
            elif address == 0x1F801802 and bank == 0:
                parameters.append(value)
            elif address == 0x1F801803 and bank == 1 and value & 0x40:
                parameters = []
            elif address == 0x1F801801 and bank == 0:
                index = len(queries)
                _require(errors, index < len(call_lines) and index < len(wait_lines) and
                         call_lines[index] < line_number < wait_lines[index],
                         "TOC command write is outside its source call/wait")
                queries.append((value, parameters, [], cycle))
                parameters = []
        elif is_response:
            value, cycle = _hex(fields.get("value")), _decimal(fields.get("cycle"))
            _require(errors, bool(queries) and value is not None and 0 <= value <= 255 and cycle is not None and
                     len(queries) <= len(wait_lines) and line_number < wait_lines[len(queries)-1] and
                     _hex(fields.get("address")) == 0x1F801801,
                     "TOC response has no command or malformed byte/cycle")
            if queries and value is not None and cycle is not None:
                queries[-1][2].append(value)
                _require(errors, cycle >= queries[-1][3] + 0x800,
                         "TOC response arrived before its command deadline")
    expected = [(0x13, [], [2, 1, 4]), (0x14, [0], [2, 0x39, 0x20]),
                (0x14, [1], [2, 0, 2]), (0x14, [2], [2, 0x34, 0x32]),
                (0x14, [3], [2, 0x35, 0x32]), (0x14, [4], [2, 0x36, 0x14])]
    _require(errors, not parameters and
             [(cmd, params, response) for cmd, params, response, _ in queries] == expected,
             "native TOC parameters/response packets differ from the mounted disc")
    rows = [_fields(line) for line in parsed.lines if "SOUND_TOC " in line]
    _require(errors, [_hex(row.get("pc")) for row in rows] == [0x80034CD0, 0x800101FC],
             "TOC RAM snapshots are missing or duplicated")
    for row in rows:
        raw = row.get("bytes", "")
        _require(errors, row.get("valid") == "1" and _decimal(row.get("tracks")) == 4 and
                 _hex(row.get("address")) == 0x800C7D30 and re.fullmatch(r"[0-9a-fA-F]{40}", raw) is not None and
                 raw.lower() == "3920000000020000343200003532000036140000",
                 "source TOC RAM does not contain four tracks and the lead-out record")
    cd = parsed.sections.get("CD_OWNER", {})
    _require(errors, _hex(cd.get("command")) == 0x14 and cd.get("phase") == "0" and
             _hex(cd.get("if")) == 0 and _hex(cd.get("enable")) == 7 and
             cd.get("fifo") == "0" and _decimal(cd.get("irq_requests")) == 10,
             "CD owner did not settle all ten startup/TOC interrupts")
    irq_rows = [_fields(line) for line in parsed.lines if "bios_cd_installed=" in line]
    _require(errors, len(irq_rows) == 1 and irq_rows[0].get("irq_faulted") == "0" and
             irq_rows[0].get("guest_irq_enabled") == "1" and
             irq_rows[0].get("bios_event_used") == "9" and irq_rows[0].get("bios_cd_installed") == "0",
             "sound return BIOS/IRQ ownership is missing or faulted")
    deliveries = _decimal(parsed.sections.get("STOP", {}).get("deliveries"))
    _require(errors, parsed.sections.get("STOP", {}).get("IRQ_installed") == "1" and
             deliveries is not None and deliveries > 10,
             "sound return lacks installed IRQ delivery evidence for CD and VBlank")
    reverb = [_fields(line) for line in parsed.lines if "SPU_REVERB " in line and
              _hex(_fields(line).get("pc")) == 0x800101FC]
    _require(errors, len(reverb) == 1,
             "outer sound return has no final reverb snapshot")
    if reverb:
        writes = _decimal(reverb[0].get("ram_writes"))
        _require(errors, _hex(reverb[0].get("base")) == 0xF204 and
                 reverb[0].get("enabled") == "1" and writes is not None and writes > 0 and writes % 4 == 0,
                 "outer sound return has no actual enabled reverb feedback work")
        row = reverb[0]
        previous = [_fields(line) for line in parsed.lines if "SPU_REVERB " in line and
                    _hex(_fields(line).get("pc")) == 0x8002C98C]
        frames, left, right = (_decimal(row.get(k)) for k in ("frames", "processed_left", "processed_right"))
        previous_frames = _decimal(previous[0].get("frames")) if len(previous) == 1 else None
        cursor = _hex(row.get("cursor"))
        _require(errors, frames is not None and previous_frames is not None and epoch_cycle is not None and
                 previous_frames <= frames <= epoch_cycle // 768 and
                 left is not None and right is not None and left == frames // 2 and
                 right == max(frames - 1, 0) // 2 and
                 _decimal(row.get("next_channel")) == max(frames - 1, 0) % 2 and
                 cursor is not None and 0x79020 <= cursor < 0x80000 and cursor % 2 == 0,
                 "outer reverb phase/cursor does not match the shared sample clock")
        _require(errors, _hex(row.get("wet_left")) == _hex(row.get("wet_right")) == 0 and
                 _hex(row.get("eon")) == 0 and _decimal(row.get("bootstrap_halfwords")) == 32768 and
                 [_hex(v) for v in row.get("coefficients", "").split(",")] == _REVERB_COEFFICIENTS,
                 "outer reverb registers/clear provenance changed unexpectedly")
        for key in ("ram_reads", "ram_writes", "unknown_reads", "unsupported_steps"):
            value = _decimal(row.get(key))
            before = _decimal(previous[0].get(key)) if len(previous) == 1 else None
            _require(errors, value is not None and before is not None and value >= before >= 0,
                     f"outer reverb {key} is malformed or regressed")
        # The real owner has 39 FIR taps. Unknown earlier passive history is
        # retained evidence, not a fault or permission to invent known zeros.
        for key in ("unknown_history_left", "unknown_history_right"):
            value = _decimal(row.get(key))
            _require(errors, value is not None and 0 <= value <= 39,
                     f"outer reverb {key} is malformed")


def validate_spu_boot_log(text: str, *, common_attributes: bool = False, reverb: bool = False, sound_outer: bool = False) -> list[str]:
    """Return source/owner violations; reverb also requires observed RAM work."""
    parsed = parse_spu_boot_log(text)
    errors = list(parsed.parse_errors)
    reverb = reverb or sound_outer
    common_attributes = common_attributes or reverb
    common_boundary = 0x8002C974
    boundary = 0x8001971C if sound_outer else 0x8002C98C if reverb else common_boundary if common_attributes else 0x8002C90C

    observed = [pc for _, pc, _ in parsed.cpu if pc in _MILESTONES]
    _require(errors, observed == _MILESTONES,
             "source milestone order is not 2C8F4, 3A424, 2C904, 3A424, 3D530, 2C90C")

    first_spu_line = min((line for line, address, _, _ in parsed.spu_writes
                          if address in (_SPU_DA6, _SPU_DA8)), default=None)
    cd_returns = [(line, regs) for line, pc, regs in parsed.cpu
                  if pc == 0x800101EC and regs is not None and
                  len(regs) == 32 and regs[2] == 1]
    _require(errors, bool(cd_returns),
             "first CD return checkpoint 800101EC with v0=1 is missing")
    if first_spu_line is not None and cd_returns:
        _require(errors, cd_returns[0][0] < first_spu_line,
                 "CD 800101EC/v0=1 evidence occurs after SPU activity")
    _require(errors, first_spu_line is not None,
             "no SPU MMIO writes were observed")

    upload_ops = [(address, value) for _, address, value, _ in parsed.spu_writes
                  if address in (_SPU_DA6, _SPU_DA8)]
    expected_upload = ([( _SPU_DA6, 0x0200)] + [(_SPU_DA8, 0x0707)] * 8 +
                       [(_SPU_DA6, 0x0200)] + [(_SPU_DA8, 0x0707)] * 8)
    _require(errors, upload_ops == expected_upload,
             "SPU upload sequence is not two DA6=0200 plus eight DA8=0707 groups")
    upload_cycles = [cycle for _, address, value, cycle in parsed.spu_writes
                     if address in (_SPU_DA6, _SPU_DA8)]
    _require(errors, all(left < right for left, right in
                         zip(upload_cycles, upload_cycles[1:])),
             "SPU upload cycles are not strictly increasing")

    stages = [row for row in parsed.spu_stages
              if _hex(row.get("pc")) in (0x8002C8F4, 0x8002C904, 0x8002C90C)]
    _require(errors, [_hex(row.get("pc")) for row in stages] ==
             [0x8002C8F4, 0x8002C904, 0x8002C90C],
             "SPU stage snapshots do not cover the two calls and 2C90C in order")
    _require(errors, [_integer(row.get("manual_halfwords")) for row in stages] ==
             [0, 8, 16],
             "SPU stage manual-halfword counts are not 0, then 8, then 16")
    expected_stage_main = [0x7FFE, 0x0000, 0x7FFE]
    expected_stage_control = [0xC001, 0xC000, 0xC000]
    expected_stage_cursor = [0x00000, 0x01010, 0x01010]
    for index, row in enumerate(stages[:3]):
        cycle = _section_int(row, "cycle")
        _require(errors, cycle is not None and cycle >= 0 and
                 _section_int(row, "samples") == cycle // 768,
                 "SPU stage sample count does not follow its cycle")
        _require(errors, row.get("faulted") == "0" and
                 row.get("manual_policy") == "SYNCHRONOUS_SOURCE_MODEL" and
                 row.get("key_policy") == "POST_FRAME" and
                 row.get("hardware_subphase") == "UNPROVEN" and
                 _hex(row.get("main_left")) == expected_stage_main[index] and
                 _hex(row.get("main_right")) == expected_stage_main[index] and
                 _hex(row.get("control")) == expected_stage_control[index] and
                 _hex(row.get("cursor")) == expected_stage_cursor[index],
                 "SPU stage snapshot is missing a healthy source-backed owner field")
        if index == 0:
            _require(errors, _hex(row.get("transfer_address")) == 0 and
                     _hex(row.get("event_guard")) == 0 and
                     _hex(row.get("event_handle")) == 0,
                     "pre-call SPU stage is not an empty owner state")
        else:
            _require(errors, _hex(row.get("transfer_address")) == 0x200 and
                     _hex(row.get("event_guard")) == 1 and
                     _hex(row.get("event_handle")) not in (None, 0),
                     "post-call SPU stage is missing transfer/event state")
    stage_cycles = [_section_int(row, "cycle") for row in stages]
    _require(errors, all(cycle is not None for cycle in stage_cycles) and
             all(left < right for left, right in zip(stage_cycles, stage_cycles[1:])),
             "SPU stage cycles are not strictly increasing")
    if len(stages) == 3:
        _require(errors, stages[1].get("event_handle") == stages[2].get("event_handle"),
                 "post-call SPU stages do not retain the same event handle")

    if common_attributes:
        common_pcs = [0x8002C90C, 0x8003C598, common_boundary]
        _require(errors, [pc for _, pc, _ in parsed.cpu if pc in common_pcs] == common_pcs,
                 "common attribute call/return order is missing or duplicated")
        common_rows = [row for row in parsed.spu_stages if _hex(row.get("pc")) == common_boundary]
        _require(errors, len(common_rows) == 1,
                 "common attribute stage is missing or duplicated")
        if common_rows:
            row = common_rows[0]
            cycle = _section_int(row, "cycle")
            _require(errors, cycle is not None and stage_cycles and
                     stage_cycles[-1] is not None and cycle > stage_cycles[-1] and
                     _section_int(row, "samples") == cycle // 768,
                     "common attribute stage cycle/sample count is invalid")
            stage_cycles.append(cycle)
            expected = {"main_left": 0x7FFE, "main_right": 0x7FFE,
                        "control": 0xC001, "cursor": 0x1010,
                        "transfer_address": 0x200, "transfer_control": 4,
                        "event_guard": 1}
            _require(errors, all(_hex(row.get(key)) == value for key, value in expected.items()) and
                     _integer(row.get("manual_halfwords")) == 16 and row.get("faulted") == "0" and
                     len(stages) == 3 and row.get("event_handle") == stages[-1].get("event_handle") and
                     row.get("manual_policy") == "SYNCHRONOUS_SOURCE_MODEL" and
                     row.get("key_policy") == "POST_FRAME" and row.get("hardware_subphase") == "UNPROVEN",
                     "common attribute stage owner/transfer/event state is invalid")
        start = next((line for line, pc, _ in parsed.cpu if pc == 0x8002C90C), -1)
        end = next((line for line, pc, _ in parsed.cpu if pc == common_boundary), -1)
        writes = [(address, value) for line, address, value, _ in parsed.spu_writes if line > start and (not reverb or line < end)]
        _require(errors, writes == [(0x1F801D80, 0x3FFF), (0x1F801D82, 0x3FFF),
                                    (0x1F801DB0, 0x3FFF), (0x1F801DB2, 0x3FFF),
                                    (0x1F801DAA, 0xC000), (0x1F801DAA, 0xC001),
                                    (0x1F801DAA, 0xC001)] and
                 all(line < end for line, _, _, _ in parsed.spu_writes if line > start and (not reverb or line < end)),
                 "common attribute source does not issue exactly seven ordered writes before return")
        write_cycles = [cycle for line, _, _, cycle in parsed.spu_writes if line > start and (not reverb or line < end)]
        _require(errors, all(a < b for a, b in zip(write_cycles, write_cycles[1:])) and
                 len(stage_cycles) == 4 and stage_cycles[-2] is not None and stage_cycles[-1] is not None and
                 all(stage_cycles[-2] < cycle < stage_cycles[-1] for cycle in write_cycles),
                 "common attribute write cycles are outside the source stages")
        common_ram = [row for row in parsed.spu_ram if _hex(row.get("pc")) == common_boundary]
        _require(errors, len(common_ram) == 1 and _hex(common_ram[0].get("offset")) == 0x1000 and
                 common_ram[0].get("valid") == "1" and common_ram[0].get("bytes", "").lower() == "07" * 16,
                 "common attribute stage lost the actual SPU upload")

    if reverb:
        _validate_reverb(parsed, errors, stage_cycles, continuing=sound_outer)
    if sound_outer:
        _validate_sound_outer(parsed, errors)

    ram_rows = [row for row in parsed.spu_ram if _hex(row.get("pc")) == 0x8002C90C]
    _require(errors, len(ram_rows) == 1,
             "final SPU RAM snapshot is missing or duplicated")
    if ram_rows:
        ram = ram_rows[0]
        _require(errors, _hex(ram.get("offset")) == 0x1000 and
                 ram.get("valid") == "1" and
                 ram.get("bytes", "").lower() == "07" * 16,
                 "final SPU RAM does not contain the sixteen uploaded 07 bytes")

    voices = [row for row in parsed.spu_voices
              if row.get("pc") is None or _hex(row.get("pc")) == 0x8002C90C]
    voice_indices = [_integer(row.get("index")) for row in voices]
    _require(errors, len(voices) == 24 and
             all(index is not None for index in voice_indices) and
             sorted(voice_indices) == list(range(24)),
             "final SPU voice snapshot does not contain all 24 indexed voices")
    expected_regs = [0x0000, 0x0000, 0x1000, 0x0200,
                     0x80FF, 0x4000, 0x0000, 0x0000]
    for voice in voices:
        envelope = _hex(voice.get("envelope"))
        regs = voice.get("regs", "").split(",")
        actual_regs = [_hex(value) for value in regs]
        _require(errors, envelope == 0 and _integer(voice.get("phase")) == 0 and
                 _integer(voice.get("pending_on")) == 0 and
                 _integer(voice.get("pending_off")) == 0 and
                 (_integer(voice.get("blocks")) or 0) > 0 and
                 (_integer(voice.get("decoded")) or 0) > 0 and
                 len(actual_regs) == 8 and
                 actual_regs == expected_regs,
                 "final SPU voice envelope/register snapshot is not source-backed")

    event_rows = _source_event_open(parsed.events_open)
    event_owner = parsed.sections.get("EVENT_OWNER", {})
    if parsed.events_open:
        _require(errors, len(event_rows) == 1,
                 "source F0000009 event was not opened exactly once")
        handle = event_rows[0].get("handle") if event_rows else None
        enabled = [row for row in parsed.events_enable
                   if row.get("result") in ("1", "00000001") and
                   (handle is None or row.get("handle") == handle)]
        _require(errors, len(enabled) == 1,
                 "source event was not enabled exactly once")
    else:
        _require(errors, _decimal(event_owner.get("opens")) == 1 and
                 _decimal(event_owner.get("enables")) == 1 and
                 _hex(event_owner.get("class")) == 0xF0000009,
                 "source event summary does not show one F0000009 open/enable")

    dma_rows = _source_dma_register(parsed.dma_registers)
    dma_owner = parsed.sections.get("DMA_OWNER", {})
    if parsed.dma_registers:
        _require(errors, len(dma_rows) == 1 and len(parsed.dma_registers) == 1,
                 "SPU DMA callback registration was not performed exactly once")
    else:
        _require(errors, _decimal(dma_owner.get("registrations")) == 1 and
                 _hex(dma_owner.get("callback")) == 0x8003AA18,
                 "DMA registration summary does not show the source callback once")

    source = parsed.sections.get("SOURCE_CLOCK", {})
    epoch = parsed.sections.get("DEVICE_EPOCH", {})
    spu = parsed.sections.get("SPU_OWNER", {})
    stop = parsed.sections.get("STOP", {})
    owned = parsed.sections.get("OWNED_DEVICES", {})
    source_cycle = _section_int(source, "cycles")
    epoch_cycle = _section_int(epoch, "cycles")
    spu_cycle = _section_int(spu, "cycle")
    _require(errors, source.get("faulted") == "0" and epoch.get("faulted") == "0" and
             source_cycle is not None and source_cycle > 0 and
             epoch_cycle is not None and epoch_cycle > 0 and
             source_cycle <= epoch_cycle,
             "common source/device epoch is missing or faulted")
    _require(errors, epoch_cycle is not None and
             all(cycle is not None and cycle <= epoch_cycle for cycle in stage_cycles),
             "SPU stage cycle is after the final device epoch")
    spu_samples = _integer(spu.get("samples"))
    spu_submitted = _integer(spu.get("submitted"))
    _require(errors, epoch_cycle is not None and spu.get("faulted") == "0" and
             spu_cycle == epoch_cycle and
             spu_samples is not None and spu_samples > 0 and
             spu_samples == spu_submitted == epoch_cycle // 768 and
             _hex(spu.get("main_left")) == 0x7FFE and
             _hex(spu.get("main_right")) == 0x7FFE and
             _hex(spu.get("cd_left")) == (0x3FFF if common_attributes else 0) and
             _hex(spu.get("cd_right")) == (0x3FFF if common_attributes else 0) and
             _hex(spu.get("control")) == (0xC081 if reverb else 0xC001 if common_attributes else 0xC000),
             "SPU owner is missing, faulted, off epoch, or has incoherent final state")

    cd = parsed.sections.get("CD_OWNER", {})
    _require(errors, cd.get("fault") == "0" and
             _section_int(cd, "cycle") == epoch_cycle,
             "CD owner is missing, faulted, or off the common device epoch")
    cd_irq = parsed.sections.get("CD_IRQ", {})
    _require(errors, cd_irq.get("frame_busy") == "0" and
             cd_irq.get("frame_faulted") == "0" and
             (_integer(cd_irq.get("instructions")) or 0) > 0 and
             cd_irq.get("call") == "ffffffff" and cd_irq.get("target") == "ffffffff",
             "CD IRQ callback frame is missing or faulted")
    sio = parsed.sections.get("SIO", {})
    _require(errors, sio.get("bound") == "1" and sio.get("faulted") == "0" and
             _section_int(sio, "cycle") == epoch_cycle,
             "SIO owner is missing, faulted, or off the common device epoch")
    video = parsed.sections.get("VIDEO_TIMING", {})
    _require(errors, video.get("running") == "1" and video.get("faulted") == "0" and
             _section_int(video, "timer1_cycle") == epoch_cycle,
             "Timer1/VBlank owner is missing, faulted, or off the common device epoch")
    input_owner = parsed.sections.get("INPUT_OWNER", {})
    _require(errors, input_owner.get("installed") == "1" and
             input_owner.get("faulted") == "0" and
             input_owner.get("frame_busy") == "0" and
             input_owner.get("frame_faulted") == "0",
             "input owner is missing or faulted")

    _require(errors, stop.get("status") == "0" and
             _hex(stop.get("pc")) == boundary and
             _hex(stop.get("target")) == boundary,
             f"native startup did not stop cleanly at {boundary:08X}")
    for marker in ("scheduler_removed", "card_removed", "bu_removed",
                   "input_removed", "gpu_removed"):
        _require(errors, re.search(rf"\b{marker}=1\b", text) is not None,
                 f"cleanup marker {marker}=1 is missing")
    _require(errors, owned.get("removed") == "1",
             "owned device teardown marker is missing")
    _require(errors, "scheduler removal refused" not in text and
             "SPU removal refused" not in text,
             "owner cleanup reported a refusal")
    return errors


def run_live(binary: Path, exe: Path, cue: Path, image: Path, *, common_attributes: bool = False, reverb: bool = False, sound_outer: bool = False) -> int:
    xvfb = shutil.which("xvfb-run")
    if not xvfb:
        print("mounted native SPU: no isolated display; NOT_RUN", file=sys.stderr)
        return 77
    with tempfile.TemporaryDirectory(prefix="musashi-mounted-spu-check-") as temp:
        cwd = Path(temp)
        env = os.environ.copy()
        env["SDL_VIDEODRIVER"] = "x11"
        runtime = Path(f"/run/user/{os.getuid()}")
        if (runtime / "pipewire-0").exists():
            env["XDG_RUNTIME_DIR"] = str(runtime)
        errors = _negative_inputs(binary, exe, cue, image, cwd, env)
        if errors:
            print("mounted native SPU: " + "; ".join(errors), file=sys.stderr)
            return 1
        run = _run([xvfb, "-a", str(binary), str(exe), str(cue), str(image)],
                   cwd=cwd, env=env)
        output = run.stdout + run.stderr
        if run.returncode == 77:
            print(output, end="", file=sys.stderr)
            print("mounted native SPU: NOT_RUN (real device/display unavailable)",
                  file=sys.stderr)
            return 77
        errors = validate_spu_boot_log(output, common_attributes=common_attributes, reverb=reverb, sound_outer=sound_outer)
        if run.returncode != 2:
            errors.append(f"native binary returned {run.returncode}, expected bounded 2")
        if errors:
            print(output, end="", file=sys.stderr)
            print("mounted native SPU: " + "; ".join(errors), file=sys.stderr)
            return 1
        print(output, end="")
        boundary = "8001971C" if sound_outer else "8002C98C" if reverb else "8002C974" if common_attributes else "8002C90C"
        print(f"MOUNTED_NATIVE_SPU_CHECK: PASS evidence=LIVE_NATIVE boundary={boundary}")
        return 0


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    common_attributes = bool(args and args[0] == "--common-attributes")
    reverb = bool(args and args[0] == "--reverb")
    sound_outer = bool(args and args[0] == "--sound-outer")
    if common_attributes or reverb or sound_outer:
        args.pop(0)
    if len(args) != 4:
        print("usage: native_spu_boot_check.py [--common-attributes|--reverb|--sound-outer] <binary> <pinned exe> <pinned cue> <pinned bin>",
              file=sys.stderr)
        return 2
    binary, exe, cue, image = (Path(value).resolve() for value in args)
    if sound_outer:
        return run_live(binary, exe, cue, image, sound_outer=True)
    if reverb:
        return run_live(binary, exe, cue, image, reverb=True)
    return run_live(binary, exe, cue, image, common_attributes=common_attributes)


if __name__ == "__main__":
    raise SystemExit(main())
