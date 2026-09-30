/* Square0's licensed retail words, real GTE, fixture-owned caller identity. */
#define main prior_gte_fixture_main
#define MUSASHI_GTE_REAL_OPERATOR 1
#include "gte_init_source_probe.c"
#undef main

static int square_write(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t v) {
    return musashi_gte_owner_write_data(&((Fixture*)p)->gte,c,r,v);
}
static int square_read(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t *v) {
    return musashi_gte_owner_read_data(&((Fixture*)p)->gte,c,r,v);
}
static int square_command(void *p,const MusashiCpuContext *c,uint32_t w) {
    return musashi_gte_owner_command(&((Fixture*)p)->gte,c,w);
}
static void square_case(const uint8_t *exe,size_t size,int member77,int allowed) {
    init(exe,size);
    /* Execute the real InitGeom path to establish CU2. */
    assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    f.transfer.write_data=square_write;
    f.transfer.read_data=square_read;
    f.transfer.command=square_command;
    g_overlay_sc01_0000_words=1;
    g_sc01_code=member77?kOverlaySc01Member77CodeWords:kOverlaySc01Member1CodeWords;
    f.cpu.pc=0x80049324;f.cpu.npc=f.cpu.pc+4;
    f.cpu.r[31]=0x8012bcac;f.cpu.r[4]=STACK-64;f.cpu.r[5]=STACK-48;
    int values[]={2,-3,4};
    for(unsigned i=0;i<3;i++)assert(musashi_boot_write32(&f.memory,STACK-64+4*i,(uint32_t)values[i]));
    if(!allowed) {
        /* Shared libgte loads may run for other library callers; the SQR
         * command still requires this member's audited return alias. */
        f.cpu.pc=0x80049334;f.cpu.npc=f.cpu.pc+4;
        FormatterCpu saved=f.cpu;
        assert(!formatter_step(&f.memory,&f.cpu));
        assert(!memcmp(&saved,&f.cpu,sizeof(saved)));
    } else {
        for(uint32_t pc=0x80049324;pc<=0x80049348;pc+=4) {
            assert(f.cpu.pc==pc);assert(formatter_step(&f.memory,&f.cpu));
        }
        assert(f.cpu.pc==0x8012bcac);
        for(unsigned i=0;i<3;i++) {
            uint32_t value;assert(musashi_boot_read32(&f.memory,STACK-48+4*i,&value));
            assert(value==(uint32_t)(values[i]*values[i]));
        }
    }
    g_overlay_sc01_0000_words=0;g_sc01_code=kOverlaySc01_CodeWords;
    close_fixture();
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *fp=fopen(argv[1],"rb");assert(fp);
    assert(!fseek(fp,0,SEEK_END));long n=ftell(fp);assert(n>0);rewind(fp);
    uint8_t *exe=malloc((size_t)n);assert(exe);
    assert(fread(exe,1,(size_t)n,fp)==(size_t)n);fclose(fp);
    square_case(exe,(size_t)n,1,1);square_case(exe,(size_t)n,0,0);
    free(exe);puts("GTE_SQUARE_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED");return 0;
}
