"""Whole-RAM independent DRAWENV/DISPENV oracle; never native lifecycle proof."""
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image
ROOT=Path(__file__).resolve().parents[1]

@pytest.mark.parametrize('variant',['baseline','pal_mutant','window_mutant','sanitizers'])
def test_draw_env_source(tmp_path,variant):
    exe=ROOT/'extracted/disc/files/SLUS_007.26'
    if not exe.exists():pytest.skip('pinned licensed EXE unavailable')
    raw=exe.read_bytes();assert hashlib.sha256(raw).hexdigest()=='66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    generated=tmp_path/'generated';generated.mkdir();_generate_formatter_includes(generated)
    for a,n in [(0x800147b8,92),(0x80058a4c,46),(0x80058b04,15),(0x80014960,14),(0x80014998,18),(0x80043078,4)]:
        words=re.findall(r'0x([0-9a-fA-F]{8})u',(generated/f'{a:08x}_words.inc').read_text());assert len(words)==n
        data=struct.pack(f'<{n}I',*(int(w,16) for w in words));offset=a-0x80010000+0x800
        assert raw[offset:offset+len(data)]==data
    if variant.endswith('mutant'):
        name,old,new=('80058a4c_words.inc','0x2a420121u','0x2a420101u') if variant=='pal_mutant' else ('800147b8_words.inc','0x24020100u','0x240200ffu')
        path=generated/name;text=path.read_text();assert text.count(old)==1;path.write_text(text.replace(old,new))
    binary=tmp_path/'probe';flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if variant=='sanitizers' else ['-O2']
    subprocess.run(['clang' if variant=='sanitizers' else 'cc','-std=c99',*flags,'-Wall','-Wextra','-Werror','-Wno-parentheses','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),str(ROOT/'tests/draw_env_source_probe.c'),str(ROOT/'pc_port/boot_memory.c'),'-o',str(binary)],check=True,timeout=90)
    _sync_code_image(generated)
    run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
    if variant.endswith('mutant'):
        assert run.returncode!=0,'semantic mutant passed: '+variant
        assert 'RAM_DIFF' in run.stderr,run.stderr
    else:
        assert run.returncode==0,run.stdout+run.stderr
        assert run.stdout=='DRAW_ENV_SOURCE_PASS fixture_only=1 gpu_submission=NOT_RUN\n'
