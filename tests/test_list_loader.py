"""Actual VSync source waits for the real handler; bounded loader table dispatch."""
from pathlib import Path
import subprocess
import tempfile
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT=Path(__file__).resolve().parents[1]


def test_list_loader_dispatch_and_vsync_source():
    exe=ROOT/'extracted/disc/files/SLUS_007.26'
    if not exe.exists():
        pytest.skip('pinned licensed EXE unavailable')
    with tempfile.TemporaryDirectory(prefix='musashi-list-loader-') as temp:
        generated=Path(temp)/'generated';generated.mkdir()
        _generate_formatter_includes(generated)
        probe=Path(temp)/'probe'
        subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',
            '-Wno-parentheses','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            '-DMUSASHI_NATIVE_CALLBACK_HANDLER=1',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
            str(ROOT/'tests/list_loader_probe.c'),str(ROOT/'pc_port/boot_memory.c'),
            str(ROOT/'pc_port/libetc_vsync_handler.c'),'-lcrypto','-o',str(probe)],check=True,timeout=30)
        _sync_code_image(generated)
        subprocess.run([str(probe),str(exe)],check=True,timeout=15)
