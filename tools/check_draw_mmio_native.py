#!/usr/bin/env python3
"""Verify the retained bounded draw/MMIO native checkpoint.

This checker validates the recorded source arguments, packet, GPU register
boundary, owner snapshots, and teardown markers.  It reports the stopped
transfer as NOT_STARTED and keeps menu/render/retail parity outside the claim.
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
    if value is None:
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


def _require(errors: list[str], condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


def _line_with(lines: list[str], marker: str) -> list[dict[str, str]]:
    rows = []
    for line_no, line in enumerate(lines):
        if marker in line:
            row = _fields(line)
            row["__line_no"] = str(line_no)
            rows.append(row)
    return rows


def _last_line_with(lines: list[str], marker: str) -> dict[str, str]:
    rows = _line_with(lines, marker)
    return rows[-1] if rows else {}


def parse_draw_mmio_log(text: str) -> dict[str, Any]:
    """Parse only the explicit draw/owner records used by this checker."""
    lines = text.splitlines()
    return {
        "text": text,
        "lines": lines,
        "sources": _line_with(lines, "DRAW_SOURCE "),
        "packets": _line_with(lines, "DRAW_PACKET "),
        "devices": _line_with(lines, "DRAW_DEVICE "),
        "draw_owner": _last_line_with(lines, "DRAW_OWNER "),
        "draw_owner_rows": _line_with(lines, "DRAW_OWNER "),
        "source_clock": _last_line_with(lines, "SOURCE_CLOCK "),
        "source_clock_rows": _line_with(lines, "SOURCE_CLOCK "),
        "epoch": _last_line_with(lines, "DEVICE_EPOCH "),
        "epoch_rows": _line_with(lines, "DEVICE_EPOCH "),
        "cd_owner": _last_line_with(lines, "CD_OWNER "),
        "cd_owner_rows": _line_with(lines, "CD_OWNER "),
        "cd_irq": _last_line_with(lines, "CD_IRQ "),
        "cd_irq_rows": _line_with(lines, "CD_IRQ "),
        "spu_owner": _last_line_with(lines, "SPU_OWNER "),
        "spu_owner_rows": _line_with(lines, "SPU_OWNER "),
        "video": _last_line_with(lines, "VIDEO_TIMING "),
        "video_rows": _line_with(lines, "VIDEO_TIMING "),
        "sio": _last_line_with(lines, "SIO profile="),
        "sio_rows": _line_with(lines, "SIO profile="),
        "input_owner": _last_line_with(lines, "INPUT_OWNER "),
        "input_owner_rows": _line_with(lines, "INPUT_OWNER "),
        "stop": _last_line_with(lines, "STOP "),
        "stop_rows": _line_with(lines, "STOP "),
        "owned": _last_line_with(lines, "OWNED_DEVICES "),
        "owned_rows": _line_with(lines, "OWNED_DEVICES "),
        "cpu_boundary": _last_line_with(lines, "CPU_BOUNDARY "),
        "cpu_boundary_rows": _line_with(lines, "CPU_BOUNDARY "),
        "execution_boundary_rows": _line_with(lines, "EXECUTION_BOUNDARY "),
        "gpu": _line_with(lines, "GPUSTAT="),
    }


def validate_draw_mmio_log(text: str) -> list[str]:
    """Return violations for a retained draw/MMIO log."""
    parsed = parse_draw_mmio_log(text)
    errors: list[str] = []
    sources = parsed["sources"]
    packets = parsed["packets"]
    devices = parsed["devices"]

    for key, label in (
        ("draw_owner_rows", "DRAW_OWNER"),
        ("source_clock_rows", "SOURCE_CLOCK"),
        ("epoch_rows", "DEVICE_EPOCH"),
        ("cd_owner_rows", "CD_OWNER"),
        ("cd_irq_rows", "CD_IRQ"),
        ("spu_owner_rows", "SPU_OWNER"),
        ("video_rows", "VIDEO_TIMING"),
        ("sio_rows", "SIO"),
        ("stop_rows", "STOP"),
        ("owned_rows", "OWNED_DEVICES"),
        ("cpu_boundary_rows", "CPU_BOUNDARY"),
    ):
        _require(errors, len(parsed[key]) == 1,
                 f"{label} summary is missing or duplicated")
    _require(errors, len(parsed["input_owner_rows"]) == 3 and
             parsed["input_owner_rows"][0].get("installed") == "0" and
             all(row.get("installed") == "1"
                 for row in parsed["input_owner_rows"][1:]) and
             sum(row.get("at") == "8005b730"
                 for row in parsed["input_owner_rows"]) == 1,
             "INPUT_OWNER initialization/final summaries are conflicting")

    _require(errors, len(sources) == 3,
             "draw source checkpoints are missing or duplicated")
    if len(sources) >= 2:
        first, second = sources[:2]
        _require(errors, all(_hex(first.get(key)) == value for key, value in {
            "pc": 0x800525DC, "a0": 0x140, "a1": 0xF0,
            "a2": 4, "a3": 0, "ra": 0x80014650,
        }.items()), "first draw source arguments or return link are invalid")
        _require(errors, first.get("readable") == "1" and
                 first.get("callback_guard") == "1" and
                 first.get("stack10") == "00000000" and
                 _decimal(first.get("gpu_reset_sequence")) == 0,
                 "first draw source guard/reset state is invalid")
        sequences = [_decimal(row.get("gpu_reset_sequence")) for row in sources]
        _require(errors, _hex(second.get("pc")) == 0x800526A8 and
                 _hex(second.get("v0")) == 0 and
                 second.get("callback_guard") == "1" and
                 sequences[0] is not None and sequences[1] is not None and
                 sequences[1] > sequences[0],
                 "second draw reset checkpoint did not retain guard=1/v0=0")
        if len(sources) == 3:
            _require(errors, all(value is not None for value in sequences) and
                     sequences[0] <= sequences[1] <= sequences[2],
                     "draw GPU reset sequence is malformed or regresses")
    if len(sources) == 3:
        _require(errors, _hex(sources[2].get("pc")) == 0x800526FC and
                 _hex(sources[2].get("v0")) == 0x800A6440,
                 "draw source packet-return checkpoint is missing")

    _require(errors, len(packets) == 1,
             "first draw packet is missing or duplicated")
    if packets:
        packet = packets[0]
        _require(errors, _hex(packet.get("address")) == 0x800A6454 and
                 _hex(packet.get("tag")) == 0x06FFFFFF and
                 _hex(packet.get("words")) == 7 and packet.get("valid") == "1" and
                 packet.get("data") ==
                 "06ffffff,e3000000,e4000000,e5000000,e1000000,e2000000,e6000000",
                 "first source packet contents are not retail-backed")

    expected_devices = [
        (0x8005B870, 0x1F8010A8, 0x00000000, 0x00000000),
        (0x8005B720, 0x1F801814, 0x800A6454, 0x04000002),
        (0x8005B730, 0x1F8010A0, 0x800A6454, 0x04000002),
    ]
    actual_devices = [(_hex(row.get("pc")), _hex(row.get("address")),
                       _hex(row.get("a0")), _hex(row.get("v1")))
                      for row in devices]
    _require(errors, actual_devices == expected_devices,
             "source GPU/MADR device sequence or identities are invalid")
    boundary = parsed["cpu_boundary"]
    _require(errors, _hex(boundary.get("pc")) == 0x8005B730 and
             _hex(boundary.get("npc")) == 0x8005B734 and
             _hex(boundary.get("v0")) == 0x1F8010A0 and
             _hex(boundary.get("a0")) == 0x800A6454 and
             _hex(boundary.get("sp")) == 0x801FFED0 and
             _hex(boundary.get("ra")) == 0x8005B8C4,
             "CPU_BOUNDARY does not identify the refused MADR source call")
    line_no = lambda row: int(row.get("__line_no", "-1"))
    if sources and packets and devices and boundary and parsed["stop"]:
        _require(errors, line_no(sources[0]) < line_no(packets[0]) <
                 line_no(devices[0]) < line_no(boundary) <
                 line_no(parsed["stop"]),
                 "source, packet, device refusal, and STOP records are out of order")
    _require(errors, "EXECUTION_BOUNDARY reason=REFUSED" in parsed["text"] and
             "DRAW_TRANSFER" not in parsed["text"] and
             "TRANSFER_STARTED" not in parsed["text"],
             "draw transfer did not stop before a transfer began")

    owner = parsed["draw_owner"]
    _require(errors, owner.get("dma_direction") == "2" and
             _hex(owner.get("chcr")) == 0x401 and
             owner.get("display_disabled") == "1" and
             owner.get("gpu_faulted") == "0" and
             owner.get("queue_valid") == "1" and
             owner.get("busy") == "00000001" and
             _hex(owner.get("saved_mask")) == 0xD and
             owner.get("history") == "00000000,00000000,00000000",
             "draw owner retained DMA direction/queue state is invalid")

    source = parsed["source_clock"]
    epoch = parsed["epoch"]
    _require(errors, source.get("faulted") == "0" and
             epoch.get("faulted") == "0" and
             source.get("devices_advanced") == "DEVICE_EPOCH" and
             epoch.get("profile") == "PACED_HOST_TIME" and
             epoch.get("cd_advanced") == "1",
             "source/device epoch is missing or faulted")
    source_cycle = _decimal(source.get("cycles"))
    epoch_cycle = _decimal(epoch.get("cycles"))
    _require(errors, source_cycle is not None and source_cycle > 0 and
             epoch_cycle is not None and epoch_cycle > 0 and
             source_cycle <= epoch_cycle and
             _decimal(parsed["cd_owner"].get("cycle")) == epoch_cycle and
             _decimal(parsed["spu_owner"].get("cycle")) == epoch_cycle and
             _decimal(parsed["video"].get("timer1_cycle")) == epoch_cycle and
             _decimal(parsed["sio"].get("cycle")) == epoch_cycle,
             "owner clocks do not share the retained epoch")

    _require(errors, parsed["cd_owner"].get("fault") == "0" and
             parsed["cd_irq"].get("frame_faulted") == "0" and
             parsed["cd_irq"].get("frame_busy") == "0" and
             parsed["spu_owner"].get("faulted") == "0" and
             parsed["video"].get("faulted") == "0" and
             parsed["video"].get("running") == "1" and
             parsed["sio"].get("bound") == "1" and
             parsed["sio"].get("faulted") == "0" and
             parsed["input_owner"].get("installed") == "1" and
             parsed["input_owner"].get("faulted") == "0" and
             parsed["input_owner"].get("frame_busy") == "0" and
             parsed["input_owner"].get("frame_faulted") == "0",
             "one or more retained owners is faulted or unfinished")

    stop = parsed["stop"]
    _require(errors, stop.get("status") == "0" and
             _hex(stop.get("pc")) == 0x8005B730 and
             _hex(stop.get("target")) == 0x8005B730 and
             stop.get("IRQ_installed") == "1" and stop.get("I_STAT") == "0000" and
             stop.get("I_MASK") == "0000" and stop.get("DPCR") == "333bbb33" and
             stop.get("DICR") == "00900000" and stop.get("TIMER1_MODE") == "0507" and
             stop.get("custom_vblank") == "1",
             "bounded stop identity or IRQ state is invalid")

    owner_direction = _decimal(owner.get("dma_direction"))
    status = _hex(parsed["gpu"][0].get("GPUSTAT")) if parsed["gpu"] else None
    _require(errors, len(parsed["gpu"]) == 1 and status is not None and
             status & (1 << 23) and
             ((status >> 29) & 3) == owner_direction and
             parsed["gpu"][0].get("GPUREAD") == "00000400" and
             parsed["gpu"][0].get("DMA2_CHCR") == "00000401",
             "final GPU diagnostic is missing")
    _require(errors, "graphics_init=RETURNED blank_presented=1 gpu_removed=1" in parsed["text"],
             "graphics cleanup did not complete")
    for marker in ("scheduler_removed", "card_removed", "bu_removed",
                   "input_removed", "gpu_removed"):
        values = re.findall(rf"\b{marker}=([^\s]+)", parsed["text"])
        _require(errors, values == ["1"],
                 f"cleanup marker {marker}=1 is missing or duplicated")
    owned_values = re.findall(r"\bOWNED_DEVICES removed=([^\s]+)", parsed["text"])
    _require(errors, owned_values == ["1"],
             "cleanup marker OWNED_DEVICES removed=1 is missing or duplicated")
    _require(errors, "removal refused" not in parsed["text"] and
             "cleanup failed" not in parsed["text"],
             "an owner teardown refusal was reported")
    _require(errors, "startup=PARTIAL" in parsed["text"] and
             "menu=NOT_REACHED" in parsed["text"],
             "menu boundary is not explicitly NOT_REACHED")
    return errors


def validate_draw_mmio_artifacts(artifact_dir: Path) -> list[str]:
    """Verify log/run metadata and the native binary hash in one directory."""
    log_path = artifact_dir / "draw-mmio.log"
    run_path = artifact_dir / "draw-mmio-run.json"
    binary_path = artifact_dir / "draw-mmio-native-boot"
    errors: list[str] = []
    if not log_path.is_file() or not run_path.is_file() or not binary_path.is_file():
        return ["draw-mmio retained artifact set is incomplete"]
    try:
        metadata = json.loads(run_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"draw-mmio run metadata is unreadable: {exc}"]
    if not isinstance(metadata, dict):
        return ["draw-mmio run metadata is not an object"]
    _require(errors, metadata.get("exit") == 2,
             "retained native process did not exit with bounded status 2")
    _require(errors, metadata.get("menu") == "NOT_REACHED",
             "retained run metadata does not state menu=NOT_REACHED")
    try:
        actual_binary = hashlib.sha256(binary_path.read_bytes()).hexdigest()
        actual_log = hashlib.sha256(log_path.read_bytes()).hexdigest()
    except OSError as exc:
        errors.append(f"draw-mmio retained hash input is unreadable: {exc}")
    else:
        _require(errors, metadata.get("binary_sha256") == actual_binary,
                 "native binary hash does not match supplied run JSON")
        _require(errors, metadata.get("log_sha256") == actual_log,
                 "native log hash does not match supplied run JSON")
    try:
        text = log_path.read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"draw-mmio log is unreadable: {exc}")
    else:
        errors.extend(validate_draw_mmio_log(text))
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_dir", nargs="?", type=Path,
                        default=DEFAULT_ARTIFACT_DIR)
    args = parser.parse_args(argv)
    artifact_dir = args.artifact_dir.resolve()
    errors = validate_draw_mmio_artifacts(artifact_dir)
    if errors:
        print("draw-mmio checker: " + "; ".join(errors), file=sys.stderr)
        return 1
    print("DRAW_MMIO_CHECK: PASS evidence=RETAINED_NATIVE "
          "menu=NOT_REACHED transfer=NOT_STARTED "
          "render_parity=NOT_CLAIMED retail_parity=NOT_CLAIMED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
