"""Exercise real MDEC setup transfers, reset, and refusal boundaries."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_mdec_setup_and_dma_completion():
    probe = r'''
#include "musashi_mdec_controller.h"
#include <assert.h>
#include <string.h>
static MusashiBootMemory ram;
int main(void) {
 MusashiIrqController irq={0}; MusashiDmaController dma;
 MusashiMdecController d; uint32_t v=0; unsigned i;
 musashi_dma_controller_init(&dma,&irq);
 musashi_mdec_init(&d,&ram,&dma);
 assert(musashi_mdec_write32(&d,0x1f801824,0x80000000));
 assert(musashi_mdec_read32(&d,0x1f801824,&v)&&v==0x80040000);
 assert(musashi_mdec_write32(&d,0x1f801824,0x60000000));
 assert(musashi_mdec_write32(&d,0x1f801820,0x40000001));
 for(i=0;i<128;i++)ram.bytes[0x1000+i]=(unsigned char)(i+1);
 assert(musashi_mdec_write32(&d,0x1f801080,0x1000));
 assert(musashi_mdec_write32(&d,0x1f801084,0x00010020));
 /* DPCR disabled: no transfer and no completion. */
 assert(!musashi_mdec_write32(&d,0x1f801088,0x01000201));
 dma.control|=8; dma.interrupt=0x00810000;
 assert(musashi_mdec_write32(&d,0x1f801088,0x01000201));
 assert(d.remaining==32 && !d.transfers && !(irq.status&8));
 assert(musashi_mdec_read32(&d,0x1f801088,&v)&&(v&0x01000000));
 assert(musashi_mdec_advance(&d,33));
 assert(d.remaining==0 && d.transfers==1 && (irq.status&8));
 for(i=0;i<128;i++)assert(d.quant[i]==(unsigned char)(i+1));
 assert(musashi_mdec_read32(&d,0x1f801088,&v)&&!(v&0x01000000));
 /* Scale table parameters are signed LE halfwords, not quant bytes. */
 assert(musashi_mdec_write32(&d,0x1f801820,0x60000000));
 for(i=0;i<32;i++)assert(musashi_mdec_write32(&d,0x1f801820,0x80007fff));
 assert(d.scale[0]==32767 && d.scale[1]==-32768 && !d.remaining);
 assert(musashi_mdec_write32(&d,0x1f801820,0x40000000));
 assert(musashi_mdec_write32(&d,0x1f801820,0x01020304));
 assert(musashi_mdec_write32(&d,0x1f801824,0x80000000));
 assert(!d.remaining && d.command==0 && d.quant[0]==4);
 /* An unsupported monochrome decode must not masquerade as a completed movie. */
 assert(!musashi_mdec_write32(&d,0x1f801820,0x20000020));
 v=0xabcdef01; assert(!musashi_mdec_read32(&d,0x1f801820,&v)&&v==0xabcdef01);
 assert(!musashi_mdec_advance(&d,32));
 /* DMA cannot overrun RAM, exceed table length, or mutate a pending span. */
 assert(musashi_mdec_write32(&d,0x1f801824,0x60000000));
 assert(musashi_mdec_write32(&d,0x1f801820,0x40000000));
 assert(musashi_mdec_write32(&d,0x1f801080,0x1ffff0));
 assert(musashi_mdec_write32(&d,0x1f801084,0x00010010));
 assert(!musashi_mdec_write32(&d,0x1f801088,0x01000201));
 assert(musashi_mdec_write32(&d,0x1f801080,0x1000));
 assert(musashi_mdec_write32(&d,0x1f801084,0x00010020));
 assert(!musashi_mdec_write32(&d,0x1f801088,0x01000201));
 assert(musashi_mdec_write32(&d,0x1f801084,0x00010010));
 assert(musashi_mdec_write32(&d,0x1f801088,0x01000201));
 assert(!musashi_mdec_write32(&d,0x1f801080,0x2000));
 assert(!musashi_mdec_write32(&d,0x1f801820,0));
 assert(musashi_mdec_write32(&d,0x1f801824,0x80000000));
 assert(!musashi_mdec_advance(&d,50) && d.transfers==1);
 assert(d.chcr[0]&0x01000000); /* No completion from reset. */
 assert(musashi_mdec_write32(&d,0x1f801088,0));
 assert(musashi_mdec_advance(&d,51));
 /* Six real RLE blocks: neutral chroma, distinct quadrant luminance.
  * DC-only IDCT gives constant pixels; output DMA restores 16x16 rows. */
 musashi_mdec_init(&d,&ram,&dma);
 dma.control|=0x88; dma.interrupt=0x00830000;
 assert(musashi_mdec_write32(&d,0x1f801824,0x60000000));
 memset(d.quant,1,sizeof(d.quant));
 memset(d.scale,0,sizeof(d.scale));
 for(i=0;i<8;i++)d.scale[i]=0x5a82;
 assert(musashi_mdec_write32(&d,0x1f801820,0x3a000006));
 /* DMA1 may start before decoded pixels exist, but cannot complete. */
 assert(musashi_mdec_write32(&d,0x1f801090,0x4000));
 assert(musashi_mdec_write32(&d,0x1f801094,0x00040020));
 assert(musashi_mdec_write32(&d,0x1f801098,0x01000200));
 assert(musashi_mdec_advance(&d,129));
 assert(!d.transfers && (d.chcr[1]&0x01000000));
 for(i=0;i<6;i++) {
   unsigned dc=i<2?0:(i-2)*64;
   assert(musashi_mdec_write32(&d,0x1f801820,0xfe000000u|0x400u|dc));
 }
 assert(d.output_size==512 && d.output_cursor==0);
 assert(musashi_mdec_advance(&d,130));
 assert(d.transfers==1 && d.output_cursor==512 && !(d.chcr[1]&0x01000000));
 for(i=0;i<256;i++) {
   unsigned quadrant=(i%16)/8+(i/128)*2;
   unsigned channel=16+quadrant;
   unsigned expected=0x8000|channel|(channel<<5)|(channel<<10);
   assert((ram.bytes[0x4000+i*2]|(unsigned)ram.bytes[0x4001+i*2]<<8)==expected);
 }
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as temp:
        p=Path(temp); (p/'probe.c').write_text(probe)
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'pc_port/include'),str(p/'probe.c'),str(ROOT/'pc_port/mdec_controller.c'),str(ROOT/'pc_port/dma_controller.c'),'-o',str(p/'probe')],check=True)
        subprocess.run([str(p/'probe')],check=True)
