"""Independent projection RAM oracle and real queue-call boundaries."""
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image
ROOT=Path(__file__).resolve().parents[1]

@pytest.mark.parametrize('variant',['baseline','matrix_mutant','div_mutant','sanitizers'])
def test_projection_source(tmp_path,variant):
    exe=ROOT/'extracted/disc/files/SLUS_007.26'
    if not exe.exists():pytest.skip('pinned licensed EXE unavailable')
    raw=exe.read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    generated=tmp_path/'generated';generated.mkdir();_generate_formatter_includes(generated)
    for address,count in [(0x8005283c,153),(0x80052d00,36),(0x80052bec,69)]:
        words=re.findall(r'0x([0-9a-fA-F]{8})u',(generated/f'{address:08x}_words.inc').read_text())
        assert len(words)==count
        data=struct.pack(f'<{count}I',*(int(w,16) for w in words));offset=address-0x80010000+0x800
        assert raw[offset:offset+len(data)]==data
    if variant.endswith('mutant'):
        path=generated/'8005283c_words.inc';text=path.read_text()
        old,new=('0x24021000u','0x240203e8u') if variant=='matrix_mutant' else ('0x0065001au','0x00a3001au')
        assert text.count(old)==1;path.write_text(text.replace(old,new))
    flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if variant=='sanitizers' else ['-O2']
    binary=tmp_path/'probe'
    subprocess.run(['clang' if variant=='sanitizers' else 'cc','-std=c99',*flags,
        '-Wall','-Wextra','-Werror','-Wno-parentheses','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
        str(ROOT/'tests/projection_source_probe.c'),str(ROOT/'pc_port/boot_memory.c'),'-o',str(binary)],check=True,timeout=90)
    _sync_code_image(generated)
    run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
    if variant.endswith('mutant'):assert run.returncode!=0,'semantic mutant passed: '+variant
    else:
        assert run.returncode==0,run.stdout+run.stderr
        assert run.stdout=='PROJECTION_SOURCE_PASS fixture_only=1 queue_completion=NOT_RUN\n'
