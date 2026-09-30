"""Execute production SB dispatch and byte bus routing against DMA registers."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]


def test_sb_drives_full_shifted_source_word():
    source=(ROOT/'pc_port/mips_formatter.c').read_text()
    start=source.index('static int cpu_write8(')
    body=source[start:source.index('\nstatic int cpu_read16(',start)]
    sb=source.split('    case 40: {',1)[1].split('    case 41:',1)[0]
    harness=r'''
#include "musashi_dma_controller.h"
#include <stdint.h>
#include <assert.h>
typedef struct { uint8_t byte; } MusashiBootMemory;
typedef struct { void *userdata; int (*write8)(void*,uint32_t,uint8_t); } Device;
typedef struct { uint32_t r[32]; uint32_t pc; Device *input_device,*cd_register_device; } FormatterCpu;
/* The production byte-write path refuses through this helper; the probe only
 * checks that the refusal happens before any DMA register effect. */
static int access_refuse0(uint32_t pc, uint32_t address, unsigned width, int store) {
 (void)pc;(void)address;(void)width;(void)store;return 0;
}
static MusashiDmaController dma;
static uint8_t *cpu_ram_span(MusashiBootMemory *m,const FormatterCpu *c,uint32_t a,unsigned n) {
 (void)c;(void)n;return a==0x80000000u?&m->byte:0;
}
static int callback_write32(const FormatterCpu *c,uint32_t a,uint32_t v) {
 (void)c;return musashi_dma_controller_write32(&dma,a,v);
}
'''+body+r'''
static int step(MusashiBootMemory *memory,FormatterCpu *cpu) {
 unsigned rs=1,rt=2; int16_t signed_immediate=0;
 switch(40) { case 40: {
'''+sb+r'''
 } return 1;
}
int main(void) {
 MusashiBootMemory m={0};FormatterCpu c={0};MusashiIrqController irq={0};unsigned off,b;
 const uint32_t baseline[]={0,0xffffffff,0x11223344};
 musashi_dma_controller_init(&dma,&irq);
 for(b=0;b<3;b++)for(off=0;off<4;off++) {
  dma.control=baseline[b];c.r[1]=0x1f8010f0u+off;c.r[2]=0xaabbccdd;
  assert(step(&m,&c));assert(dma.control==(0xaabbccddu<<(off*8)));
 }
 dma.interrupt=0x869e1234;c.r[1]=0x1f8010f6;c.r[2]=0x1029e;
 assert(step(&m,&c));assert(dma.interrupt==0x849e0000);
 c.r[1]=0x80000000;c.r[2]=0xaabbccdd;assert(step(&m,&c));assert(m.byte==0xdd);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as temp:
        p=Path(temp);(p/'probe.c').write_text(harness)
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'pc_port/include'),str(p/'probe.c'),str(ROOT/'pc_port/dma_controller.c'),'-o',str(p/'probe')],check=True)
        subprocess.run([str(p/'probe')],check=True)
