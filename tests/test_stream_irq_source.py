"""Movie IRQ admission edges must be real JAL instructions in the pinned EXE."""
from pathlib import Path
import hashlib
import re
import struct

ROOT=Path(__file__).resolve().parents[1]


def test_stream_edges_match_retail_instructions():
    exe=(ROOT/'extracted/disc/files/SLUS_007.26').read_bytes()
    assert hashlib.sha256(exe).hexdigest()=='66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a'
    source=(ROOT/'pc_port/mips_formatter.c').read_text()
    body=source.split('static const uint32_t stream_edges[][2] = {',1)[1].split('};',1)[0]
    edges=re.findall(r'\{0x([0-9a-f]+)u,0x([0-9a-f]+)u\}',body)
    assert len(edges)>=14
    for call,target in edges:
        call,target=int(call,16),int(target,16)
        word=struct.unpack_from('<I',exe,call-0x80010000+0x800)[0]
        assert word==0x0c000000|((target>>2)&0x03ffffff)
