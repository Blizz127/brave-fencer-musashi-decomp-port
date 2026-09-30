from pathlib import Path
import subprocess
import pytest
ROOT = Path(__file__).resolve().parents[1]

def run_probe(tmp_path, source):
    binary = tmp_path / 'cpu_status'
    subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-DMUSASHI_CPU_STATUS_TESTING', '-I', str(ROOT / 'pc_port/include'),
                    str(ROOT / 'tests/cpu_status_probe.c'), str(source), '-o', str(binary)],
                   check=True, timeout=30)
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)

def test_cpu_status_selected_rom_equations_and_ownership(tmp_path):
    run = run_probe(tmp_path, ROOT / 'pc_port/cpu_status.c')
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout == 'CPU_STATUS_PASS synthetic_contexts=1\n'

@pytest.mark.parametrize('old,new', [
    ('((o->state.sr & 15u) << 2)', '((o->state.sr & 15u) << 1)'),
    ('((o->state.saved_sr >> 2) & 15u)', '((o->state.saved_sr >> 1) & 15u)'),
    ('t->identity == o->state.identity &&', ''),
    ('if (!token_valid(o, t) || !o->state.kernel_entered ||\n',
     'if (!token_valid(o, t) ||\n'),
    ('next.gpr, c->gpr, sizeof next.gpr', 'next.gpr, c->gpr, sizeof(uint32_t)'),
    ('next.epc = c->delay_slot ? c->branch_pc : c->pc;', 'next.epc = c->pc;'),
])
def test_cpu_status_mutations_rejected(tmp_path, old, new):
    source = (ROOT / 'pc_port/cpu_status.c').read_text()
    assert source.count(old) == 1
    mutant = tmp_path / 'mutant.c'
    mutant.write_text(source.replace(old, new))
    run = run_probe(tmp_path, mutant)
    assert run.returncode != 0, 'Status/context/return mutation survived'
