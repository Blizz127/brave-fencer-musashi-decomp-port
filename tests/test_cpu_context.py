"""Live source context and explicit delay provenance; synthetic CPU frames."""
from pathlib import Path
import hashlib
import subprocess
import pytest
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image

ROOT = Path(__file__).resolve().parents[1]

def test_source_context_delay_and_borrowed_registers(tmp_path):
    exe = ROOT / 'extracted/disc/files/SLUS_007.26'
    if not exe.exists():
        pytest.skip('pinned licensed executable unavailable')
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == '66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    generated = tmp_path / 'generated'; generated.mkdir()
    _generate_formatter_includes(generated)
    probe = tmp_path / 'probe'
    subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
        '-Wno-parentheses', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-I', str(ROOT/'include'), '-I', str(ROOT/'pc_port/include'), '-I', str(generated),
        str(ROOT/'tests/cpu_context_probe.c'), str(ROOT/'pc_port/boot_memory.c'),
        '-lcrypto', '-o', str(probe)], check=True, timeout=40)
    _sync_code_image(generated)
    run = subprocess.run([str(probe), str(exe)], text=True, capture_output=True, timeout=15)
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout == 'CPU_CONTEXT_PASS source_branches=7 host_physical_context=UNCLAIMED\n'
