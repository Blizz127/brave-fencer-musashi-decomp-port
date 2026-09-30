"""LIST source IRQ fixture; no real PsyCross worker or cold boot claim."""
from pathlib import Path
import subprocess
import sys
import tempfile
import os
import pytest
import hashlib
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_bios_event_callbacks import _generate_formatter_includes


def _title_sector_reference(binary):
    """Independent token decoder; unknown dictionary history is an error."""
    with binary.open('rb') as stream:
        stream.seek(6163 * 2352 + 24)
        data = stream.read(2048)
    ring = [None] * 1024
    cursor, offset = 1, 0
    decoded = bytearray()
    while offset < len(data):
        flags = data[offset]
        offset += 1
        for bit in range(8):
            if flags & (1 << bit):
                if offset == len(data):
                    break
                values = [data[offset]]
                offset += 1
                decoded.extend(values)
                ring[cursor] = values[0]
                cursor = (cursor + 1) & 1023
            else:
                if offset + 2 > len(data):
                    return decoded
                token = int.from_bytes(data[offset:offset + 2], 'little')
                offset += 2
                back = token & 1023
                if not back:
                    return decoded
                for _ in range((token >> 10) + 2):
                    value = ring[back]
                    assert value is not None, 'reference depends on unknown scratch history'
                    decoded.append(value)
                    ring[cursor] = value
                    cursor = (cursor + 1) & 1023
                    back = (back + 1) & 1023
    return decoded


def _compile_and_run(mutation=None, *, compiler="cc", extra_flags=()):
    cue, binary = ROOT / 'extracted/disc/disc.cue', ROOT / 'extracted/disc/disc.bin'
    if not cue.exists() or not binary.exists():
        pytest.skip('licensed pinned media unavailable')
    env = os.environ.copy()
    brew = Path('/home/linuxbrew/.linuxbrew/lib/pkgconfig')
    if (brew / 'sdl2.pc').exists():
        env['PKG_CONFIG_PATH'] = str(brew) + os.pathsep + env.get('PKG_CONFIG_PATH', '')
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2'],
                                    text=True, env=env).split()
    flags += [f'-Wl,-rpath,{flag[2:]}' for flag in flags if flag.startswith('-L')]
    with tempfile.TemporaryDirectory(prefix='musashi-list-') as tmp:
        base=Path(tmp); generated=base/'generated'; generated.mkdir()
        _generate_formatter_includes(generated)
        image=(ROOT/'extracted/disc/files/SLUS_007.26').read_bytes()
        assert hashlib.sha256(image).hexdigest()=='66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
        for address,count in ((0x8001a338,424),):
            words=re.findall(r'0x([0-9a-fA-F]{8})u',(generated/f'{address:08x}_words.inc').read_text())
            assert len(words)==count
            actual=struct.pack(f'<{count}I',*(int(w,16) for w in words))
            offset=address-0x80010000+0x800
            assert actual==image[offset:offset+len(actual)]
        exe=base/'probe'
        decoded = _title_sector_reference(binary)
        reference = base/'title-sector.bin'
        reference.write_bytes(struct.pack('<I',len(decoded)) + decoded)
        sources = {name: ROOT/'pc_port'/f'{name}.c' for name in (
            'mips_formatter','boot_memory','cd_controller','disc_media','bios_events',
            'bios_kernel','cd_dma3','dma_controller','scanline_timer','gpu_controller',
            'irq_controller','irq_scheduler','cpu_status','vblank_bindings')}
        if mutation:
            name, needle, replacement = mutation
            if name.endswith('.inc'):
                source = generated/name
                original = source.read_text()
                assert original.count(needle) == 1
                source.write_text(original.replace(needle,replacement))
            else:
                original = sources[name].read_text()
                assert original.count(needle) == 1
                sources[name] = base/f'{name}_mutant.c'
                sources[name].write_text(original.replace(needle,replacement))
        compiled = subprocess.run([compiler,'-std=c99','-O2','-Wall','-Wextra','-Werror',
            *extra_flags,
            '-Wno-parentheses','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            '-DMUSASHI_NATIVE_IRQ_DISPATCH=1','-DMUSASHI_NATIVE_CALLBACK_HANDLER=1',
            '-DMUSASHI_NATIVE_VBLANK=1',
            '-DMUSASHI_NATIVE_DMA_REGISTRATION=1','-DMUSASHI_NATIVE_DMA_IRQ_HANDLER=1',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
            str(ROOT/'tests/list_irq_probe.c'),
            *map(str,sources.values()),
            *[str(ROOT/'src/main'/f'{name}.c') for name in
              ('8001096c',)],str(ROOT/'pc_port/libmcrd_vblank.c'),str(ROOT/'pc_port/libetc_irq_dispatch.c'),str(ROOT/'pc_port/libetc_dma_irq.c'),str(ROOT/'pc_port/libetc_vsync_handler.c'),str(ROOT/'pc_port/libetc_dma_registration.c'),
            '-lcrypto',*flags,'-o',str(exe)],capture_output=True,text=True,timeout=30)
        assert compiled.returncode == 0, compiled.stderr
        return subprocess.run([str(exe),str(cue),str(binary),str(ROOT/'extracted/disc/files/SLUS_007.26'),str(reference)],capture_output=True,text=True,timeout=30)


def test_list_irq():
    result = _compile_and_run()
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'LIST_IRQ_PASS fixture_only=1' in result.stdout



@pytest.mark.parametrize('mutation', [
    ('cd_dma3','memcpy(d->memory->bytes + d->state.madr, staging, bytes);','(void)staging;'),
    ('mips_formatter','expected = mode == 2u ? 0x8001a958u :', 'expected = mode == 2u ? 0x8001a7f4u :'),
    ('8001a338_words.inc','0x1043000du','0x1000000du'),
    ('mips_formatter','accepted = instruction == 0x03e00008u && cpu.r[31] == expected;',
     'expected = cpu.r[31]; accepted = instruction == 0x03e00008u;'),
    ('80018714_words.inc','0xac2447acu','0x00000000u'),
    ('mips_formatter','const unsigned decode_limit = 2097152;',
     'const unsigned decode_limit = 16384;'),
    ('80018730_words.inc','0x14a0fff5u','0x1000fff5u'),
], ids=['missing_payload_copy','mapped_mode_alias','ignore_header_lba','wrong_saved_return',
        'missing_pac_destination','insufficient_decode_budget','nonterminating_decode_copy'])
def test_list_irq_mutants_rejected(mutation):
    result=_compile_and_run(mutation)
    assert result.returncode!=0,result.stdout+result.stderr
