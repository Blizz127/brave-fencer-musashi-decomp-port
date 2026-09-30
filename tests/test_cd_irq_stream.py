"""Synthetic callback state, real mounted CD/DMA/IRQ execution."""
from pathlib import Path
import subprocess
import pytest
from test_cd_irq import ROOT, _media_and_flags
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image


@pytest.mark.parametrize("variant", ["baseline", "zero_pio_budget", "no_bank_renewal"])
def test_cd_irq_stream_source(tmp_path, variant):
    cue, binary, flags = _media_and_flags()
    base = tmp_path
    generated = base / 'generated'
    if not generated.exists():
        generated.mkdir()
        _generate_formatter_includes(generated)
    exe = base / 'probe'
    formatter = ROOT / 'pc_port/mips_formatter.c'
    if variant == "zero_pio_budget":
        source = formatter.read_text()
        needle = "pio_remaining = (((cpu.r[5]+63u)/64u)+1u)*70000u+2048u;"
        assert source.count(needle) == 1
        formatter = base / "formatter_mutant.c"
        formatter.write_text(source.replace(needle, "pio_remaining = 0;"))
    if variant == "no_bank_renewal":
        source = formatter.read_text()
        needle = "if (cd_irq_bank_progress(bank_state,state,bank_lba,lba,previous_bank_lba))"
        assert source.count(needle) == 1
        formatter = base / "formatter_mutant.c"
        formatter.write_text(source.replace(needle, "if (0 && cd_irq_bank_progress(bank_state,state,bank_lba,lba,previous_bank_lba))"))
    command = ['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',
        '-Wno-parentheses','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-DMUSASHI_NATIVE_IRQ_DISPATCH=1','-DMUSASHI_NATIVE_CALLBACK_HANDLER=1',
        '-DMUSASHI_NATIVE_VBLANK=1',
        '-DMUSASHI_NATIVE_DMA_REGISTRATION=1','-DMUSASHI_NATIVE_DMA_IRQ_HANDLER=1',
        '-I',str(ROOT/'include'),'-I',str(ROOT/'pc_port/include'),'-I',str(generated),
        str(ROOT/'tests/cd_irq_stream_probe.c'),
        str(formatter),
        *[str(ROOT/'pc_port'/f'{name}.c') for name in ('boot_memory',
          'spu_cd_audio','cd_controller','cd_dma3','dma_controller','disc_media','bios_events','irq_controller','irq_scheduler','cpu_status','vblank_bindings')],
        *[str(ROOT/'src/main'/f'{name}.c') for name in
          ('8001096c',)],str(ROOT/'pc_port/libmcrd_vblank.c'),str(ROOT/'pc_port/libetc_irq_dispatch.c'),str(ROOT/'pc_port/libetc_dma_irq.c'),str(ROOT/'pc_port/libetc_vsync_handler.c'),str(ROOT/'pc_port/libetc_dma_registration.c'),
        '-lcrypto',*flags,'-o',str(exe)]
    subprocess.run(command, check=True, timeout=40)
    _sync_code_image(generated)
    result = subprocess.run([str(exe),str(cue),str(binary)],capture_output=True,text=True,timeout=30)
    if variant == "zero_pio_budget":
        assert result.returncode != 0 and "budget=3" in result.stderr, result.stdout + result.stderr
    elif variant == "no_bank_renewal":
        assert result.returncode != 0 and "budget=1" in result.stderr, result.stdout + result.stderr
    else:
        assert result.returncode == 0, result.stdout + result.stderr
