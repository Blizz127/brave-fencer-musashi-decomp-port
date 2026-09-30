"""Synthetic EXE-backed CPU fixture; independent of native lifecycle evidence."""
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import tempfile
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]


def test_spu_startup_source_fixture():
    exe = ROOT / "extracted/disc/files/SLUS_007.26"
    if not exe.exists():
        pytest.skip("pinned licensed EXE unavailable")
    with tempfile.TemporaryDirectory(prefix="musashi-spu-startup-source-") as temp:
        generated = Path(temp) / "generated"
        generated.mkdir()
        _generate_formatter_includes(generated)
        image = exe.read_bytes()
        assert hashlib.sha256(image).hexdigest() == (
            "66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a")
        # Independently compare all 39 SPU streams (including full enclosing
        # exports) to the pinned EXE. This fixture exercises the first two
        # calls; the common-attributes fixture covers the next caller prefix.
        formatter = (ROOT / "pc_port/mips_formatter.c").read_text()
        # The formatter keeps only each stream's shape; its words come from
        # guest RAM (the helper regenerates them from the local EXE).
        streams = [a.lower() for a in re.findall(
            r"static const uint32_t kSpu([0-9A-F]{8})Words\[\d+\];", formatter)]
        assert len(streams) == 39
        for address in streams:
            text = (generated / f"{address}_words.inc").read_text()
            words = [int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{8})u", text)]
            actual = struct.pack(f"<{len(words)}I", *words)
            offset = int(address, 16) - 0x80010000 + 0x800
            assert actual == image[offset:offset + len(actual)], address
        probe = Path(temp) / "probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
            "-Wno-parentheses", "-DMUSASHI_NATIVE_WORD_CLEAR_BINDING", "-ffunction-sections", "-fdata-sections",
            "-Wl,--gc-sections", "-I", str(ROOT / "include"),
            "-I", str(ROOT / "pc_port/include"), "-I", str(generated),
            str(ROOT / "tests/spu_startup_source_probe.c"),
            *[str(ROOT / "pc_port" / name) for name in (
                "boot_memory.c", "callback_bindings.c", "bios_events.c", "bios_kernel.c",
                "irq_controller.c", "dma_controller.c", "spu_cd_audio.c")],
            "-lcrypto", "-o", str(probe),
        ], check=True, timeout=30)
        _sync_code_image(generated)
        subprocess.run([str(probe), str(exe)], check=True, timeout=30)
