"""Directory source fixtures; isolated parser boundary, never a fake CD return."""
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import tempfile
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image
ROOT = Path(__file__).resolve().parents[1]


def _run(mutation=None, compiler='cc', extra_flags=()):
    image_path=ROOT/'extracted/disc/files/SLUS_007.26'
    cue=ROOT/'extracted/disc/disc.cue'
    binary=ROOT/'extracted/disc/disc.bin'
    if not all(p.exists() for p in (image_path,cue,binary)):
        pytest.skip('pinned licensed media unavailable')
    image=image_path.read_bytes()
    asm_bytes={int(pc,16):bytes.fromhex(raw) for pc,raw in re.findall(
        r'/\*\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+\*/',
        (ROOT/'asm/main.s').read_text())}
    assert hashlib.sha256(image).hexdigest()=='66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    with tempfile.TemporaryDirectory(prefix='musashi-directory-') as temp:
        generated=Path(temp)/'generated'; generated.mkdir()
        _generate_formatter_includes(generated)
        for address,count in ((0x80045940,42),(0x800459e8,171),(0x8005c4dc,25),(0x80045640,8)):
            path=generated/f'{address:08x}_words.inc'
            words=re.findall(r'0x([0-9a-fA-F]{8})u',path.read_text())
            assert len(words)==count
            actual=struct.pack(f'<{count}I',*(int(w,16) for w in words))
            offset=address-0x80010000+0x800
            assert actual==image[offset:offset+len(actual)]
            comments=b''.join(asm_bytes[address+4*i] for i in range(count))
            assert actual==comments
            assert tuple(int(w,16) for w in words)==struct.unpack(f'<{count}I',comments)
        args=[]
        if mutation:
            pc,word=mutation
            path=generated/'800459e8_words.inc'
            text=path.read_text(); matches=list(re.finditer(r'0x[0-9a-fA-F]{8}u',text))
            match=matches[(pc-0x800459e8)//4]
            path.write_text(text[:match.start()]+f'0x{word:08x}u'+text[match.end():])
            args=['badlb',f'{pc:x}']
        probe=Path(temp)/'probe'
        result=subprocess.run([compiler,'-std=c99','-O2','-Wall','-Wextra','-Werror',
            '-Wno-parentheses',*extra_flags,'-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
            str(ROOT/'tests/directory_source_probe.c'),str(ROOT/'pc_port/boot_memory.c'),
            str(ROOT/'pc_port/disc_media.c'),'-lcrypto','-o',str(probe)],capture_output=True,text=True,timeout=60)
        assert result.returncode==0,result.stderr
        _sync_code_image(generated)
        result=subprocess.run([str(probe),str(image_path),str(cue),str(binary),*args],capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stdout+result.stderr
        return result.stdout


def test_directory_source():
    assert 'DIRECTORY_SOURCE_PASS fixture_only=1' in _run()


@pytest.mark.parametrize('mutation',[(0x80045b54,0x80643a1a),(0x80045b50,0x80633a1a)],ids=['wrong_rt_word','wrong_pc'])
def test_directory_lb_actual_fetched_word_refusal(mutation):
    assert 'DIRECTORY_BAD_LB_REFUSED' in _run(mutation)
