from pathlib import Path
import re
import struct

import pytest

from tools.decode_title_pac import BASE, decode, title_code

ROOT = Path(__file__).resolve().parents[1]


def test_literals_and_overlapping_match():
    assert decode(b'\x07ABC\x01\x0c\x00\x00') == (b'ABCABCAB', 8)


@pytest.mark.parametrize('data', [b'\x01', b'\x00\x01', b'\x00\x01\x00', b'\xffABCDEFGH'])
def test_invalid_or_unterminated_stream_refused(data):
    with pytest.raises(ValueError):
        decode(data)


def test_expansion_limit():
    with pytest.raises(ValueError, match='bounded RAM'):
        decode(b'\x07ABC\x01\x0c\x00\x00', 7)


def test_title_code_provenance_and_first_sector():
    from test_list_irq import _title_sector_reference
    code = title_code(ROOT / 'extracted/disc/files/SC01.CD')
    first = _title_sector_reference(ROOT / 'extracted/disc/disc.bin')
    assert code[:len(first)] == first
    # SC01 title dispatch differs from MAIN0012 at the same address.
    assert struct.unpack_from('<I', code, 0x801282AC - BASE)[0] == 0x8C22EEA0
    main = (ROOT / 'extracted/overlays/main/0012.bin').read_bytes()
    assert struct.unpack_from('<I', main, 0x28000 + 0x801282AC - BASE)[0] == 0x8C22E618
    for path in (ROOT / 'src/overlays/sc01_0000').glob('*.c'):
        words = re.findall(r'MUSASHI_NATIVE_MIPS_WORD\(0x([0-9A-F]{8})\)', path.read_text())
        assert words
        raw = struct.pack(f'<{len(words)}I', *(int(w,16) for w in words))
        offset = int(path.stem, 16) - BASE
        assert code[offset:offset+len(raw)] == raw
