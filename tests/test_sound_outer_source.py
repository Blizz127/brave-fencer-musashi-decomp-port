"""Actual remaining sound outer source execution with synthetic CPU/owner state."""
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import tempfile
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]


def test_sound_outer_source():
    exe = ROOT / "extracted/disc/files/SLUS_007.26"
    if not exe.exists():
        pytest.skip("pinned licensed EXE unavailable")
    image = exe.read_bytes()
    assert hashlib.sha256(image).hexdigest() == (
        "66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a")
    with tempfile.TemporaryDirectory(prefix="musashi-sound-outer-source-") as temp:
        generated = Path(temp) / "generated"
        generated.mkdir()
        _generate_formatter_includes(generated)
        for address, count in (("8002c8f4",214), ("80037d98",66),
                               ("8003d424",4), ("8002cc4c",26),
                               ("8002fae0",10), ("8002c8bc",14),
                               ("80037cc8",4), ("8003be74",9),
                               ("8003be98",130), ("8003b1ec",25),
                               ("80034c24",51), ("800430b8",9),
                               ("800430dc",137), ("80043830",81),
                               ("80037004",9)):
            words = re.findall(r"0x([0-9a-fA-F]{8})u",
                               (generated / f"{address}_words.inc").read_text())
            assert len(words) == count
            actual = struct.pack(f"<{count}I", *(int(w, 16) for w in words))
            offset = int(address, 16) - 0x80010000 + 0x800
            assert image[offset:offset + len(actual)] == actual
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
            "-Wno-parentheses", "-DMUSASHI_NATIVE_WORD_CLEAR_BINDING",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "pc_port/include"),
            "-I", str(generated), str(ROOT / "tests/sound_outer_source_probe.c"),
            *[str(ROOT / "pc_port" / name) for name in (
                "boot_memory.c", "callback_bindings.c", "bios_events.c",
                "bios_kernel.c", "irq_controller.c", "dma_controller.c", "spu_cd_audio.c")],
            "-lcrypto", "-o", str(probe),
        ], check=True, timeout=30)
        _sync_code_image(generated)
        result = subprocess.run([str(probe), str(exe)], check=False, timeout=30,
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert result.stdout.count("OUTER_SITE ") == 12
        assert result.stdout.endswith("SOUND_OUTER_SOURCE_PASS fixture_only=1\n")
