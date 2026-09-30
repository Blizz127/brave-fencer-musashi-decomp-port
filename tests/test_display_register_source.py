"""Actual display source plus real GPU controller; fixture is not scanout proof."""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import sys
import pytest
ROOT = Path(os.environ.get('MUSASHI_TEST_REPO_ROOT', Path(__file__).resolve().parents[1]))
STAGE = Path(os.environ.get('MUSASHI_GPU_STAGE_ROOT', ROOT))
sys.path.insert(0, str(ROOT / 'tests'))
from test_bios_event_callbacks import _generate_formatter_includes, _sync_code_image


@pytest.mark.parametrize('mutation', [None, 'omit_gp1_store', 'omit_cache_store'])
def test_display_register_source_and_retained_refusal_prefix(tmp_path, mutation):
    exe = ROOT / 'extracted/disc/files/SLUS_007.26'
    if not exe.exists():
        pytest.skip('pinned licensed EXE unavailable')
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == '66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    generated = tmp_path / 'generated'
    generated.mkdir()
    _generate_formatter_includes(generated)
    if mutation:
        stream = generated / '8005b684_words.inc'
        source = stream.read_text()
        old = '0xac440000u' if mutation == 'omit_gp1_store' else '0xa0248874u'
        assert source.count(old) == 1
        stream.write_text(source.replace(old, '0x00000000u'))
    binary = tmp_path / 'probe'
    subprocess.run([
        'cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-parentheses',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-I', str(STAGE / 'pc_port/include'), '-I', str(ROOT / 'include'), '-I', str(ROOT / 'pc_port/include'), '-I', str(generated),
        '-DDISPLAY_SOURCE_FORMATTER="' + str(ROOT / 'pc_port/mips_formatter.c') + '"',
        str(Path(__file__).with_name('display_register_source_probe.c')),
        str(ROOT / 'pc_port/boot_memory.c'), str(STAGE / 'pc_port/gpu_controller.c'),
        '-lcrypto', '-o', str(binary),
    ], check=True, timeout=45)
    _sync_code_image(generated)
    run = subprocess.run([str(binary), str(exe)], capture_output=True, text=True, timeout=15)
    if mutation:
        assert run.returncode != 0, 'compiled source mutant escaped: ' + mutation
        assert re.search('assertion', run.stderr, re.I), run.stderr
    else:
        assert run.returncode == 0, run.stdout + run.stderr
        assert run.stdout == 'DISPLAY_REGISTER_SOURCE_PASS accepted=4 refused_prefixes=4 source_frame=1 scanout=NOT_EXECUTED\n'
