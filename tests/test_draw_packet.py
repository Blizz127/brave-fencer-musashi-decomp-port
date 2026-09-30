"""Retail packet bytes and pure source construction; no GPU completion fixture."""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import pytest
from retail_local import asm_words, exe_words
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]
RANGES = [(0x80059D68,0x80059E6C),(0x8005A870,0x8005AB00),
          (0x8005AB00,0x8005AB58),(0x8005AB58,0x8005AC24),
          (0x8005AC24,0x8005ACF0),(0x8005ACF0,0x8005AD34),
          (0x8005AD34,0x8005ADB8),(0x8005B7B0,0x8005BA90),
          (0x8005B710,0x8005B75C),(0x8005C020,0x8005C054),
          (0x800426FC,0x80042718)]


def pinned_exe():
    path = ROOT/'extracted/disc/files/SLUS_007.26'
    if not path.exists():
        pytest.skip('pinned licensed EXE unavailable')
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest() == '66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    return path, data


def test_draw_ranges_match_exe_and_assembly():
    pinned_exe()
    assembly = asm_words()
    for start,end in RANGES:
        assert exe_words(start,end) == [assembly[pc] for pc in range(start,end,4)]
    # Generated queue stream is 184 words, while only 84 idle-path words map.
    assert (RANGES[7][1]-RANGES[7][0])//4 == 184


def test_draw_packet_source_and_dispatch_guards():
    exe,_ = pinned_exe()
    with tempfile.TemporaryDirectory(prefix='musashi-draw-packet-') as temp:
        generated = Path(temp)/'generated'; generated.mkdir()
        _generate_formatter_includes(generated)
        binary = Path(temp)/'probe'
        subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',
            '-Wno-parentheses','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
            str(ROOT/'tests/draw_packet_probe.c'),str(ROOT/'pc_port/boot_memory.c'),
            '-lcrypto','-o',str(binary)],check=True,timeout=40)
        _sync_code_image(generated)
        subprocess.run([str(binary),str(exe)],check=True,timeout=15)


def test_packet_builder_complete_control_flow():
    """[800553F4,80055D40) is one complete function: every branch, jump and
    jump-table target stays inside it, and its only call is 8005C604."""
    import struct
    from retail_local import exe
    start, end = 0x800553F4, 0x80055D40
    words = exe_words(start, end)
    assert len(words) == 595
    table = struct.unpack_from('<30I', exe(), 0x80073C20-0x80010000+0x800)
    assert all(start <= target < end and target % 4 == 0 for target in table)
    calls = set()
    for index,word in enumerate(words):
        pc = start+4*index
        op = word >> 26
        if op in (2,3):
            target = ((pc+4)&0xF0000000) | ((word&0x03FFFFFF)<<2)
            if op == 3:
                calls.add(target)
            else:
                assert start <= target < end
        elif op in (1,4,5,6,7):
            offset = word & 0xFFFF
            if offset & 0x8000:
                offset -= 0x10000
            assert start <= pc+4+4*offset < end
    assert calls == {0x8005C604}
    assert words[-2:] == [0x03E00008,0]
