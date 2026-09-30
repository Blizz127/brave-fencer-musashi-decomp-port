"""Source ClearImage/validator/packet with declared fixture GPU register backend."""
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image
ROOT=Path(__file__).resolve().parents[1]

@pytest.mark.parametrize('variant',['baseline','packet_mutant','rgb_mutant','sanitizers'])
def test_clear_image_source(tmp_path,variant):
    exe=ROOT/'extracted/disc/files/SLUS_007.26'
    if not exe.exists():pytest.skip('pinned licensed EXE unavailable')
    raw=exe.read_bytes();assert hashlib.sha256(raw).hexdigest()=='66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    generated=tmp_path/'generated';generated.mkdir();_generate_formatter_includes(generated)
    for a,n in [(0x80059888,37),(0x80059760,74),(0x8005af68,151)]:
        words=re.findall(r'0x([0-9a-fA-F]{8})u',(generated/f'{a:08x}_words.inc').read_text());assert len(words)==n
        data=struct.pack(f'<{n}I',*(int(w,16) for w in words));offset=a-0x80010000+0x800
        assert raw[offset:offset+len(data)]==data
    if variant.endswith('mutant'):
        path=generated/('8005af68_words.inc' if variant=='packet_mutant' else '80059888_words.inc');text=path.read_text()
        old,new=('0x3c050200u','0x3c050300u') if variant=='packet_mutant' else ('0x02123825u','0x02123824u')
        assert text.count(old)==1;path.write_text(text.replace(old,new))
    flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if variant=='sanitizers' else ['-O2']
    binary=tmp_path/'probe'
    subprocess.run(['clang' if variant=='sanitizers' else 'cc','-std=c99',*flags,'-Wall','-Wextra','-Werror','-Wno-parentheses',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
        str(ROOT/'tests/clear_image_source_probe.c'),*[str(ROOT/'pc_port'/f) for f in ('boot_memory.c','gpu_controller.c','dma_controller.c','irq_controller.c','scanline_timer.c')],'-lcrypto','-o',str(binary)],check=True,timeout=90)
    _sync_code_image(generated)
    run=subprocess.run([str(binary),str(exe)],capture_output=True,text=True,timeout=30)
    if variant.endswith('mutant'):
        assert run.returncode!=0,'semantic mutant passed: '+variant
        expected='word(0x80078830+4*i)==packet[i]' if variant=='packet_mutant' else 'cpu.r[18]==color'
        assert expected in run.stderr,run.stderr
    else:
        assert run.returncode==0,run.stdout+run.stderr
        assert run.stdout=='CLEAR_IMAGE_SOURCE_PASS fixture_only=1 fill_and_dma=NOT_RUN\n'
