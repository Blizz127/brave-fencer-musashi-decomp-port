"""Real pinned-sector owner tests; missing licensed media is the only skip."""
from pathlib import Path
import subprocess
import tempfile
import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize('mutation', [None, 'zero_data', 'rewind_request'])
def test_cd_sector_direct_compile(mutation):
    cue, binary = ROOT / 'extracted/disc/disc.cue', ROOT / 'extracted/disc/disc.bin'
    if not cue.exists() or not binary.exists():
        pytest.skip('licensed pinned media unavailable')
    with tempfile.TemporaryDirectory(prefix='musashi-cd-sector-') as temp:
        executable = Path(temp) / 'probe'
        cd_source = ROOT / 'pc_port/cd_controller.c'
        if mutation:
            source = cd_source.read_text()
            old, new = {
                'zero_data': ('memcpy(destination,c->data_fifo+s->fifo_cursor,bytes);',
                              'memset(destination,0,bytes);'),
                'rewind_request': ('if (!s->data_requested) {',
                                   'if (1) {'),
            }[mutation]
            assert source.count(old) == 1
            cd_source = Path(temp) / 'mutated_cd.c'
            cd_source.write_text(source.replace(old, new))
        subprocess.run(['cc' , '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'pc_port/include'),
                        str(ROOT / 'tests/cd_sector_probe.c'),
                        str(cd_source),
                        str(ROOT / 'pc_port/disc_media.c'), '-lcrypto',
                        '-o', str(executable)], check=True, timeout=30)
        result = subprocess.run([str(executable), str(cue), str(binary)],
                                capture_output=True, text=True, timeout=30)
        if mutation:
            assert result.returncode != 0, 'semantic mutant unexpectedly passed'
            assert 'Assertion' in result.stderr
        else:
            assert result.returncode == 0, result.stderr
            assert 'CD_SECTOR_PASS' in result.stdout
