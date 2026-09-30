/* Real rotation/translation leaves under member 77's audited caller pair. */
#define main prior_gte_fixture_main
#include "gte_init_source_probe.c"
#undef main
static void bank_case(const uint8_t *exe,size_t size,int member77) {
    init(exe,size);assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    g_overlay_sc01_0000_words=1;
    g_sc01_code=member77?kOverlaySc01Member77CodeWords:kOverlaySc01Member1CodeWords;
    const uint32_t values[]={0x00011000,0x00030002,0x00051000,0x00070006,0x1000,100,0xffffff38,300};
    for(unsigned i=0;i<8;i++)assert(musashi_boot_write32(&f.memory,STACK-128+4*i,values[i]));
    f.cpu.r[4]=STACK-128;f.cpu.r[31]=0x8017fd4c;
    f.cpu.pc=0x8004914c;f.cpu.npc=f.cpu.pc+4;
    if(member77) {
        for(unsigned i=0;i<12;i++)assert(formatter_step(&f.memory,&f.cpu));
        assert(f.cpu.pc==0x8017fd4c);
        for(unsigned i=0;i<5;i++)assert(gteRegs.CP2C.p[i].d==values[i]);
        f.cpu.pc=0x800491ac;f.cpu.npc=f.cpu.pc+4;
        f.cpu.r[4]=STACK-128;f.cpu.r[31]=0x8017fd54;
        for(unsigned i=0;i<8;i++)assert(formatter_step(&f.memory,&f.cpu));
        assert(f.cpu.pc==0x8017fd54);
        for(unsigned i=0;i<3;i++)assert(gteRegs.CP2C.p[i+5].d==values[i+5]);
    } else {
        while(f.cpu.pc!=0x80049160)assert(formatter_step(&f.memory,&f.cpu));
        FormatterCpu saved=f.cpu;MusashiGteSnapshot a,b;
        assert(musashi_gte_owner_snapshot(&f.gte,&a));
        assert(!formatter_step(&f.memory,&f.cpu));
        assert(!memcmp(&saved,&f.cpu,sizeof(saved)));
        assert(musashi_gte_owner_snapshot(&f.gte,&b));
        assert(!memcmp(a.control,b.control,sizeof(a.control)) && a.write_count==b.write_count);
    }
    g_overlay_sc01_0000_words=0;g_sc01_code=kOverlaySc01_CodeWords;
    close_fixture();
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *fp=fopen(argv[1],"rb");assert(fp);
    assert(!fseek(fp,0,SEEK_END));long n=ftell(fp);assert(n>0);rewind(fp);
    uint8_t *exe=malloc((size_t)n);assert(exe);
    assert(fread(exe,1,(size_t)n,fp)==(size_t)n);fclose(fp);
    bank_case(exe,(size_t)n,1);bank_case(exe,(size_t)n,0);free(exe);
    puts("GTE_BANK_PAIR_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED");return 0;
}
