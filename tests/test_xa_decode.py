"""Real XA samples versus an independent codec and direct convolution oracle."""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import pytest
ROOT=Path(__file__).resolve().parents[1]


def test_opening_xa_matches_ffmpeg_and_resampling(tmp_path):
    if not shutil.which('ffmpeg'):
        pytest.skip('FFmpeg oracle unavailable')
    with (ROOT/'extracted/disc/disc.bin').open('rb') as f:
        f.seek(4657*2352);stream=f.read(32*2352)
    inp=tmp_path/'movie.str';inp.write_bytes(stream)
    code=r'''
#include "musashi_xa_decode.h"
#include <stdio.h>
#include <assert.h>
int main(int argc,char **argv) {
 FILE *f,*o,*r;MusashiXaDecode d,z,before;uint8_t raw[2352];
 int16_t pcm[4032],out[4704],saved[4704];unsigned i;
 assert(argc==4);f=fopen(argv[1],"rb");o=fopen(argv[2],"wb");r=fopen(argv[3],"wb");
 assert(f&&o&&r);xa_reset(&d);xa_reset(&z);
 while(fread(raw,1,2352,f)==2352)if((raw[18]&0x44)==0x44) {
  assert(xa_decode_samples(&d,raw,pcm));assert(xa_decode(&z,raw,out));
  assert(fwrite(pcm,2,4032,o)==4032);assert(fwrite(out,2,4704,r)==4704);
 }
 /* Unsupported coding and malformed headers preserve history and output. */
 before=z;for(i=0;i<4704;i++)out[i]=saved[i]=1234;
 raw[19]=0xff;assert(!xa_decode(&z,raw,out));
 assert(!memcmp(&z,&before,sizeof z)&&!memcmp(out,saved,sizeof out));
 raw[19]=1;raw[20]^=1;assert(!xa_decode(&z,raw,out));
 assert(!memcmp(&z,&before,sizeof z)&&!memcmp(out,saved,sizeof out));
 fclose(f);fclose(o);fclose(r);return 0;
}
'''
    (tmp_path/'probe.c').write_text(code)
    exe=tmp_path/'probe';raw=tmp_path/'raw.pcm';resampled=tmp_path/'resampled.pcm'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'pc_port/include'),str(tmp_path/'probe.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(inp),str(raw),str(resampled)],check=True)
    oracle=tmp_path/'ffmpeg.pcm'
    subprocess.run(['ffmpeg','-v','error','-f','psxstr','-i',str(inp),'-map','0:a:0','-c:a','pcm_s16le','-f','s16le',str(oracle)],check=True)
    assert raw.read_bytes()==oracle.read_bytes()
    assert len(raw.read_bytes())==4*2016*4
    header=(ROOT/'pc_port/include/musashi_xa_decode.h').read_text()
    table=header.split('xa_coefficients[29][7] = {',1)[1].split('};',1)[0]
    coeff=[list(map(int,re.findall(r'-?\d+',line))) for line in table.splitlines() if '{' in line]
    samples=struct.unpack('<%dh'%(len(raw.read_bytes())//2),raw.read_bytes())
    expected=[]
    for end in range(6,len(samples)//2+1,6):
        for phase in range(7):
            for ch in range(2):
                value=sum((samples[(end-t-1)*2+ch]*coeff[t][phase])//32768 for t in range(29) if end-t-1>=0)
                expected.append(max(-32768,min(32767,value)))
    assert resampled.read_bytes()==struct.pack('<%dh'%len(expected),*expected)
