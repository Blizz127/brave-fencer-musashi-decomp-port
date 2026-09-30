/* Explicit CPU/device fixture; no native lifecycle, DMA or renderer proof. */
#ifndef DRAW_MMIO_FORMATTER
#define DRAW_MMIO_FORMATTER "../pc_port/mips_formatter.c"
#endif
#include DRAW_MMIO_FORMATTER
#include "musashi_gpu_controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static MusashiBootMemory memory, saved_memory;
typedef struct Probe {
    unsigned reads, writes, legacy, cycles, ticks;
    int refuse_read, refuse_write, refuse_clock, real_gpu;
    uint32_t address, value;
    MusashiGpuController gpu;
} Probe;
static int tick(void *u, uint32_t cost) {
    Probe *p=u; ++p->ticks;
    if (p->refuse_clock) return 0;
    p->cycles+=cost; return 1;
}
static uint32_t legacy_read(void *u, uint32_t address) {
    Probe *p=u; assert(address==0x1f801814); ++p->legacy; return 0xdeadbeef;
}
static int read_word(void *u, uint32_t address, uint32_t *value) {
    Probe *p=u; ++p->reads; p->address=address;
    *value=0xbad00bad; /* Even hostile refused output must not reach the CPU. */
    if (p->refuse_read) return 0;
    if (p->real_gpu) return musashi_gpu_controller_read32(&p->gpu,address,value);
    *value=address ^ 0x87654321; return 1;
}
static int write_word(void *u, uint32_t address, uint32_t value) {
    Probe *p=u; ++p->writes; p->address=address; p->value=value;
    if (p->refuse_write) return 0;
    return !p->real_gpu || musashi_gpu_controller_write32(&p->gpu,address,value);
}
static int reset(void *u) { (void)u; return 1; }
static int mode(void *u,uint32_t v) { (void)u; (void)v; return 1; }
static int display(void *u,int v) { (void)u; (void)v; return 1; }
static void pointers(void) {
    memset(&memory,0xa5,sizeof(memory));
    assert(musashi_boot_write32(&memory,0x80072868,0x1f8010a8));
    assert(musashi_boot_write32(&memory,0x8007285c,0x1f801814));
    assert(musashi_boot_write32(&memory,0x80072860,0x1f8010a0));
    assert(musashi_boot_write32(&memory,0x80072864,0x1f8010a4));
}
static void routing(void) {
    static const uint32_t owned[]={0x1f801810,0x1f801814,0x1f8010a8,0x1f8010a0,0x1f8010a4};
    static const uint32_t absent[]={0x1f8010ac,0x1f8010a2,0x1f801818,0x1f801815};
    Probe p={0}; FormatterCpu cpu={0};
    MusashiGpuIoDevice io={&p,read_word,write_word};
    MusashiGpuDevice legacy={.userdata=&p,.read32=legacy_read};
    MusashiCallbackDevice callbacks={.reset_device=&legacy};
    uint32_t out; uint16_t half; uint8_t byte;
    cpu.callback_device=&callbacks;
    assert(cpu_read32(&memory,&cpu,0x1f801814,&out) && out==0xdeadbeef && p.legacy==1);
    assert(!cpu_read32(&memory,&cpu,0x1f8010a8,&out));
    assert(!cpu_read32(&memory,&cpu,0x1f801810,&out));
    for(unsigned i=0;i<5;i++) {
        if(owned[i]!=0x1f801814) {
            out=0xfeedface;
            assert(!cpu_read32(&memory,&cpu,owned[i],&out) && out==0xfeedface);
        }
        assert(!cpu_write32(&memory,&cpu,owned[i],0));
    }
    cpu.gpu_io_device=&io;
    for(unsigned i=0;i<5;i++) {
        assert(cpu_read32(&memory,&cpu,owned[i],&out) && out==(owned[i]^0x87654321));
        assert(cpu_write32(&memory,&cpu,owned[i],0x12345678));
        assert(p.address==owned[i] && p.value==0x12345678);
        p.refuse_read=1; out=0xfeedface;
        assert(!cpu_read32(&memory,&cpu,owned[i],&out) && out==0xfeedface);
        p.refuse_read=0; p.refuse_write=1;
        assert(!cpu_write32(&memory,&cpu,owned[i],0)); p.refuse_write=0;
        io.read32=NULL; out=0xfeedface;
        assert(!cpu_read32(&memory,&cpu,owned[i],&out) && out==0xfeedface);
        io.read32=read_word; io.write32=NULL;
        assert(!cpu_write32(&memory,&cpu,owned[i],0)); io.write32=write_word;
        unsigned reads=p.reads,writes=p.writes;
        byte=0xa5; half=0xa55a;
        assert(!cpu_read8(&memory,&cpu,owned[i],&byte) && byte==0xa5);
        assert(!cpu_read16(&memory,&cpu,owned[i],&half) && half==0xa55a);
        assert(!cpu_write8(&memory,&cpu,owned[i],0));
        assert(!cpu_write16(&memory,&cpu,owned[i],0));
        assert(p.reads==reads && p.writes==writes);
    }
    unsigned reads=p.reads,writes=p.writes;
    for(unsigned i=0;i<4;i++) {
        out=0xfeedface;
        assert(!cpu_read32(&memory,&cpu,absent[i],&out) && out==0xfeedface);
        assert(!cpu_write32(&memory,&cpu,absent[i],0x12345678));
    }
    assert(!cpu_read32(&memory,&cpu,0x1f801814,NULL));
    assert(p.reads==reads && p.writes==writes && p.legacy==1 && !p.cycles);
}
static void instructions(void) {
    static const uint32_t sites[][4]={
        {0x8005b870,0x80072868,0x1f8010a8,2},
        {0x8005b8a4,0x8007285c,0x1f801814,3},
        {0x8005b720,0x8007285c,0x1f801814,2},
        {0x8005b730,0x80072860,0x1f8010a0,2},
        {0x8005b740,0x80072864,0x1f8010a4,2},
        {0x8005b750,0x80072868,0x1f8010a8,2}};
    for(unsigned site=0;site<6;site++) for(unsigned test=0;test<6;test++) {
        Probe p={0}; FormatterCpu cpu={0}, before;
        MusashiGpuIoDevice io={&p,read_word,write_word};
        MusashiExecutionClock clock={&p,tick};
        MusashiGpuDevice legacy={.userdata=&p,.read32=legacy_read};
        MusashiCallbackDevice callbacks={.reset_device=&legacy};
        pointers(); cpu.gpu_io_device=&io; cpu.clock=&clock; cpu.callback_device=&callbacks;
        cpu.pc=sites[site][0]; cpu.npc=cpu.pc+4;
        cpu.r[2]=0xabcdef12; cpu.r[sites[site][3]]=sites[site][2];
        if(site==2) cpu.r[3]=0x04000002;
        if(site==5) cpu.r[3]=0x01000401;
        cpu.r[4]=0x800a6454; cpu.hi=0x12345678;cpu.lo=0x87654321;
        if(test==1) { /* Matching wrong cell/register must not become a new capability. */
            uint32_t alias=sites[site][2]==0x1f801814 ? 0x1f801810 : 0x1f801814;
            assert(musashi_boot_write32(&memory,sites[site][1],alias));
            cpu.r[sites[site][3]]=alias;
        }
        if(test==2) cpu.r[sites[site][3]]^=4; /* Cell remains correct. */
        if(test==3) p.refuse_read=p.refuse_write=1;
        if(test==4) p.refuse_clock=1;
        if(test==5) assert(musashi_boot_write32(&memory,sites[site][1],0x80078000));
        before=cpu; saved_memory=memory;
        int accepted=formatter_step(&memory,&cpu);
        if(test) {
            assert(!accepted && !memcmp(&cpu,&before,sizeof(cpu)));
            assert(p.cycles==(test==3?3u:0u));
            assert(p.ticks==((test==1 || test==2 || test==5)?0u:1u));
            assert(p.reads+p.writes==(test==3?1u:0u));
        } else {
            assert(accepted && cpu.pc==before.pc+4 && cpu.npc==before.npc+4);
            assert(p.cycles==3 && p.ticks==1 && p.reads+p.writes==1);
            if(site<2) assert(cpu.r[2]==(sites[site][2]^0x87654321));
            else {
                static const uint32_t values[]={0,0,0x04000002,0x800a6454,0,0x01000401};
                assert(p.value==values[site]);
            }
        }
        assert(!p.legacy && !memcmp(&memory,&saved_memory,sizeof(memory)));
        assert(cpu.hi==before.hi && cpu.lo==before.lo);
    }
}
static void command_value_guards(void) {
    static const uint32_t sites[]={0x8005b720,0x8005b750};
    for(unsigned i=0;i<2;i++) {
        Probe p={0}; FormatterCpu cpu={0},before;
        MusashiGpuIoDevice io={&p,read_word,write_word};
        MusashiExecutionClock clock={&p,tick};
        pointers(); cpu.pc=sites[i];cpu.npc=cpu.pc+4;
        cpu.gpu_io_device=&io;cpu.clock=&clock;
        cpu.r[2]=i?0x1f8010a8:0x1f801814;
        cpu.r[3]=i?0x01000400:0x04000003;
        before=cpu;saved_memory=memory;
        assert(!formatter_step(&memory,&cpu));
        assert(!memcmp(&cpu,&before,sizeof(cpu)) && !p.ticks && !p.reads && !p.writes);
        assert(!memcmp(&memory,&saved_memory,sizeof(memory)));
    }
}
static void callback_initializer_guard(void) {
    for(unsigned test=0;test<6;test++) {
        Probe p={0}; FormatterCpu cpu={0},before;
        MusashiExecutionClock clock={&p,tick};
        pointers();
        assert(musashi_boot_write32(&memory,0x8006cb84,0x8006cb64));
        assert(musashi_boot_write32(&memory,0x8006cb70,0x80042718));
        assert(musashi_boot_write16(&memory,0x8006bafc,1));
        cpu.pc=0x80042580;cpu.npc=cpu.pc+4;cpu.clock=&clock;
        cpu.r[29]=0x801fff18;cpu.r[31]=0x800526a8;
        cpu.r[16]=0x01234567;cpu.hi=0x13579bdf;cpu.lo=0x2468ace0;
        /* Actually execute the wrapper's two source pointer loads/prologue. */
        for(unsigned i=0;i<6;i++) assert(formatter_step(&memory,&cpu));
        assert(cpu.pc==0x80042598 && cpu.r[2]==0x80042718 && p.cycles==15);
        if(test==1) { /* Same valid table contents at a foreign live base. */
            assert(musashi_boot_write32(&memory,0x8007800c,0x80042718));
            assert(musashi_boot_write32(&memory,0x8006cb84,0x80078000));
        }
        if(test==2) assert(musashi_boot_write32(&memory,0x8006cb70,0x80059658));
        if(test==3) cpu.r[2]=0x80059658;
        if(test==4) { /* Matching live slot and loaded mapped alias. */
            assert(musashi_boot_write32(&memory,0x8006cb70,0x80059658));
            cpu.r[2]=0x80059658;
        }
        if(test==5) p.refuse_clock=1;
        before=cpu;saved_memory=memory;p.ticks=p.cycles=0;
        int accepted=formatter_step(&memory,&cpu);
        if(test) {
            assert(!accepted && !memcmp(&cpu,&before,sizeof(cpu)));
            assert(!p.cycles && p.ticks==(test==5?1u:0u));
            assert(!memcmp(&memory,&saved_memory,sizeof(memory)));
        } else {
            assert(accepted && cpu.pc==0x8004259c && cpu.npc==0x80042718);
            assert(cpu.r[31]==0x800425a0 && p.cycles==2);
            assert(formatter_step(&memory,&cpu)); /* Actual NOP delay. */
            assert(cpu.pc==0x80042718 && cpu.npc==0x8004271c && p.cycles==4);
            unsigned limit=40;
            while(cpu.pc!=0x800526a8 && limit--) assert(formatter_step(&memory,&cpu));
            assert(limit && cpu.r[2]==0 && cpu.r[29]==0x801fff18 && cpu.r[31]==0x800526a8);
            assert(cpu.r[16]==0x01234567 && cpu.hi==0x13579bdf && cpu.lo==0x2468ace0);
            /* Actual source guard1 skips native hook/install service calls. */
        }
    }
}
static void actual_control_then_unowned_dma(void) {
    Probe p={0}; FormatterCpu cpu={0}, before;
    MusashiGpuBackend backend={.userdata=&p,.reset=reset,.draw_mode=mode,
        .display_enable=display,.clear_fifo=reset,.ready=reset};
    MusashiGpuIoDevice io={&p,read_word,write_word};
    MusashiExecutionClock clock={&p,tick};
    pointers(); saved_memory=memory;
    assert(musashi_gpu_controller_init(&p.gpu,&backend)); p.real_gpu=1;
    cpu.pc=0x8005b710;cpu.npc=cpu.pc+4;cpu.gpu_io_device=&io;cpu.clock=&clock;
    cpu.r[4]=0x800a6454;cpu.r[5]=0;cpu.r[29]=0x801ffed0;cpu.r[31]=0x8005b8c4;
    for(unsigned i=0;i<8;i++) assert(formatter_step(&memory,&cpu));
    assert(cpu.pc==0x8005b730 && p.writes==1 && p.address==0x1f801814 && p.value==0x04000002);
    assert(p.gpu.dma_direction==2 && !p.gpu.faulted && p.cycles==19);
    before=cpu;
    assert(!formatter_step(&memory,&cpu) && !memcmp(&cpu,&before,sizeof(cpu)));
    assert(p.cycles==22 && p.writes==2 && p.reads==0);
    assert(p.address==0x1f8010a0 && p.value==0x800a6454);
    assert(cpu.r[29]==0x801ffed0 && cpu.r[31]==0x8005b8c4);
    assert(!memcmp(&memory,&saved_memory,sizeof(memory)) && p.gpu.dma_direction==2);
    /* No DMA owner, data movement, interrupt or completion is supplied. */
}
#ifdef DRAW_MMIO_BAD_WORD
static void mutated_word_guard(void) {
    Probe p={0}; FormatterCpu cpu={0},before;
    MusashiGpuIoDevice io={&p,read_word,write_word};
    MusashiExecutionClock clock={&p,tick};
    pointers();cpu.clock=&clock;cpu.gpu_io_device=&io;
    assert(musashi_boot_write32(&memory,0x8006cb84,0x8006cb64));
    assert(musashi_boot_write32(&memory,0x8006cb70,0x80042718));
    cpu.pc=DRAW_MMIO_BAD_WORD==1?0x8005b740:0x80042598;
    cpu.npc=cpu.pc+4;
    cpu.r[2]=DRAW_MMIO_BAD_WORD==1?0x1f8010a4:0x80042718;
    cpu.r[3]=0x80042718;cpu.r[4]=0x12345678;
    before=cpu;saved_memory=memory;
    assert(!formatter_step(&memory,&cpu));
    assert(!memcmp(&cpu,&before,sizeof(cpu)) && !memcmp(&memory,&saved_memory,sizeof(memory)));
    assert(!p.ticks && !p.reads && !p.writes);
    puts("DRAW_WORD_GUARD_PASS");
}
#endif
int main(void) {
#ifdef DRAW_MMIO_BAD_WORD
    mutated_word_guard();
    return 0;
#endif
    routing(); instructions(); command_value_guards(); callback_initializer_guard(); actual_control_then_unowned_dma();
    puts("DRAW_MMIO_PASS routing=1 fetched_cases=38 callback_cases=6 real_control=1 dma=UNOWNED fixture_only=1");
    return 0;
}
