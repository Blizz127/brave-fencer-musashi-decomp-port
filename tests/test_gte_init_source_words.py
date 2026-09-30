"""Verify the staged GTE/BIOS word streams against retail EXE and asm."""
from pathlib import Path
import hashlib
import json
import os
import re
import struct

import pytest


AUTH_ROOT = Path(os.environ.get(
    'MUSASHI_GTE_AUTH_ROOT',
    str(Path(__file__).resolve().parents[1])))
SOURCE_ROOT = Path(os.environ.get(
    'MUSASHI_GTE_STAGE_ROOT',
    str(Path(__file__).resolve().parents[1])))
WORD_RE = re.compile(r'MUSASHI_NATIVE_MIPS_WORD\(0x([0-9A-Fa-f]{8})\)')
EXPECTED = {
    '80053cf8.c': (0x80053CF8, 0x80053D44, 19, 19, 0),
    '80047cb4.c': (0x80047CB4, 0x80047D34, 32, 32, 0),
    '8004d16c.c': (0x8004D16C, 0x8004D20C, 40, 26, 14),
    '80053ad8.c': (0x80053AD8, 0x80053AF8, 8, 8, 0),
    '8004921c.c': (0x8004921C, 0x80049234, 6, 6, 0),
    '8005ce28.c': (0x8005CE28, 0x8005CE34, 3, 3, 0),
}


def _retail_words(start, count, exe):
    offset = start - 0x80010000 + 0x800
    raw = exe[offset:offset + count * 4]
    return [struct.unpack_from('<I', raw, i)[0] for i in range(0, len(raw), 4)]


def test_gte_init_exports_match_pinned_exe_and_asm():
    exe_path = AUTH_ROOT / 'extracted/disc/files/SLUS_007.26'
    asm_path = AUTH_ROOT / 'asm/main.s'
    manifest_path = SOURCE_ROOT / 'artifacts/gte-init-source-exports.json'
    if not exe_path.exists() or not asm_path.exists():
        pytest.skip('pinned retail inputs unavailable')
    exe = exe_path.read_bytes()
    assert hashlib.sha256(exe).hexdigest() == \
        '66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    asm = asm_path.read_text()
    retail_asm = {
        int(address, 16): bytes.fromhex(raw)
        for address, raw in re.findall(
            r'/\* [0-9A-F]+ ([0-9A-F]{8}) ([0-9A-F]{8}) \*/', asm)
    }
    manifest = json.loads(manifest_path.read_text())
    assert manifest['total_words'] == 108
    assert manifest['total_executable_words'] == 94
    assert manifest['total_data_words'] == 14
    for name, (start, end, count, executable, data) in EXPECTED.items():
        source = SOURCE_ROOT / 'src/main' / name
        actual = [int(word, 16) for word in WORD_RE.findall(source.read_text())]
        assert len(actual) == count
        expected = _retail_words(start, count, exe)
        assert actual == expected
        for index, word in enumerate(actual):
            address = start + index * 4
            assert retail_asm[address] == struct.pack('<I', word)
        entry = next(item for item in manifest['exports']
                     if item['address'] == f'{start:08X}')
        assert entry['end'] == f'{end:08X}'
        assert entry['words'] == count
        assert entry['executable_words'] == executable
        assert entry['data_words'] == data
        raw = b''.join(struct.pack('<I', word) for word in actual)
        assert entry['raw_sha256'] == hashlib.sha256(raw).hexdigest()
    body = (SOURCE_ROOT / 'src/main/80053cf8.c').read_text()
    assert 'void func_80053CF8(void)' in body


def test_patch_export_separates_executable_and_exception_data():
    manifest = json.loads(
        (SOURCE_ROOT / 'artifacts/gte-init-source-exports.json').read_text())
    entry = next(item for item in manifest['exports']
                 if item['address'] == '8004D16C')
    assert entry['executable_words'] == 26
    assert entry['data_words'] == 14
    assert entry['words'] == entry['executable_words'] + entry['data_words']
