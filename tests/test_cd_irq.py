from pathlib import Path
import subprocess
import sys
import tempfile
import os
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image
from test_audio_sdl import _sdl_compile_flags


def _media_and_flags():
    cue, binary = ROOT / 'extracted/disc/disc.cue', ROOT / 'extracted/disc/disc.bin'
    if not cue.exists() or not binary.exists():
        pytest.skip('licensed pinned media unavailable')
    flags, _ = _sdl_compile_flags()
    assert flags is not None, "SDL2 SDK metadata unavailable"
    return cue, binary, flags


def _compile_and_run(cue, binary, flags, mutation=None):
    with tempfile.TemporaryDirectory(prefix='musashi-cd-irq-') as tmp:
        base=Path(tmp); generated=base/'generated'; generated.mkdir()
        _generate_formatter_includes(generated)
        exe=base/'probe'
        formatter = ROOT / 'pc_port/mips_formatter.c'
        if mutation is not None:
            needle, replacement = mutation
            source = formatter.read_text()
            assert source.count(needle) == 1
            formatter = base / 'mips_formatter_mutant.c'
            formatter.write_text(source.replace(needle, replacement))
        command = ['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',
            '-Wno-parentheses','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            '-DMUSASHI_NATIVE_IRQ_DISPATCH=1','-DMUSASHI_NATIVE_CALLBACK_HANDLER=1',
            '-DMUSASHI_NATIVE_VBLANK=1',
            '-DMUSASHI_NATIVE_DMA_REGISTRATION=1','-DMUSASHI_NATIVE_DMA_IRQ_HANDLER=1',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
            str(ROOT/'tests/cd_irq_probe.c'),
            str(formatter),
            *[str(ROOT/'pc_port'/f'{name}.c') for name in ('boot_memory',
              'cd_controller','disc_media','bios_events','irq_controller','irq_scheduler','cpu_status','vblank_bindings')],
            *[str(ROOT/'src/main'/f'{name}.c') for name in
              ('8001096c',)],str(ROOT/'pc_port/libmcrd_vblank.c'),str(ROOT/'pc_port/libetc_irq_dispatch.c'),str(ROOT/'pc_port/libetc_dma_irq.c'),str(ROOT/'pc_port/libetc_vsync_handler.c'),str(ROOT/'pc_port/libetc_dma_registration.c'),
            '-lcrypto',*flags,'-o',str(exe)]
        compiled = subprocess.run(command, capture_output=True, text=True,
                                  timeout=30)
        if compiled.returncode != 0:
            return compiled, None
        _sync_code_image(generated)
        run = subprocess.run([str(exe),str(cue),str(binary)],
                             capture_output=True, text=True, timeout=30)
        return compiled, run


def test_cd_irq_source_and_scheduler():
    cue, binary, flags = _media_and_flags()
    compiled, run = _compile_and_run(cue, binary, flags)
    assert compiled.returncode == 0, compiled.stderr
    assert run is not None and run.returncode == 0, run.stderr if run else ''


@pytest.mark.parametrize(
    ('name', 'mutation'),
    [
        (
            'disabled_43398_guard',
            ('services->deliver_event && target == 0x80043398u &&',
             'services->deliver_event && 0 &&'),
        ),
        (
            'bypassed_deliver_event',
            ('!selected.deliver_event(selected.userdata,cpu.r[4],cpu.r[5])',
             '!1'),
        ),
    ],
)
def test_cd_irq_source_callback_mutants_fail_at_runtime(name, mutation):
    del name
    cue, binary, flags = _media_and_flags()
    compiled, run = _compile_and_run(cue, binary, flags, mutation)
    assert compiled.returncode == 0, compiled.stderr
    assert run is not None
    assert run.returncode != 0, run.stdout + run.stderr


def test_cd_irq_cpu_binds_spu_for_8003ab84_voice_sh():
    source = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert source.count("cpu.cd_spu_device = selected.cd_spu;") == 1
    boot = (ROOT / "pc_port/native_boot.c").read_text()
    assert ".cd_spu=boot.spu ? &boot.cd_spu : NULL" in boot


def test_cd_irq_jalr_80037d14_admits_800301a4():
    """Live boot STOP: PAC jalr at 80037D14 (not its delay slot) to 800301A4."""
    source = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert source.count("case 0x80037d14u:") == 1
    assert "case 0x80037d18u:" not in source
    assert "target == 0x800301a4u && sp == saved_sp-0xc0u" in source
