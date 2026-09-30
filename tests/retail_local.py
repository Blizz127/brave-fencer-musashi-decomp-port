"""Read retail words from the developer's LOCAL, gitignored extracts.

Retail code is never committed (tests/test_no_retail_words.py): tests that
need instruction words read the pinned EXE, the MAIN.CD overlay members and
the splat assembly the developer extracted from their own disc, and skip
cleanly when those files are absent.
"""
from __future__ import annotations

import functools
import hashlib
import json
import re
import struct
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
EXE_PATH = ROOT / "extracted/disc/files/SLUS_007.26"
EXE_SHA256 = "66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a"
ASM_PATH = ROOT / "asm/main.s"
EXE_BASE = 0x80010000 - 0x800  # file offset = vram - EXE_BASE


def _need(path: Path) -> Path:
    if not path.is_file():
        pytest.skip(f"local extract unavailable: {path.relative_to(ROOT)}")
    return path


@functools.lru_cache(maxsize=None)
def _exe() -> bytes:
    data = EXE_PATH.read_bytes()
    assert hashlib.sha256(data).hexdigest() == EXE_SHA256
    return data


def exe() -> bytes:
    """The pinned SLUS_007.26 (sha256-checked)."""
    _need(EXE_PATH)
    return _exe()


def exe_raw(start: int, end: int) -> bytes:
    return exe()[start - EXE_BASE:end - EXE_BASE]


def exe_words(start: int, end: int) -> list[int]:
    raw = exe_raw(start, end)
    return list(struct.unpack(f"<{len(raw) // 4}I", raw))


@functools.lru_cache(maxsize=None)
def _asm() -> dict[int, int]:
    return {int(address, 16): int.from_bytes(bytes.fromhex(raw), "little")
            for address, raw in re.findall(
                r"/\*\s+[0-9A-F]+\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s+\*/",
                ASM_PATH.read_text())}


def asm_words() -> dict[int, int]:
    """vram -> word from the local asm/main.s disassembly comments."""
    _need(ASM_PATH)
    return _asm()


def overlay(member: str) -> bytes:
    """A local extracted overlay member, e.g. overlay('main/0010')."""
    return _need(ROOT / f"extracted/overlays/{member}.bin").read_bytes()


def manifest(name: str) -> dict:
    """A local (gitignored) artifacts/ manifest."""
    return json.loads(_need(ROOT / "artifacts" / name).read_text())


def check_published_ranges(manifest_name: str, expected: dict, total: int) -> None:
    """EXE == asm for each pinned range, and the manifest records its digest."""
    doc = manifest(manifest_name)
    asm = asm_words()
    assert doc["status"] == "SOURCE_EXPORTS_PUBLISHED"
    assert doc["total_words"] == total
    assert doc["c_match_claim"] is False
    assert doc["licensed_payload_copied"] is False
    for start, end, count in expected.values():
        assert end - start == 4 * count
        raw = exe_raw(start, end)
        assert list(struct.unpack(f"<{count}I", raw)) == [
            asm[pc] for pc in range(start, end, 4)]
        entry = next(item for item in doc["exports"]
                     if item["address"] == f"{start:08X}")
        assert entry["end"] == f"{end:08X}"
        assert entry["words"] == count
        assert entry["raw_sha256"] == hashlib.sha256(raw).hexdigest()
        assert entry["c_match_claim"] is False
        for flag in ("asm_match", "pinned_exe_match"):
            if flag in entry:
                assert entry[flag] is True
