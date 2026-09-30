"""Exact display source and checked first-GP1 refusal; no scanout success stub."""
from pathlib import Path
import hashlib
import subprocess
import pytest
from retail_local import asm_words, exe_words
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image
ROOT = Path(__file__).resolve().parents[1]
RANGES = [(0x80043078,0x80043088),(0x80059FC0,0x8005A468),(0x8005B684,0x8005B6AC)]


def licensed_exe():
    path = ROOT / 'extracted/disc/files/SLUS_007.26'
    if not path.exists():
        pytest.skip('pinned licensed EXE unavailable')
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest() == '66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    return path,data


def test_display_312_words_match_exe_and_asm():
    licensed_exe()
    asm = asm_words()
    count = 0
    for start,end in RANGES:
        words = exe_words(start,end)
        assert words == [asm[pc] for pc in range(start,end,4)]
        count += len(words)
    assert count == 312


def test_display_entry_source_and_pre_effect_guards(tmp_path):
    exe,_ = licensed_exe()
    generated = tmp_path/'generated';generated.mkdir();_generate_formatter_includes(generated)
    binary = tmp_path/'probe'
    subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror','-Wno-parentheses',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
        str(ROOT/'tests/display_entry_probe.c'),str(ROOT/'pc_port/boot_memory.c'),'-lcrypto','-o',str(binary)],
        check=True,timeout=40)
    _sync_code_image(generated)
    result = subprocess.run([str(binary),str(exe)],capture_output=True,text=True,timeout=15)
    assert result.returncode == 0,result.stdout+result.stderr
    assert result.stdout == 'DISPLAY_ENTRY_PASS synthetic_frame=1 gp1_05=REFUSED scanout=NOT_EXECUTED\n'
