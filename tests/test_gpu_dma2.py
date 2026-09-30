"""Real DMA/GPU controllers, synthetic renderer and source-independent RAM lists."""
from pathlib import Path
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[1]


def compile_probe(tmp_path, owner_source):
    binary = tmp_path / 'probe'
    subprocess.run([
        'cc', '-Wl,--wrap=realloc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'pc_port/include'),
        str(ROOT / 'tests/gpu_dma2_probe.c'), str(owner_source),
        str(ROOT / 'pc_port/dma_controller.c'), str(ROOT / 'pc_port/gpu_controller.c'),
        '-o', str(binary),
    ], check=True, timeout=30)
    return subprocess.run([str(binary)], text=True, capture_output=True, timeout=15)


def test_gpu_dma2_general_lists_and_refusals(tmp_path):
    run = compile_probe(tmp_path, ROOT / 'pc_port/gpu_dma2.c')
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout == 'GPU_DMA2_OWNED_PASS fixture_renderer=synthetic\n'


@pytest.mark.parametrize('old,new', [
    ('(status & 0x12000000u) != 0x12000000u', '(status & 0x16000000u) != 0x16000000u'),
    ('delay = 1u + (uint64_t)nodes + payload;', 'delay = (uint64_t)nodes + payload;'),
    ('d->dma->gpu_channel_control = value;\n    d->pending = 1;',
     'd->dma->gpu_channel_control = value & ~DMA2_BUSY;\n    d->pending = 1;'),
    ('\n        count = tag >> 24;', '\n        count = 6;'),
    ('if (!accepted || after != before + 1u || d->violation || d->gpu->faulted)',
     'if (!accepted) d->state.accepted_words = 0;\n            '
     'if (!accepted || after != before + 1u || d->violation || d->gpu->faulted)'),
    ('accepted = musashi_gpu_controller_write32(d->gpu, MUSASHI_GPU_GP0,\n'
     '                                                       d->snapshot[position++]);',
     '++position; ++d->gpu->accepted_gp0_words; accepted = 1;'),
])
def test_gpu_dma2_mutations_rejected(tmp_path, old, new):
    source = (ROOT / 'pc_port/gpu_dma2.c').read_text()
    assert source.count(old) == 1
    mutant = tmp_path / 'mutant.c'
    mutant.write_text(source.replace(old, new))
    run = compile_probe(tmp_path, mutant)
    assert run.returncode != 0, 'implementation mutation survived the real-controller fixture'
