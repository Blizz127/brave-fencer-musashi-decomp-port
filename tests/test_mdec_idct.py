"""Independent cosine-basis check of the uploaded-scale integer IDCT."""
from pathlib import Path
import ctypes
import math
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]


def test_all_idct_basis_vectors():
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)
        (p/'probe.c').write_text('''
#include "musashi_mdec_controller.h"
#include <string.h>
#include "musashi_mdec_decode.h"
void transform(int *b,const int16_t *s) { mdec_idct(b,s); }
''')
        subprocess.run(['cc','-std=c99','-shared','-fPIC','-O2','-I',str(ROOT/'pc_port/include'),str(p/'probe.c'),'-o',str(p/'probe.so')],check=True)
        lib=ctypes.CDLL(str(p/'probe.so'))
        lib.transform.argtypes=[ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_int16)]
        # Uploaded PSX scale table, generated independently from cosine bases.
        basis=[[math.cos((2*x+1)*u*math.pi/16)*(1 if u==0 else math.sqrt(2)) for x in range(8)] for u in range(8)]
        scale=(ctypes.c_int16*64)(*[int(v*math.sqrt(2)*16384) for row in basis for v in row])
        for index in range(64):
            for amplitude in (-512,511):
                b=(ctypes.c_int*64)();b[index]=amplitude
                lib.transform(b,scale)
                # Two transposed passes produce the 2-D inverse DCT.
                for y in range(8):
                    for x in range(8):
                        expected=amplitude*basis[index%8][x]*basis[index//8][y]/8
                        assert abs(b[y*8+x]-expected)<2.1, (index,x,y,b[y*8+x],expected)
