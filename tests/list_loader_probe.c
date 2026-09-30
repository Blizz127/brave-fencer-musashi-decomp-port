/* Source-only fixture: synthetic MMIO and event cadence, real VBlank handler
 * writes the counter. No disc transfer or native boot success is supplied. */
#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static MusashiBootMemory memory;
typedef struct Fixture { unsigned cycles, events; int deliver; } Fixture;
static int advance(void *userdata, uint32_t cost) {
    Fixture *f=userdata;
    f->cycles+=cost;
    if (f->deliver && !f->events && f->cycles>=300u) {
        assert(musashi_boot_call_80042ce8(&memory,NULL,NULL));
        ++f->events;
    }
    return 1;
}
static uint32_t read32(void *userdata,uint32_t address) {
    (void)userdata;
    assert(address==0x1f801814u); return 0;
}
static uint16_t read16(void *userdata,uint32_t address) {
    (void)userdata; assert(address==0x1f801110u); return 77;
}
static uint32_t get(uint32_t address) {
    uint32_t v; assert(musashi_boot_read32(&memory,address,&v)); return v;
}
static void put(uint32_t address,uint32_t value) {
    assert(musashi_boot_write32(&memory,address,value));
}
static void init(FormatterCpu *cpu) {
    memset(cpu,0,sizeof(*cpu)); cpu->r[29]=0x801ff000u;
    cpu->r[31]=UINT32_MAX; cpu->r[16]=0x12345678; cpu->r[17]=0x87654321;
}
int main(int argc,char **argv) {
    FILE *file; uint8_t *exe; long size;
    assert(argc==2); file=fopen(argv[1],"rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); size=ftell(file); assert(size>0);
    rewind(file); exe=malloc((size_t)size); assert(exe);
    assert(fread(exe,1,(size_t)size,file)==(size_t)size); fclose(file);
    for (unsigned deliver=0;deliver<2;++deliver) {
        FormatterCpu cpu; Fixture f={0,0,(int)deliver};
        MusashiExecutionClock clock={&f,advance};
        MusashiGpuDevice gpu={.userdata=&f,.read32=read32};
        MusashiCallbackDevice device={.userdata=&f,.read16=read16,.reset_device=&gpu};
        memset(&memory,0,sizeof(memory)); assert(musashi_boot_map_exe(&memory,exe,(size_t)size));
        for(unsigned i=0;i<8;++i) put(0x8006cb98u+4*i,0);
        put(0x8006cbb8,0); put(0x8006baa0,0); put(0x8006ba9c,0);
        put(0x8006ba94,0x1f801814); put(0x8006ba98,0x1f801110);
        init(&cpu); cpu.pc=0x8004239c; cpu.npc=cpu.pc+4;
        cpu.clock=&clock; cpu.callback_device=&device;
        for(unsigned step=0;step<10000 && cpu.pc!=UINT32_MAX;++step)
            assert(formatter_step(&memory,&cpu));
        if(deliver) {
            assert(cpu.pc==UINT32_MAX && cpu.r[2]==77 && f.events==1);
            assert(cpu.r[29]==0x801ff000 && cpu.r[16]==0x12345678 && cpu.r[17]==0x87654321);
            assert(get(0x8006baa0)==1 && get(0x8006ba9c)==77);
        } else assert(cpu.pc>=0x800424e4 && cpu.pc<0x80042570 && get(0x8006cbb8)==0);
    }
    for(unsigned state=0;state<11;++state) for(unsigned corrupt=0;corrupt<2;++corrupt) {
        FormatterCpu cpu; Fixture f={0}; MusashiExecutionClock clock={&f,advance};
        memset(&memory,0,sizeof(memory)); assert(musashi_boot_map_exe(&memory,exe,(size_t)size));
        put(0x800ae6f0,state);
        if(corrupt) put(0x80072a4c+4*state,0x80019b38+4);
        init(&cpu); cpu.pc=0x80019af8;cpu.npc=cpu.pc+4;cpu.clock=&clock;
        for(unsigned step=0;step<20 && cpu.pc!=0x80019b30;++step) assert(formatter_step(&memory,&cpu));
        assert(cpu.pc==0x80019b30);
        FormatterCpu before=cpu; unsigned cycles=f.cycles;
        if(corrupt || state>=9) {
            assert(!formatter_step(&memory,&cpu));
            assert(memcmp(&before,&cpu,sizeof(cpu))==0 && cycles==f.cycles);
        } else {
            uint32_t target=get(0x80072a4c+4*state);
            assert(formatter_step(&memory,&cpu)); assert(cpu.pc==0x80019b34 && cpu.npc==target);
            assert(formatter_step(&memory,&cpu)); assert(cpu.pc==target);
        }
    }
    free(exe); puts("LIST_LOADER_SOURCE_PASS fixture_only=1"); return 0;
}
