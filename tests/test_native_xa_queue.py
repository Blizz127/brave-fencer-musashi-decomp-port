"""Native XA transport retains every sample across a host-side underrun."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]


def test_late_pcm_is_queued_intact_and_contiguously(tmp_path):
    source=(ROOT/'pc_port/native_boot.c').read_text()
    start=source.index('static int submit_xa_pcm(')
    body=source[start:source.index('\nstatic int cd_write8',start)]
    probe=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
typedef struct { uint64_t sample_index; } MusashiSpuCdAudioSnapshot;
typedef struct { void *spu; uint64_t xa_sample_anchor; } NativeBoot;
static uint64_t current,starts[4];static unsigned calls;
static int16_t samples[4][4];
static int musashi_spu_cd_audio_snapshot(void *p,MusashiSpuCdAudioSnapshot *s) {
 (void)p;s->sample_index=current;return 1;
}
static int musashi_spu_cd_audio_submit_cd_pcm(void *p,uint64_t start,const int16_t *pcm,size_t n) {
 (void)p;assert(start>=current && calls<4 && n==2);starts[calls]=start;
 for(unsigned i=0;i<4;i++)samples[calls][i]=pcm[i];++calls;return 1;
}
'''+body+r'''
int main(void) {
 NativeBoot b={(void*)1,0};const int16_t pcm[4]={-32768,32767,-13,71};
 current=10;assert(submit_xa_pcm(&b,0,0,pcm,2));
 current=15;assert(submit_xa_pcm(&b,0,2,pcm,2));
 current=16;assert(submit_xa_pcm(&b,0,4,pcm,2));
 assert(starts[0]==10 && starts[1]==15 && starts[2]==17);
 for(unsigned c=0;c<3;c++)for(unsigned i=0;i<4;i++)assert(samples[c][i]==pcm[i]);
 assert(!submit_xa_pcm(&b,0,UINT64_MAX,pcm,2));assert(calls==3);
}
'''
    (tmp_path/'probe.c').write_text(probe)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(tmp_path/'probe.c'),'-o',str(tmp_path/'probe')],check=True)
    subprocess.run([str(tmp_path/'probe')],check=True)
