"""New service-binding fixture; no real PsyCross worker or cold boot claim."""
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


def test_cd_irq_services_scheduler():
    cue, binary = ROOT / 'extracted/disc/disc.cue', ROOT / 'extracted/disc/disc.bin'
    if not cue.exists() or not binary.exists():
        pytest.skip('licensed pinned media unavailable')
    flags, _ = _sdl_compile_flags()
    assert flags is not None, "SDL2 SDK metadata unavailable"
    with tempfile.TemporaryDirectory(prefix='musashi-cd-services-') as tmp:
        base=Path(tmp); generated=base/'generated'; generated.mkdir()
        _generate_formatter_includes(generated)
        exe=base/'probe'
        subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',
            '-Wno-parentheses','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            '-DMUSASHI_NATIVE_IRQ_DISPATCH=1','-DMUSASHI_NATIVE_CALLBACK_HANDLER=1',
            '-DMUSASHI_NATIVE_VBLANK=1',
            '-DMUSASHI_NATIVE_DMA_REGISTRATION=1','-DMUSASHI_NATIVE_DMA_IRQ_HANDLER=1',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
            str(ROOT/'tests/cd_irq_services_scheduler_probe.c'),
            *[str(ROOT/'pc_port'/f'{name}.c') for name in ('mips_formatter','boot_memory',
              'cd_controller','disc_media','bios_events','bios_kernel','irq_controller','irq_scheduler','cpu_status','vblank_bindings')],
            *[str(ROOT/'src/main'/f'{name}.c') for name in
              ('8001096c',)],str(ROOT/'pc_port/libmcrd_vblank.c'),str(ROOT/'pc_port/libetc_irq_dispatch.c'),str(ROOT/'pc_port/libetc_dma_irq.c'),str(ROOT/'pc_port/libetc_vsync_handler.c'),str(ROOT/'pc_port/libetc_dma_registration.c'),
            '-lcrypto',*flags,'-o',str(exe)],check=True,timeout=30)
        _sync_code_image(generated)
        subprocess.run([str(exe),str(cue),str(binary)],check=True,timeout=30)
