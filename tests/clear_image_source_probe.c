/* Synthetic source frame/configuration, real register owners. No GPU fill,
 * renderer, linked-list DMA completion, native boot or captured-state proof. */
#include "../pc_port/mips_formatter.c"
#include "musashi_gpu_controller.h"
#include "musashi_dma_controller.h"
#include "musashi_scanline_timer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static MusashiBootMemory memory,saved;
static FormatterCpu cpu;
static MusashiGpuController gpu;
static MusashiDmaController dma;
static MusashiIrqController irq;
static MusashiScanlineTimer timer;
static unsigned cycles,calls,gpu_reads,gpu_writes,dma_refusals;
static uint32_t refuse_pc;
static int reset(void *p){(void)p;return 1;}
static int mode(void *p,uint32_t v){(void)p;(void)v;return 1;}
static int display(void *p,int v){(void)p;(void)v;return 1;}
static int ready(void *p){(void)p;return 1;}
static int advance(void *p,uint32_t cost){(void)p;calls++;if(cpu.pc==refuse_pc)return 0;cycles+=cost;return 1;}
static int read_gpu(void *p,uint32_t a,uint32_t *v){(void)p;gpu_reads++;return a==0x1f8010a8?musashi_dma_controller_read32(&dma,a,v):musashi_gpu_controller_read32(&gpu,a,v);}
static int write_gpu(void *p,uint32_t a,uint32_t v){(void)p;gpu_writes++;if(a==0x1f8010a0){dma_refusals++;return 0;}return musashi_gpu_controller_write32(&gpu,a,v);}
static uint16_t read_irq(void *p,uint32_t a){uint16_t v;(void)p;assert(a==0x1f801110?musashi_scanline_timer_read16(&timer,a,&v):musashi_irq_controller_read16(&irq,a,&v));return v;}
static void write_irq(void *p,uint32_t a,uint16_t v){(void)p;assert(musashi_irq_controller_write16(&irq,a,v));}
static MusashiExecutionClock clock_device={NULL,advance};
static MusashiGpuIoDevice io={NULL,read_gpu,write_gpu};
static MusashiCallbackDevice callback={.read16=read_irq,.write16=write_irq};
static void put(uint32_t a,uint32_t v){assert(musashi_boot_write32(&memory,a,v));}
static void put16(uint32_t a,uint16_t v){assert(musashi_boot_write16(&memory,a,v));}
static uint32_t word(uint32_t a){uint32_t v;assert(musashi_boot_read32(&memory,a,&v));return v;}
static void setup(const uint8_t *exe,size_t size,uint32_t draw_mode,uint32_t color) {
    memset(&memory,0,sizeof(memory));assert(musashi_boot_map_exe(&memory,exe,size));
    memset(&gpu,0,sizeof(gpu));memset(&irq,0,sizeof(irq));memset(&timer,0,sizeof(timer));
    const MusashiGpuBackend backend={.reset=reset,.draw_mode=mode,.display_enable=display,.clear_fifo=reset,.ready=ready};
    assert(musashi_gpu_controller_init(&gpu,&backend));
    assert(musashi_gpu_controller_write32(&gpu,0x1f801810,0xe1000000|draw_mode));
    musashi_dma_controller_init(&dma,&irq);assert(musashi_dma_controller_write32(&dma,0x1f8010a8,0x401));
    assert(musashi_irq_controller_write16(&irq,0x1f801074,0xd));assert(musashi_scanline_timer_init_paced(&timer,0));
    assert(musashi_scanline_timer_write32(&timer,0x1f801114,0x107));
    /* Explicit SDK/device/queue fixture prerequisites; no completion state.
     * Dimensions and RECT are test inputs. Genuine source performs validation,
     * queue ownership/mask changes and all packet stores. */
    put(0x80072780,0x80072740);put(0x80072748,0x8005b7b0);put(0x8007274c,0x8005af68);
    put(0x8007285c,0x1f801814);put(0x80072860,0x1f8010a0);put(0x80072864,0x1f8010a4);put(0x80072868,0x1f8010a8);
    put(0x8006cb8c,0x1f801074);put16(0x8007278c,1024);put16(0x8007278e,512);
    memory.bytes[0x72789]=1;memory.bytes[0x7278a]=1;
    put(0x8007288c,0);put(0x80072890,0);put(0x80072794,0);put(0x8006cbb8,10);
    put(0x801ff010,0);put(0x801ff014,0x01e00280);
    memset(memory.bytes+0x78830,0xa5,0x40);
    memset(&cpu,0,sizeof(cpu));cpu.pc=0x80059888;cpu.npc=cpu.pc+4;
    cpu.r[29]=0x801ff000;cpu.r[31]=0x800147a8;cpu.r[4]=0x801ff010;
    cpu.r[5]=color&255;cpu.r[6]=(color>>8)&255;cpu.r[7]=(color>>16)&255;
    cpu.clock=&clock_device;cpu.gpu_io_device=&io;cpu.callback_device=&callback;
    cycles=calls=gpu_reads=gpu_writes=dma_refusals=refuse_pc=0;
}
static int until(uint32_t target){for(unsigned i=0;i<700;i++){if(cpu.pc==target)return 1;if(!formatter_step(&memory,&cpu)){fprintf(stderr,"CLEAR_STOP pc=%08x ra=%08x sp=%08x\n",cpu.pc,cpu.r[31],cpu.r[29]);return 0;}}return 0;}
static void unchanged_refusal(int clock_charged){FormatterCpu prior=cpu;saved=memory;unsigned old=cycles,old_calls=calls;int accepted=formatter_step(&memory,&cpu);if(accepted)fprintf(stderr,"UNEXPECTED_ACCEPT pc=%08x charged=%d\n",prior.pc,clock_charged);assert(!accepted);assert(!memcmp(&prior,&cpu,sizeof(cpu))&&!memcmp(&saved,&memory,sizeof(memory)));assert(cycles==old+(clock_charged?3u:0u));assert(calls==old_calls+(clock_charged?1u:0u));}
static void success(const uint8_t *exe,size_t size){
    for(unsigned variant=0;variant<2;variant++){
        uint32_t color=variant?0x563412:0,draw_mode=variant?0x631:0,status;
        setup(exe,size,draw_mode,color);assert(musashi_gpu_controller_read32(&gpu,0x1f801814,&status));
        assert(until(0x800598f4));assert(cpu.r[4]==0x8005af68&&cpu.r[5]==0x801ff010&&cpu.r[6]==8);
        assert(cpu.r[29]==0x801fefd8&&word(0x801feff8)==0x800147a8);
        assert(until(0x8005b8bc));assert(cpu.r[19]==0x8005af68&&cpu.r[17]==8&&cpu.r[18]==color);
        assert(cpu.r[29]==0x801fefb0&&word(0x801fefd0)==0x800598fc&&word(0x801fefcc)==0x801ff010);
        assert(irq.mask==0&&word(0x80072894)==0xd); /* Real source exclusion, not completion. */
        assert(until(0x8005af68));assert(cpu.r[4]==0x801ff010&&cpu.r[5]==color&&cpu.r[31]==0x8005b8c4);
        assert(until(0x8005b710));assert(cpu.r[4]==0x80078830&&cpu.r[31]==0x8005b1a8&&cpu.r[29]==0x801fef70);
        const uint32_t packet[]={0x05ffffff,0xe6000000,0xe1000000|(status&0x7ff),0x02000000|color,0,0x01e00280};
        for(unsigned i=0;i<6;i++)assert(word(0x80078830+4*i)==packet[i]);
        for(unsigned i=24;i<64;i++)assert(memory.bytes[0x78830+i]==0xa5);
        assert(word(0x801ff010)==0&&word(0x801ff014)==0x01e00280);
        assert(until(0x8005b730));assert(gpu.dma_direction==2&&cpu.r[4]==0x80078830);
        unchanged_refusal(1);assert(dma_refusals==1&&dma.gpu_channel_control==0x401&&irq.mask==0);
        assert(!gpu.faulted); /* DMA capability absent; GP1 alone really accepted. */
    }
}
static uint32_t sp_word(uint32_t a){uint32_t v;memcpy(&v,memory.scratchpad+(a-0x1f800000u),4);return v;}
/* The opening scene calls ClearImage with its stack and RECT in the 1KB
 * scratchpad (sp 1F8002E8, RECT 1F800320). Same source path, same packet. */
static void scratchpad_stack(const uint8_t *exe,size_t size){
    const uint32_t rect=0x1f800320u,top=0x1f800310u,dims=0x01e00280u,color=0x563412;
    uint32_t status;
    setup(exe,size,0x631,color);assert(musashi_gpu_controller_read32(&gpu,0x1f801814,&status));
    memset(memory.scratchpad+(rect-0x1f800000u),0,4);memcpy(memory.scratchpad+(rect+4u-0x1f800000u),&dims,4);
    cpu.r[29]=top;cpu.r[4]=rect;
    assert(until(0x800598f4));assert(cpu.r[5]==rect&&cpu.r[29]==top-0x28u&&sp_word(top-0x8u)==0x800147a8);
    assert(until(0x8005b8bc));assert(cpu.r[19]==0x8005af68&&cpu.r[18]==color&&sp_word(cpu.r[29]+0x20u)==0x800598fc);
    assert(until(0x8005af68));assert(cpu.r[4]==rect&&cpu.r[5]==color);
    assert(until(0x8005b710));
    const uint32_t packet[]={0x05ffffff,0xe6000000,0xe1000000|(status&0x7ff),0x02000000|color,0,dims};
    for(unsigned i=0;i<6;i++)assert(word(0x80078830+4*i)==packet[i]);
    /* At the dispatch gate a RECT straddling the scratchpad's end is
     * refused with the CPU unchanged (the validator's own load already
     * faults on such a RECT before it gets here). */
    setup(exe,size,0,0);memcpy(memory.scratchpad+(rect+4u-0x1f800000u),&dims,4);
    cpu.r[29]=top;cpu.r[4]=rect;
    assert(until(0x800598f4));cpu.r[5]=cpu.r[19]=0x1f8003fcu;unchanged_refusal(0);
}
static void negatives(const uint8_t *exe,size_t size){
    for(unsigned n=0;n<7;n++){
        setup(exe,size,0,0);
        uint32_t pc=n<2?0x800598f4:n<4?0x8005b8bc:0x8005b154;
        assert(until(pc));
        if(n==0)put(0x8007274c,0x8005b710);
        if(n==1)cpu.r[6]=0x40;
        if(n==2)put(cpu.r[29]+0x20,0x80059dfc);
        if(n==3)cpu.r[19]=0x8005b710;
        if(n==4){put(0x8007285c,0x1f801810);cpu.r[2]=0x1f801810;}
        if(n==5){put(0x8007285c,0x1f801810);}
        if(n==6){refuse_pc=pc;FormatterCpu prior=cpu;saved=memory;unsigned old=cycles,old_calls=calls;
            assert(!formatter_step(&memory,&cpu));assert(!memcmp(&prior,&cpu,sizeof(cpu))&&!memcmp(&saved,&memory,sizeof(memory)));assert(cycles==old&&calls==old_calls+1);continue;}
        unchanged_refusal(0);
    }
    setup(exe,size,0,0);memory.bytes[0x7278a]=2;put(0x80072784,0x8005b710);
    assert(until(0x80059844));unchanged_refusal(0);assert(!gpu_reads&&!gpu_writes);
    /* Separate fetched-instruction fixture for the second diagnostic site.
     * We do not synthesize a return from the refused first diagnostic call.
     * Its SW delay must not touch the stack on an unbound mapped alias. */
    setup(exe,size,0,0);cpu.pc=0x8005986c;cpu.npc=cpu.pc+4;
    cpu.r[3]=0x8005b710;cpu.r[2]=0xdeadbeef;put(0x80072784,cpu.r[3]);
    unchanged_refusal(0);assert(!gpu_reads&&!gpu_writes);
}
int main(int argc,char **argv){assert(argc==2);FILE*file=fopen(argv[1],"rb");assert(file);assert(!fseek(file,0,SEEK_END));long size=ftell(file);rewind(file);assert(size>0);uint8_t*exe=malloc((size_t)size);assert(exe&&fread(exe,1,(size_t)size,file)==(size_t)size);assert(!fclose(file));success(exe,(size_t)size);scratchpad_stack(exe,(size_t)size);negatives(exe,(size_t)size);free(exe);puts("CLEAR_IMAGE_SOURCE_PASS fixture_only=1 fill_and_dma=NOT_RUN");return 0;}
