#!/usr/bin/env python3
"""Check the bounded native GPU linked-list DMA/display boundary.

This checker verifies the actual six-word GP0 transfer, deferred DMA
completion, source refusal at GP1(05), owner health, and teardown. It does not
claim a reached menu, rendered output, or physical GPU timing parity.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ARTIFACT_DIR = REPO_ROOT / "artifacts/spu-startup-20260906/native"


def _fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line))


def _hex(value: str | None) -> int | None:
    if value is None or not re.fullmatch(r"[0-9A-Fa-f]+", value):
        return None
    try:
        return int(value, 16)
    except ValueError:
        return None


def _decimal(value: str | None) -> int | None:
    if value is None or not re.fullmatch(r"[0-9]+", value):
        return None
    try:
        return int(value, 10)
    except ValueError:
        return None


def _rows(lines: list[str], marker: str) -> list[dict[str, str]]:
    result = []
    for number, line in enumerate(lines):
        if marker in line:
            row = _fields(line)
            row["__line_no"] = str(number)
            result.append(row)
    return result


def _one(errors: list[str], rows: list[dict[str, str]], name: str) -> dict[str, str]:
    if len(rows) != 1:
        errors.append(f"{name} summary is missing or duplicated")
        return rows[-1] if rows else {}
    return rows[0]


def _require(errors: list[str], condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


def parse_gpu_dma_log(text: str) -> dict[str, Any]:
    lines = text.splitlines()
    return {
        "text": text,
        "lines": lines,
        "sources": _rows(lines, "DRAW_SOURCE "),
        "packets": _rows(lines, "DRAW_PACKET "),
        "devices": _rows(lines, "DRAW_DEVICE "),
        "queue_return": _rows(lines, "DRAW_QUEUE_RETURN "),
        "display_gp1": _rows(lines, "DISPLAY_GP1 "),
        "display_cache": _rows(lines, "DISPLAY_CACHE "),
        "dma": _rows(lines, "GPU_DMA2 stage="),
        "dma_removed": _rows(lines, "GPU_DMA2 removed="),
        "source_clock": _rows(lines, "SOURCE_CLOCK "),
        "epoch": _rows(lines, "DEVICE_EPOCH "),
        "cd": _rows(lines, "CD_OWNER "),
        "cd_irq": _rows(lines, "CD_IRQ "),
        "spu": _rows(lines, "SPU_OWNER "),
        "video": _rows(lines, "VIDEO_TIMING "),
        "sio": _rows(lines, "SIO profile="),
        "input_owner": _rows(lines, "INPUT_OWNER "),
        "draw_owner": _rows(lines, "DRAW_OWNER "),
        "stop": _rows(lines, " STOP status="),
        "boundary": _rows(lines, "CPU_BOUNDARY "),
        "gpu": _rows(lines, "GPUSTAT="),
        "gpu_env": _rows(lines, "GPU_ENV "),
        "gpu_env_backend": _rows(lines, "GPU_ENV_BACKEND "),
        "owned": _rows(lines, "OWNED_DEVICES "),
        "graphics": _rows(lines, "graphics_init="),
    }


def _check_dma(errors: list[str], parsed: dict[str, Any]) -> None:
    dma = parsed["dma"]
    stages = [row.get("stage") for row in dma]
    expected = ["START", "COMPLETE", "SOURCE_QUEUE_RETURN", "STOP"]
    _require(errors, stages == expected,
             "DMA stages must be unique and ordered START, COMPLETE, SOURCE_QUEUE_RETURN, STOP")
    if len(dma) != len(expected):
        return
    start, complete, returned, stop = dma
    start_cycle = _decimal(start.get("cycle"))
    due = _decimal(start.get("due"))
    headers = _decimal(start.get("headers"))
    payload = _decimal(start.get("payload"))
    _require(errors, start_cycle is not None and start_cycle > 0 and
             due is not None and headers is not None and payload is not None and
             due == start_cycle + 1 + headers + payload,
             "DMA due cycle is not start+1+headers+payload")
    _require(errors, start.get("readable") == "1" and
             start.get("starts") == "1" and start.get("transfers") == "0" and
             start.get("fetched") == "6" and start.get("accepted") == "6" and
             start.get("start_madr", "").lower() == "000a6454" and
             start.get("madr", "").lower() == "00ffffff" and
             start.get("bcr", "").lower() == "00000000" and
             start.get("chcr", "").lower() == "01000401" and
             start.get("nodes") == "1" and start.get("headers") == "1" and
             start.get("payload") == "6" and
             start.get("accepted_in_transfer") == "6" and
             start.get("fault") == "0",
             "DMA start does not describe the actual six-word transfer")
    for row, label in ((complete, "completion"), (returned, "source return"),
                       (stop, "stop")):
        cycle = _decimal(row.get("cycle"))
        _require(errors, cycle is not None and start_cycle is not None and cycle >= start_cycle,
                 f"DMA {label} cycle is invalid")
        _require(errors, row.get("transfers") == "1" and row.get("fetched") == "6" and
                 row.get("accepted") == "6" and row.get("madr", "").lower() == "00ffffff" and
                 row.get("chcr", "").lower() == "00000401" and row.get("fault") == "0",
                 f"DMA {label} does not retain completion state")
        _require(errors, row.get("nodes") == "1",
                 f"DMA {label} node count is inconsistent")
    _require(errors, all(row.get("nodes") == "1" for row in (start, complete, returned, stop)),
             "DMA node count is inconsistent across stages")
    complete_cycle = _decimal(complete.get("cycle"))
    _require(errors, complete_cycle is not None and due is not None and complete_cycle >= due,
             "DMA completion occurs before its deadline")
    cycles = [_decimal(row.get("cycle")) for row in (start, complete, returned, stop)]
    _require(errors, all(cycle is not None for cycle in cycles) and
             all(cycles[i] <= cycles[i + 1] for i in range(3)),
             "DMA stage cycles are not monotonic")
    invariant_keys = ("due", "starts", "headers", "payload", "accepted_in_transfer",
                      "start_madr", "bcr", "readable", "timing", "hardware_parity",
                      "fault", "fault_node", "fault_word")
    for key in invariant_keys:
        values = [row.get(key, "").lower() for row in (start, complete, returned, stop)]
        _require(errors, len(set(values)) == 1,
                 f"DMA {key} disagrees across transfer stages")
    line_no = lambda row: int(row.get("__line_no", "-1"))
    _require(errors, line_no(start) < line_no(complete) < line_no(returned) < line_no(stop),
             "DMA stage records are out of order")


def validate_gpu_dma_log(text: str) -> list[str]:
    parsed = parse_gpu_dma_log(text)
    errors: list[str] = []
    _check_dma(errors, parsed)

    sources = parsed["sources"]
    _require(errors, len(sources) == 3, "draw source checkpoints are missing or duplicated")
    if len(sources) >= 2:
        first, reset = sources[:2]
        _require(errors, all(_hex(first.get(k)) == v for k, v in {
            "pc": 0x800525DC, "a0": 0x140, "a1": 0xF0,
            "a2": 4, "a3": 0, "ra": 0x80014650,
        }.items()) and first.get("stack10") == "00000000" and
                 first.get("readable") == "1" and first.get("callback_guard") == "1",
                 "first draw source arguments or guard are invalid")
        _require(errors, _hex(reset.get("pc")) == 0x800526A8 and
                 _hex(reset.get("v0")) == 0 and
                 reset.get("callback_guard") == "1",
                 "second reset checkpoint did not retain guard=1/v0=0")
    if len(sources) == 3:
        _require(errors, _hex(sources[2].get("pc")) == 0x800526FC and
                 _hex(sources[2].get("v0")) == 0x800A6440,
                 "third source return checkpoint is missing")

    _require(errors, len(parsed["packets"]) == 1,
             "draw packet is missing or duplicated")
    if parsed["packets"]:
        packet = parsed["packets"][0]
        _require(errors, _hex(packet.get("address")) == 0x800A6454 and
                 _hex(packet.get("tag")) == 0x06FFFFFF and
                 _decimal(packet.get("words")) == 7 and packet.get("valid") == "1" and
                 packet.get("data") ==
                 "06ffffff,e3000000,e4000000,e5000000,e1000000,e2000000,e6000000",
                 "first GP0 packet is not the retail-backed six-command packet")

    devices = parsed["devices"]
    _require(errors, any(_hex(r.get("pc")) == 0x8005B720 and
                         _hex(r.get("address")) == 0x1F801814 and
                         _hex(r.get("v1")) == 0x04000002 for r in devices),
             "source GP1 DMA-direction write is missing")
    _require(errors, any(_hex(r.get("pc")) == 0x8005B730 and
                         _hex(r.get("address")) == 0x1F8010A0 and
                         _hex(r.get("a0")) == 0x800A6454 for r in devices),
             "source MADR write identity is missing")

    queue_return = _one(errors, parsed["queue_return"], "DRAW_QUEUE_RETURN")
    _require(errors, _hex(queue_return.get("pc")) == 0x80059DFC and
             _hex(queue_return.get("sp")) == 0x801FFEF8 and
             _hex(queue_return.get("ra")) == 0x80059DFC and
             _hex(queue_return.get("v0")) == 0 and
             _hex(queue_return.get("I_MASK")) == 0xD,
             "source queue return did not preserve v0=0 and I_MASK=D")
    display_rows = [row for row in parsed["display_gp1"]
                    if _hex(row.get("word")) == 0x05000000]
    display_gp1 = _one(errors, display_rows, "DISPLAY_GP1(05)")
    _require(errors, _hex(display_gp1.get("pc")) == 0x8005B690 and
             _hex(display_gp1.get("word")) == 0x05000000 and
             _hex(display_gp1.get("address")) == 0x1F801814 and
             display_gp1.get("cache_valid") == "1",
             "source GP1(05) packet identity is missing")

    cache_rows = [row for row in parsed["display_cache"]
                  if _hex(row.get("opcode")) == 0x05]
    display_cache = _one(errors, cache_rows, "DISPLAY_CACHE(05)")
    _require(errors, _hex(display_cache.get("address")) == 0x80078879 and
             display_cache.get("valid") == "1" and
             _hex(display_cache.get("value")) == _hex(display_gp1.get("cache_before")),
             "GP1(05) cache transition is missing or inconsistent")

    boundary = _one(errors, parsed["boundary"], "CPU_BOUNDARY")
    _require(errors, _hex(boundary.get("pc")) == 0x8005B690 and
             _hex(boundary.get("v0")) == 0x1F801814 and
             _hex(boundary.get("a0")) == 0x05000000 and
             _hex(boundary.get("ra")) == 0x8005A088 and
             _hex(boundary.get("sp")) == 0x801ffef0 and
             _hex(boundary.get("npc")) == 0x8005B694,
             "final GP1(05) refusal boundary is invalid")
    _require(errors, "EXECUTION_BOUNDARY reason=REFUSED" in parsed["text"],
             "source did not stop at the checked GP1 refusal")

    if sources and parsed["packets"] and parsed["dma"] and parsed["stop"]:
        gp1 = next((r for r in devices if _hex(r.get("pc")) == 0x8005B720), {})
        madr = next((r for r in devices if _hex(r.get("pc")) == 0x8005B730), {})
        _require(errors, int(sources[0]["__line_no"]) <
                 int(parsed["packets"][0]["__line_no"]) <
                 int(gp1.get("__line_no", "-1")) <
                 int(madr.get("__line_no", "-1")) <
                 int(parsed["dma"][0]["__line_no"]) <
                 int(queue_return.get("__line_no", "-1")) <
                 int(display_gp1.get("__line_no", "-1")) <
                 int(boundary.get("__line_no", "-1")) <
                 int(parsed["stop"][0]["__line_no"]),
                 "source, packet, DMA, refusal, and STOP records are out of order")

    owner = _one(errors, parsed["draw_owner"], "DRAW_OWNER")
    _require(errors, owner.get("dma_direction") == "2" and
             owner.get("chcr", "").lower() == "00000401" and
             owner.get("display_disabled") == "1" and
             owner.get("gpu_faulted") == "0" and owner.get("queue_valid") == "1" and
             owner.get("busy") == "00000001" and
             _hex(owner.get("saved_mask")) == 0xD and
             owner.get("history", "").lower() == "8005b710,800a6454,00000000",
             "GPU owner retained DMA direction/queue state is invalid")

    devices = parsed["devices"]
    expected_devices = [
        (0x8005B870, 0x1F8010A8, 0x00000000, 0x00000000),
        (0x8005B720, 0x1F801814, 0x800A6454, 0x04000002),
        (0x8005B730, 0x1F8010A0, 0x800A6454, 0x04000002),
    ]
    actual_devices = [(_hex(row.get("pc")), _hex(row.get("address")),
                       _hex(row.get("a0")), _hex(row.get("v1"))) for row in devices]
    _require(errors, actual_devices == expected_devices,
             "source DMA device sequence is missing, duplicated, or conflicting")

    source = _one(errors, parsed["source_clock"], "SOURCE_CLOCK")
    epoch = _one(errors, parsed["epoch"], "DEVICE_EPOCH")
    source_cycle = _decimal(source.get("cycles"))
    epoch_cycle = _decimal(epoch.get("cycles"))
    _require(errors, source.get("faulted") == "0" and
             source.get("devices_advanced") == "DEVICE_EPOCH" and
             source_cycle is not None and source_cycle > 0 and
             epoch.get("faulted") == "0" and epoch_cycle is not None and epoch_cycle > 0 and
             source_cycle <= epoch_cycle,
             "source/device epoch is missing or faulted")
    for rows, name, field in ((parsed["cd"], "CD_OWNER", "cycle"),
                              (parsed["spu"], "SPU_OWNER", "cycle"),
                              (parsed["video"], "VIDEO_TIMING", "timer1_cycle"),
                              (parsed["sio"], "SIO", "cycle")):
        row = _one(errors, rows, name)
        _require(errors, _decimal(row.get(field)) == epoch_cycle,
                 f"{name} cycle does not equal the final device epoch")

    cd = parsed["cd"][0] if parsed["cd"] else {}
    irq = _one(errors, parsed["cd_irq"], "CD_IRQ")
    spu = parsed["spu"][0] if parsed["spu"] else {}
    video = parsed["video"][0] if parsed["video"] else {}
    sio = parsed["sio"][0] if parsed["sio"] else {}
    _require(errors, cd.get("fault") == "0" and irq.get("frame_busy") == "0" and
             irq.get("frame_faulted") == "0" and
             spu.get("faulted") == "0" and video.get("faulted") == "0" and
             video.get("running") == "1" and sio.get("bound") == "1" and
             sio.get("faulted") == "0",
             "one or more retained owners is faulted or unfinished")
    input_rows = parsed["input_owner"]
    _require(errors, bool(input_rows) and all(row.get("faulted") == "0" and
             row.get("frame_faulted") == "0" and row.get("frame_busy") == "0"
             for row in input_rows) and input_rows[-1].get("installed") == "1",
             "final INPUT_OWNER health is missing or conflicting")

    gpu = _one(errors, parsed["gpu"], "GPU diagnostic")
    _require(errors, _hex(gpu.get("GPUSTAT")) is not None and
             gpu.get("GPUREAD", "").lower() == "00000400" and
             gpu.get("DMA2_CHCR", "").lower() == "00000401" and
             gpu.get("valid") == "1",
             "final GPU diagnostic is missing")
    status = _hex(gpu.get("GPUSTAT"))
    _require(errors, status is not None and (status & (1 << 23)) != 0 and
             ((status >> 29) & 3) == 2 and
             (status & ((1 << 26) | (1 << 28))) == ((1 << 26) | (1 << 28)) and
             (status & (1 << 25)) != 0,
             "GPUSTAT display/DMA flags are inconsistent with the retained owner")
    env = _one(errors, parsed["gpu_env"], "GPU_ENV")
    env_backend = _one(errors, parsed["gpu_env_backend"], "GPU_ENV_BACKEND")
    for row, name in ((env, "GPU_ENV"), (env_backend, "GPU_ENV_BACKEND")):
        _require(errors, row.get("window", "").lower() == "00000000" and
                 row.get("area_start", "").lower() == "00000000" and
                 row.get("area_end", "").lower() == "00000000" and
                 row.get("offset", "").lower() == "00000000" and
                 row.get("mask") == "0" and row.get("faulted") == "0",
                 f"{name} raw state is not the actual zero packet state")
    _require(errors, _decimal(env.get("gp0_accepted")) is not None and
             _decimal(env.get("gp0_accepted")) >= 6,
             "GPU accepted-word counter is below the six submitted commands")

    stop = _one(errors, parsed["stop"], "STOP")
    _require(errors, stop.get("status") == "0" and
             _hex(stop.get("pc")) == 0x8005B690 and
             _hex(stop.get("target")) == 0x8005B690 and
             stop.get("IRQ_installed") == "1" and
             stop.get("I_MASK", "").lower() == "000d" and
             stop.get("DPCR", "").lower() == "333bbb33" and
             stop.get("DICR", "").lower() == "00900000" and
             _hex(stop.get("I_STAT")) in (0, 1) and
             stop.get("irq_faulted", "0") == "0",
             "bounded stop identity or IRQ state is invalid")
    irq_faults = re.findall(r"\birq_faulted=([^\s]+)", parsed["text"])
    _require(errors, bool(irq_faults) and all(value == "0" for value in irq_faults),
             "final IRQ fault state is missing or conflicting")
    frame_busy = re.findall(r"\bframe_busy=([^\s]+)", parsed["text"])
    frame_faulted = re.findall(r"\bframe_faulted=([^\s]+)", parsed["text"])
    _require(errors, bool(frame_busy) and all(value == "0" for value in frame_busy) and
             bool(frame_faulted) and all(value == "0" for value in frame_faulted),
             "CD/input frame health is missing or conflicting")
    epoch_cycle = _decimal(epoch.get("cycles"))
    dma_stop_cycle = _decimal(parsed["dma"][-1].get("cycle")) if parsed["dma"] else None
    _require(errors, dma_stop_cycle is not None and dma_stop_cycle == epoch_cycle,
             "final DMA diagnostic cycle does not equal the device epoch")

    markers = [
        "scheduler_removed", "card_removed", "bu_removed", "input_removed",
        "gpu_removed", "GPU_DMA2 removed", "OWNED_DEVICES removed",
    ]
    for marker in markers:
        values = re.findall(rf"\b{re.escape(marker)}=([^\s]+)", parsed["text"])
        _require(errors, values == ["1"],
                 f"cleanup marker {marker}=1 is missing, duplicated, or conflicting")
    _require(errors, len(parsed["graphics"]) == 1 and
             "blank_presented=1" in parsed["text"],
             "graphics cleanup marker is missing or duplicated")
    menu_values = re.findall(r"\bmenu=([^\s]+)", parsed["text"])
    _require(errors, "startup=PARTIAL" in parsed["text"] and
             menu_values == ["NOT_REACHED"],
             "menu boundary is not explicitly NOT_REACHED")
    _require(errors, not re.search(r"GPU_COMMAND_CACHE|GPU_CACHE_STORE|command_cache.*05000000",
                                   parsed["text"], re.IGNORECASE),
             "fake GP1(05) command-cache storage was reported")
    return errors


def validate_gpu_dma_artifacts(artifact_dir: Path) -> list[str]:
    log_path = artifact_dir / "gpu-display.log"
    run_path = artifact_dir / "gpu-display-run.json"
    binary_path = artifact_dir / "gpu-display-native-boot"
    if not log_path.is_file() or not run_path.is_file() or not binary_path.is_file():
        return ["gpu-display retained artifact set is incomplete"]
    errors: list[str] = []
    try:
        metadata = json.loads(run_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"gpu-display run metadata is unreadable: {exc}"]
    if not isinstance(metadata, dict):
        return ["gpu-display run metadata is not an object"]
    _require(errors, metadata.get("exit") == 2 and metadata.get("menu") == "NOT_REACHED",
             "retained native metadata does not state exit=2/menu=NOT_REACHED")
    try:
        binary_hash = hashlib.sha256(binary_path.read_bytes()).hexdigest()
        log_hash = hashlib.sha256(log_path.read_bytes()).hexdigest()
        text = log_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        return [f"gpu-display retained artifact is unreadable: {exc}"]
    _require(errors, metadata.get("binary_sha256") == binary_hash,
             "native binary hash does not match supplied run JSON")
    _require(errors, metadata.get("log_sha256") == log_hash,
             "native log hash does not match supplied run JSON")
    errors.extend(validate_gpu_dma_log(text))
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_dir", nargs="?", type=Path,
                        default=DEFAULT_ARTIFACT_DIR)
    args = parser.parse_args(argv)
    errors = validate_gpu_dma_artifacts(args.artifact_dir.resolve())
    if errors:
        print("gpu-dma checker: " + "; ".join(errors), file=sys.stderr)
        return 1
    print("GPU_DMA_CHECK: PASS evidence=RETAINED_NATIVE "
          "menu=NOT_REACHED transfer=COMPLETED "
          "render=NOT_CLAIMED hardware_parity=UNPROVEN")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
