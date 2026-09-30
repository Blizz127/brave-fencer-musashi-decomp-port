"""Selected ROM image, real shared CPU token and kernel consumer fixtures."""
from pathlib import Path
import os
import hashlib
import subprocess
import pytest
ROOT = Path(__file__).resolve().parents[1]
ROM = Path(os.environ.get('BFM_BIOS', 'bios/scph5500.bin'))  # user-supplied BIOS

@pytest.mark.parametrize('mutation', [None, 'table', 'continuation', 'publication', 'consumer'])
def test_selected_exception_image_and_kernel(tmp_path, mutation):
    if not ROM.exists(): pytest.skip('selected licensed BIOS unavailable')
    data=ROM.read_bytes()
    assert hashlib.sha256(data).hexdigest()=='11052b6499e466bbf0a709b1f9cb6834a9418e66680387912451e971cf8a1fef'
    assert hashlib.sha256(data[0x10780:0x107b8]).hexdigest()=='83f0476343ece5e0c763e8bb5338370e48c2eda65b8189886a25dbd076918fb6'
    exe=ROOT/'extracted/disc/files/SLUS_007.26'
    if not exe.exists(): pytest.skip('pinned licensed EXE unavailable')
    raw=exe.read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    assert hashlib.sha256(raw[0x3d9d4:0x3da0c]).hexdigest()=='f4c0d8444a0b8a46df755f60d7b2b6964d22e8ee8dc622662103ae17ff8784b3'
    replacements={
        'table': ('{0x1508,0x1b20', '{0x150c,0x1b20'),
        'continuation': ('0x0c0003a8,0,0x3042003c', '0x0c0003a9,0,0x3042003c'),
        'publication': ('image->written_mask!=0x3fff', 'image->written_mask==0'),
        'consumer': ('accepted = run_exception_entry(kernel, token) && run_early_hook(kernel);',
                     'accepted = (token != NULL) && run_early_hook(kernel);'),
    }
    sources=['bios_exception.c','bios_kernel.c','bios_events.c','bios_card.c','cpu_status.c','boot_memory.c']
    files=[ROOT/'pc_port'/f for f in sources]
    if mutation:
        index=1 if mutation=='consumer' else 0
        text=files[index].read_text(); old,new=replacements[mutation]
        assert text.count(old)==1
        altered=tmp_path/files[index].name; altered.write_text(text.replace(old,new)); files[index]=altered
    binary=tmp_path/'probe' 
    subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-I',str(ROOT/'pc_port/include'),str(ROOT/'tests/bios_exception_probe.c'),
        *[str(f) for f in files],
        '-lcrypto','-o',str(binary)],check=True,timeout=40)
    run=subprocess.run([str(binary),str(ROM),str(exe)],capture_output=True,text=True,timeout=15)
    if mutation:
        assert run.returncode!=0, 'semantic mutant unexpectedly passed: '+mutation
        return
    assert run.returncode==0,run.stdout+run.stderr
    assert run.stdout=='BIOS_EXCEPTION_PASS source_rom=1 shared_token=1 publication=1 card_hook_preserved=1\n'
