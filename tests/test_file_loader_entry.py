from pathlib import Path
import hashlib
import re
import struct
import subprocess

import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]


def test_actual_file_loader_entry_and_cache_refresh(tmp_path):
    exe = ROOT / "extracted/disc/files/SLUS_007.26"
    if not exe.exists():
        pytest.skip("pinned licensed EXE unavailable")
    image = exe.read_bytes()
    assert hashlib.sha256(image).hexdigest() == "66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a"
    generated = tmp_path / "generated"
    generated.mkdir()
    _generate_formatter_includes(generated)
    for address, count in ((0x8001971C,133),(0x80019990,32),(0x80045374,179),
                           (0x80045660,184),(0x80045C94,27),(0x80043A18,65)):
        words = re.findall(r"0x([0-9a-fA-F]{8})u", (generated / f"{address:08x}_words.inc").read_text())
        assert len(words) == count
        data = struct.pack(f"<{count}I", *(int(w,16) for w in words))
        offset = address-0x80010000+0x800
        assert image[offset:offset+len(data)] == data
    probe = tmp_path / "probe"
    subprocess.run([
        "cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-Wno-parentheses",
        "-ffunction-sections","-fdata-sections","-Wl,--gc-sections",
        "-I",str(ROOT/"include"),"-I",str(ROOT/"pc_port/include"),"-I",str(generated),
        str(ROOT/"tests/file_loader_entry_probe.c"),str(ROOT/"pc_port/boot_memory.c"),
        "-lcrypto","-o",str(probe),
    ],check=True,timeout=30)
    _sync_code_image(generated)
    run = subprocess.run([str(probe),str(exe)],capture_output=True,text=True,timeout=10)
    assert run.returncode == 0, run.stdout+run.stderr
    assert run.stdout == "FILE_LOADER_ENTRY_PASS fixture_only=1 sector=16 transfer=NOT_RUN\n"
