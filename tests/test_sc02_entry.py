"""Retail SC02 member31 identity, including exhaustive wrong-word controls."""
import ctypes
import hashlib
from pathlib import Path
import re
import struct
import subprocess

from tools.decode_title_pac import decode

ROOT = Path(__file__).resolve().parents[1]


def test_sc02_entry_identity(tmp_path):
    archive = (ROOT / 'extracted/disc/files/SC02.CD').read_bytes()
    assert hashlib.sha256(archive).hexdigest() == '8a04737374845155390268f7269c1599ecf6b6669357089059b20f1dcad11607'
    sector, size = struct.unpack_from('<II', archive, 8 + 31*8)
    assert (sector*2048, size) == (15247360, 1050624)
    member = archive[sector*2048:sector*2048+size]
    assert struct.unpack_from('<4I', member) == (0x434150, 4, 0, 329157)
    code, consumed = decode(member[0x800:329157])
    assert consumed == 327109
    assert hashlib.sha256(code).hexdigest() == '5bb5b6364206ccd0c47d6cf6a1dc2627e13bbef3cda40d618103b92ccfef4db6'
    # The entry words come from the user's own disc (decoded member 31).
    words = list(struct.unpack('<178I', code[:712]))
    # While src/ still carries word exports, they must equal the disc image.
    for path in (ROOT / 'src/overlays/sc02_0031').glob('*.c'):
        exported = [int(w,16) for w in re.findall(r'MUSASHI_NATIVE_MIPS_WORD\(0x([0-9A-F]{8})\)', path.read_text())]
        if not exported:
            continue
        raw = struct.pack('<' + 'I'*len(exported), *exported)
        offset = int(path.stem,16)-0x80128158
        assert code[offset:offset+len(raw)] == raw, path.name
    assert words[(0x801282AC-0x80128158)//4] == 0x8C224F08
    formatter = (ROOT / 'pc_port/mips_formatter.c').read_text()
    # Slice only the function under test. Reaching to formatter_step now drags
    # in refusal_trace/fprintf helpers that the harness does not declare, and
    # GCC 16's C23 default rejects the implicit declarations. Ending at the next
    # '\n\n/*' comment is equally wrong: sc01_entry_matches directly follows
    # with no comment between them, so the slice swallowed it and its
    # undeclared kOverlaySc01_80128158Words table. Cut at this function's own
    # closing brace, which sits alone in column 0.
    # Run off the disc: identity is a digest of the loaded 178-word entry
    # span, so the harness needs span_digest, the digest constants and
    # sc02_entry_matches. Each is cut at its own column-0 closing brace.
    def cut(start):
        begin = formatter.index(start)
        return formatter[begin:formatter.index('\n}\n', begin) + 3]
    consts = formatter[formatter.index('enum { SC_ENTRY_WORDS = 178 };'):
                       formatter.index('#define SC01_MEMBER0_ENTRY_DIGEST')]
    helper = cut('static int span_digest(') + consts + cut('static int sc02_entry_matches(')
    assert 'sc01_entry_matches' not in helper
    harness = '''#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t words[178]; int fail; } MusashiBootMemory;
static uint8_t *musashi_boot_ram_span(MusashiBootMemory *m,uint32_t a,size_t n) {
if(a!=0x80128158u || n!=178u*4u || m->fail>=0) return 0;
return (uint8_t *)m->words;
}
''' + helper + '\nint probe(MusashiBootMemory *m) { return sc02_entry_matches(m); }\n'
    path = tmp_path / 'probe.c'; path.write_text(harness)
    lib = tmp_path / 'probe.so'
    subprocess.run(['cc','-shared','-fPIC',str(path),'-o',str(lib)],check=True,capture_output=True)
    class Memory(ctypes.Structure):
        _fields_ = [('words',ctypes.c_uint32*178),('fail',ctypes.c_int)]
    probe = ctypes.CDLL(str(lib)).probe
    probe.argtypes = [ctypes.POINTER(Memory)]
    memory = Memory((ctypes.c_uint32*178)(*words),-1)
    assert probe(ctypes.byref(memory)) == 1
    for i in range(178):
        memory.words[i] ^= 1
        assert probe(ctypes.byref(memory)) == 0
        memory.words[i] ^= 1
        memory.fail = i
        assert probe(ctypes.byref(memory)) == 0
        memory.fail = -1
    assert 'g_overlay_sc02_0031_words = sc02_entry_matches(memory);' in formatter
