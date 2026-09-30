"""Formatter-only 143AC post-heap draw-entry fixture."""

from pathlib import Path
import hashlib
import re
import struct
import subprocess

import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "extracted/disc/files/SLUS_007.26"
EXPECTED_EXE_SHA256 = "66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a"
STREAMS = (
    (0x800143AC, 38,
     "87f91a48ce5a4e525177d3383cf8550f30513f3937c5364bad435e20b6b7df4d"),
    (0x800145EC, 49,
     "1afc947e5b8bce5fa35251d77b4a59b5cbfce016eb956bb0e7b2650ba34442f7"),
    (0x800525DC, 30,
     "6b0cb9d387877f05da9ede05a2c78b2951703859bc49fe7126c399525440e5f0"),
    (0x80052654, 92,
     "d30d8b4a5384f330ae301067bc22a8379f8e78e29978e59943224b052d638011"),
)


def test_draw_entry_streams_match_pinned_exe_and_execute(tmp_path):
    if not EXE.exists():
        pytest.skip("pinned licensed EXE unavailable")
    image = EXE.read_bytes()
    assert hashlib.sha256(image).hexdigest() == EXPECTED_EXE_SHA256
    generated = tmp_path / "generated"
    generated.mkdir()
    _generate_formatter_includes(generated)
    for address, count, raw_sha256 in STREAMS:
        words = re.findall(r"0x([0-9a-fA-F]{8})u",
                           (generated / f"{address:08x}_words.inc").read_text())
        assert len(words) == count
        encoded = struct.pack(f"<{count}I", *(int(word, 16) for word in words))
        assert hashlib.sha256(encoded).hexdigest() == raw_sha256
        offset = address - 0x80010000 + 0x800
        assert image[offset:offset + len(encoded)] == encoded
    probe = tmp_path / "probe"
    subprocess.run([
        "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
        "-Wno-parentheses", "-ffunction-sections", "-fdata-sections",
        "-Wl,--gc-sections", "-I", str(ROOT / "include"),
        "-I", str(ROOT / "pc_port/include"), "-I", str(generated),
        str(ROOT / "tests/draw_entry_probe.c"),
        str(ROOT / "pc_port/boot_memory.c"),
        "-lcrypto", "-o", str(probe),
    ], check=True, timeout=30)
    _sync_code_image(generated)
    run = subprocess.run([str(probe), str(EXE)], capture_output=True,
                         text=True, timeout=20)
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout == "DRAW_ENTRY_PASS fixture_only=1 mode0_boundary=1\n"
