/* Synthetic pinned-EXE/source-created DISPENV fixture. Real GPU controller,
 * checking backend only: this is not native lifecycle or enabled scanout proof. */
#ifndef DISPLAY_SOURCE_FORMATTER
#define DISPLAY_SOURCE_FORMATTER "../pc_port/mips_formatter.c"
#endif
#include DISPLAY_SOURCE_FORMATTER
#include "musashi_gpu_controller.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define ENV 0x800a6498u
#define STACK 0x801ff000u
static MusashiBootMemory ram,before_ram;
static MusashiGpuController gpu;
static uint32_t submitted[8];
static unsigned attempts,accepted,backend_calls,refuse_at,clock_calls,ticks;
static int clock_accept;
static MusashiGpuDisplayState rendered;
static const uint32_t expected[4]={0x05000000u,0x06c60260u,0x07040010u,0x08000001u};
static int reset(void *p) {
    (void)p;
    rendered=(MusashiGpuDisplayState){.horizontal=0x00c00200,.vertical=0x00040010,
        .width=256,.height=240,.h_start=512,.h_end=3072,.v_start=16,.v_end=256,.dot_divisor=10};
    return 1;
}
static int ready(void *p) {(void)p;return 1;}
static int draw_mode(void *p,uint32_t w) {(void)p;(void)w;return 1;}
static int disable(void *p,int enabled) {(void)p;return !enabled;}
static int display(void *p,const MusashiGpuDisplayState *candidate) {
    (void)p;++backend_calls;
    assert(backend_calls>=1 && backend_calls<=4);
    assert(candidate->origin==0 && candidate->x==0 && candidate->y==0);
    assert(candidate->horizontal==(backend_calls==1?0x00c00200u:0x00c60260u));
    assert(candidate->vertical==0x00040010 && candidate->v_start==16 && candidate->v_end==256);
    assert(candidate->h_start==(backend_calls==1?512:608));
    assert(candidate->h_end==(backend_calls==1?3072:3168));
    assert(candidate->mode==(backend_calls==4?1u:0u));
    assert(candidate->dot_divisor==(backend_calls==4?8:10));
    assert(candidate->width==(backend_calls==4?320:256) && candidate->height==240);
    if(refuse_at && backend_calls==refuse_at)return 0;
    rendered=*candidate;return 1;
}
static int advance(void *p,uint32_t cost) {
    (void)p;++clock_calls;if(!clock_accept)return 0;ticks+=cost;return 1;
}
static int write32(void *p,uint32_t address,uint32_t word) {
    (void)p;assert(address==MUSASHI_GPU_GP1);assert(attempts<8);
    submitted[attempts++]=word;
    int ok=musashi_gpu_controller_write32(&gpu,address,word);
    if(ok)++accepted;
    return ok;
}
static MusashiExecutionClock clock_device={NULL,advance};
static MusashiGpuIoDevice io={.write32=write32};
static void put32(uint32_t a,uint32_t v) {assert(musashi_boot_write32(&ram,a,v));}
static uint32_t get32(uint32_t a) {uint32_t v;assert(musashi_boot_read32(&ram,a,&v));return v;}
static void fresh(const uint8_t *exe,size_t size,FormatterCpu *cpu,unsigned refusal) {
    memset(&ram,0xa5,sizeof(ram));assert(musashi_boot_map_exe(&ram,exe,size));
    memset(cpu,0,sizeof(*cpu));cpu->pc=0x80059fc0;cpu->npc=cpu->pc+4;
    cpu->r[4]=ENV;cpu->r[29]=STACK;cpu->r[31]=UINT32_MAX;
    for(unsigned i=16;i<=23;++i)cpu->r[i]=0x12340000u+i;
    cpu->r[28]=0x80074750;cpu->r[30]=0x801fffc0;cpu->hi=0x11223344;cpu->lo=0x55667788;
    cpu->clock=&clock_device;cpu->gpu_io_device=&io;
    /* Synthetic prior ResetGraph effects, not a captured post-initialization
     * image. Actual 52704 instructions below construct the 20-byte input. */
    ram.bytes[0x72788]=ram.bytes[0x7278a]=ram.bytes[0x7278b]=0;
    put32(0x8006cbe8,0);
    memset(ram.bytes+0x727f4,0xff,20);
    memset(ram.bytes+0x78874,0x5a,0x100);
    memset(ram.bytes+(ENV&0x1fffff),0xa5,20);
    FormatterCpu setup={0};setup.pc=0x80052704;setup.npc=setup.pc+4;
    setup.r[2]=0x800a6438;setup.r[17]=240;setup.r[18]=320;
    setup.r[19]=setup.r[20]=4;setup.r[29]=STACK;
    for(unsigned n=0;setup.pc!=0x80059fc0;n++) {assert(n<100);assert(formatter_step(&ram,&setup));}
    assert(setup.r[4]==ENV);
    memset(&gpu,0,sizeof(gpu));memset(&rendered,0,sizeof(rendered));
    attempts=accepted=backend_calls=refuse_at=clock_calls=ticks=0;clock_accept=1;
    const MusashiGpuBackend backend={.reset=reset,.ready=ready,.draw_mode=draw_mode,
        .display_enable=disable,.clear_fifo=reset,.display=display};
    assert(musashi_gpu_controller_init(&gpu,&backend));
    attempts=accepted=backend_calls=0;refuse_at=refusal;
    before_ram=ram;
}
static void memory_guard(void) {
    for(unsigned i=0;i<MUSASHI_RAM_SIZE;i++) {
        if(i>=(STACK&0x1fffff)-0x18 && i<(STACK&0x1fffff)-4)continue;
        if(i==((ENV&0x1fffff)+0x12))continue;
        if(i>=0x727f4 && i<0x72808)continue;
        if(i>=0x78879 && i<0x7887d)continue;
        assert(ram.bytes[i]==before_ram.bytes[i]);
    }
}
static void invoke(const uint8_t *exe,size_t size,unsigned refusal) {
    FormatterCpu cpu;fresh(exe,size,&cpu,refusal);FormatterCpu entry=cpu;
    unsigned n;
    for(n=0;n<1000 && cpu.pc!=UINT32_MAX;n++) {
        FormatterCpu prior=cpu;MusashiGpuController prior_gpu=gpu;
        MusashiGpuDisplayState prior_rendered=rendered;
        if(!formatter_step(&ram,&cpu)) {
            assert(refusal && attempts==refusal && accepted==refusal-1);
            assert(cpu.pc==0x8005b690 && cpu.r[4]==expected[refusal-1]);
            assert(!memcmp(&cpu,&prior,sizeof(cpu)));
            prior_gpu.faulted=1;assert(!memcmp(&gpu,&prior_gpu,sizeof(gpu)));
            assert(!memcmp(&rendered,&prior_rendered,sizeof(rendered)));
            break;
        }
    }
    assert(n<1000);assert(attempts==(refusal?refusal:4));
    for(unsigned i=0;i<attempts;i++)assert(submitted[i]==expected[i]);
    for(unsigned i=0;i<4;i++)assert(ram.bytes[0x78879+i]==(i<accepted?(uint8_t)expected[i]:0x5a));
    assert(gpu.display_disabled && gpu.faulted==(refusal?1:0) && !gpu.executing);
    if(refusal) {
        assert(cpu.r[29]==STACK-0x28);
        assert(get32(STACK-8)==UINT32_MAX);
        assert(!memcmp(ram.bytes+0x727f4,before_ram.bytes+0x727f4,20));
    } else {
        assert(cpu.pc==UINT32_MAX && cpu.r[2]==ENV && cpu.r[29]==STACK && cpu.r[31]==UINT32_MAX);
        assert(cpu.hi==entry.hi && cpu.lo==entry.lo);
        for(unsigned i=16;i<32;i++)if(i!=24&&i!=25&&i!=26&&i!=27)assert(cpu.r[i]==entry.r[i]);
        assert(!memcmp(ram.bytes+0x727f4,ram.bytes+(ENV&0x1fffff),20));
        uint32_t status;assert(musashi_gpu_controller_read32(&gpu,MUSASHI_GPU_GPUSTAT,&status));
        assert((status&0x007f0000u)==0x00020000u);
        assert(accepted==4 && backend_calls==4 && ticks>0 && clock_calls>0);
        assert(gpu.display.origin==rendered.origin && gpu.display.horizontal==rendered.horizontal);
        assert(gpu.display.vertical==rendered.vertical && gpu.display.mode==rendered.mode);
        assert(gpu.display.width==320 && rendered.width==320 && gpu.display.height==240);
        assert(gpu.accepted_gp0_words==0); /* These are GP1 effects, not fake GP0 submissions. */
    }
    memory_guard();
}
static void clock_refusal(const uint8_t *exe,size_t size) {
    FormatterCpu cpu;fresh(exe,size,&cpu,0);
    for(unsigned n=0;cpu.pc!=0x8005b690;n++) {assert(n<400);assert(formatter_step(&ram,&cpu));}
    FormatterCpu prior=cpu;MusashiGpuController prior_gpu=gpu;before_ram=ram;
    unsigned old_ticks=ticks,old_calls=clock_calls;clock_accept=0;
    assert(!formatter_step(&ram,&cpu));
    assert(clock_calls==old_calls+1 && ticks==old_ticks && !attempts && !accepted && !backend_calls);
    assert(!memcmp(&cpu,&prior,sizeof(cpu)) && !memcmp(&gpu,&prior_gpu,sizeof(gpu)));
    assert(!memcmp(&ram,&before_ram,sizeof(ram)));
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    uint8_t *exe=malloc((size_t)n);assert(exe);assert(fread(exe,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));
    for(unsigned refusal=0;refusal<=4;refusal++)invoke(exe,(size_t)n,refusal);
    clock_refusal(exe,(size_t)n);
    free(exe);puts("DISPLAY_REGISTER_SOURCE_PASS accepted=4 refused_prefixes=4 source_frame=1 scanout=NOT_EXECUTED");return 0;
}
