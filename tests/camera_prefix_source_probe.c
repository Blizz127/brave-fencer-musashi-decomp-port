/* Actual camera prefix with real persistent CPU/image/kernel/GTE owners.
 * Fixture scheduling selects a retail callsite; intervening native rendering
 * is not claimed. MTC2 and sqrt completion are deliberately unsupported. */
#define main prior_gte_fixture_main
#include "gte_init_source_probe.c"
#undef main

static unsigned camera_visits[8];
static const uint32_t camera_sites[8]={0x800144d4,0x80053308,0x8004923c,
    0x80053f6c,0x80047948,0x8004787c,0x80054340,0x80047d3c};
static int camera_checkpoint(void *p,const void *continuation) {
    for(unsigned i=0;i<8;++i)if(f.cpu.pc==camera_sites[i])camera_visits[i]++;
    return checkpoint(p,continuation);
}
static void camera_case(const uint8_t *exe,size_t size,unsigned which) {
    uint32_t data_before[32], control_before[32];
    /* Explicit preexisting fixture GTE data, before owner lease, not capture. */
    for(unsigned i=0;i<32;++i)gteRegs.CP2D.p[i].d=0x87650000u+i;
    init(exe,size);
    assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(f.cpu.pc==0x80052618 && f.controls==12 && f.image.generation==2);
    for(unsigned i=0;i<32;++i) {
        data_before[i]=gteRegs.CP2D.p[i].d;
        control_before[i]=gteRegs.CP2C.p[i].d;
    }
    if(which==1) {
        /* Synthetic nonzero source-matrix input proves the actualcopy, not
         * coincidence with zero destination. Camera fields still source-set. */
        for(unsigned i=0;i<32;++i)
            f.memory.bytes[0xae7f0+i]=(uint8_t)(0x31u+i*7u);
    }
    before=f.memory;
    memset(camera_visits,0,sizeof(camera_visits));
    f.callbacks.checkpoint=camera_checkpoint;
    f.cpu.pc=0x80014470;f.cpu.npc=f.cpu.pc+4; /* actual JAL144D4 */
    f.checkpoint_pc=0;
    f.observe_boundary=which==2?0x80053fe8:0x80047d3c;
    if(which==2)f.refuse_clock_pc=0x80053fe8;
    MusashiCpuStatus *same_status=f.status;
    assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    assert(f.status==same_status && f.image.generation==2 && f.image.writes==14);
    assert(f.syscalls==2 && f.controls==13 && f.gte.write_count==13);
    assert(f.cycles==f.boundary_cycles);
    assert(f.clock_calls==f.boundary_calls+(which==2));
    assert(f.cpu.pc==(which==2?0x80053fe8u:0x80047d3cu));
    for(unsigned i=0;i<32;++i) {
        assert(gteRegs.CP2D.p[i].d==data_before[i]);
        assert(gteRegs.CP2C.p[i].d==(i==26?300u:control_before[i]));
    }
    uint32_t value;
    assert(musashi_boot_read32(&f.memory,0x80126950,&value)&&value==300);
    for(unsigned i=0;i<8;++i) {
        assert(musashi_boot_read32(&f.memory,0x80126984+4*i,&value));
        assert(value==(i==2?0xfffffed4u:0u));
    }
    assert(!memcmp(f.memory.bytes+0xae688,before.bytes+0xae7f0,32));
    if(which!=2) {
        for(unsigned i=0;i<8;++i)assert(camera_visits[i]==1);
        assert(f.cpu.npc==0x80047d40 && f.cpu.r[31]==0x80054050);
        assert(f.cpu.r[29]==STACK-0xd8 && f.cpu.r[30]==STACK+64);
        assert(f.cpu.r[4]==90000 && !f.cpu.r[5]);
        assert(f.cpu.r[2]==300 && f.cpu.r[3]==90000);
        assert(f.cpu.hi==0 && f.cpu.lo==90000);
        for(unsigned i=0;i<6;++i) {
            assert(musashi_boot_read32(&f.memory,STACK-0xd8+0x10+4*i,&value));
            assert(value==(i==2?0xfffffed4u:0u));
        }
        assert(musashi_boot_read32(&f.memory,STACK-8,&value)&&value==0x80014478);
        assert(musashi_boot_read32(&f.memory,STACK-0x20,&value)&&value==0x80014544);
    } else {
        assert(camera_visits[0]==1&&camera_visits[1]==1&&camera_visits[2]==1&&camera_visits[3]==1);
        for(unsigned i=4;i<8;++i)assert(!camera_visits[i]);
    }
    for(unsigned i=0;i<MUSASHI_RAM_SIZE;++i)
        if(!(i>=0x1feed0&&i<0x1ff000)&&!(i>=0x126950&&i<0x126954)&&
           !(i>=0x126984&&i<0x1269a4)&&!(i>=0xae688&&i<0xae6a8))
            assert(f.memory.bytes[i]==before.bytes[i]);
    MusashiCpuStatusSnapshot cpu_state;
    assert(musashi_cpu_status_snapshot(f.status,&cpu_state));
    assert(cpu_state.sr==0x40000401 && cpu_state.sequence==2 &&
           !cpu_state.active && !cpu_state.faulted && !f.gte.faulted);
    close_fixture();
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long size=ftell(file);assert(size>0);rewind(file);
    uint8_t *exe=malloc((size_t)size);assert(exe);
    assert(fread(exe,1,(size_t)size,file)==(size_t)size);assert(!fclose(file));
    for(unsigned i=0;i<3;++i)camera_case(exe,(size_t)size,i);
    free(exe);puts("CAMERA_PREFIX_SOURCE_PASS fixture_only=1 mtc2=REFUSED");return 0;
}
