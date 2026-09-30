/* Explicit fixture scheduling of the three retail SC02 transform callees.
 * Real Status initialization, formatter and shared PsyCross GTE operator;
 * this does not claim the intervening overlay caller or native boot ran. */
#define main prior_gte_fixture_main
#define MUSASHI_GTE_REAL_OPERATOR 1
#include "gte_init_source_probe.c"
#undef main

static unsigned dw, dr, cr, commands;
static int reject;
static int wd(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t v) {
    dw++;return reject!=1 && musashi_gte_owner_write_data(&((Fixture*)p)->gte,c,r,v);
}
static int rd(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t *v) {
    dr++;if(reject==2){*v=0xbadc0ffe;return 0;}
    return musashi_gte_owner_read_data(&((Fixture*)p)->gte,c,r,v);
}
static int rc(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t *v) {
    cr++;if(reject==3){*v=0xbadc0ffe;return 0;}
    return musashi_gte_owner_read_control(&((Fixture*)p)->gte,c,r,v);
}
static int command(void *p,const MusashiCpuContext *c,uint32_t w) {
    commands++;return reject!=4 && musashi_gte_owner_command(&((Fixture*)p)->gte,c,w);
}
enum { MATRIX=STACK-128, VECTOR=MATRIX+32, OUTPUT=MATRIX+48, FLAGS=MATRIX+64 };
static void position(uint32_t pc,uint32_t ra) {
    f.cpu.pc=pc;f.cpu.npc=pc+4;f.cpu.r[31]=ra;
    f.cpu.delay_slot=0;f.cpu.branch_pc=0;
}
static void step(uint32_t pc) {
    assert(f.cpu.pc==pc);assert(formatter_step(&f.memory,&f.cpu));
}
static void setup(const uint8_t *exe,size_t size,int saturated) {
    init(exe,size);assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    g_overlay_sc02_0031_words=1;
    f.transfer.write_data=wd;f.transfer.read_data=rd;
    f.transfer.read_control=rc;f.transfer.command=command;
    dw=dr=cr=commands=0;reject=0;
    /* 90 degree Z rotation: (-y,x,z), Q12; translation is full signed32. */
    const uint32_t matrix[]={0xf0000000u,0x10000000u,0,0,0x1000};
    for(unsigned i=0;i<5;i++)assert(musashi_boot_write32(&f.memory,MATRIX+4*i,matrix[i]));
    const int32_t t[]={saturated==1?40000:100,saturated==1?-40000:-200,saturated==2?-300:300};
    for(unsigned i=0;i<3;i++)assert(musashi_boot_write32(&f.memory,MATRIX+20+4*i,(uint32_t)t[i]));
    assert(musashi_boot_write32(&f.memory,VECTOR,0xffec000au)); /* (10,-20,30) */
    assert(musashi_boot_write32(&f.memory,VECTOR+4,30));
    for(unsigned i=0;i<20;i++)f.memory.bytes[(OUTPUT&0x1fffff)+i]=0xcc;
    /* Sentinel upper halves must not leak through architectural IR reads. */
    for(unsigned i=0;i<32;i++)gteRegs.CP2D.p[i].d=0x76540000u+i;
    f.cpu.r[4]=MATRIX;position(0x8004914c,0x8012f170);
    for(uint32_t pc=0x8004914c;pc<=0x80049178;pc+=4)step(pc);
    assert(f.cpu.pc==0x8012f170);
    for(unsigned i=0;i<5;i++)assert(gteRegs.CP2C.p[i].d==matrix[i]);
    position(0x800491ac,0x8012f178);
    for(uint32_t pc=0x800491ac;pc<=0x800491c8;pc+=4)step(pc);
    for(unsigned i=0;i<3;i++)assert(gteRegs.CP2C.p[5+i].d==(uint32_t)t[i]);
    assert(f.cpu.pc==0x8012f178);
    position(0x8004945c,0x8012f188);
    f.cpu.r[4]=VECTOR;f.cpu.r[5]=OUTPUT;f.cpu.r[6]=FLAGS;
    before=f.memory;
}
static void refusal(void) {
    FormatterCpu saved=f.cpu;MusashiGteSnapshot a,b;
    assert(musashi_gte_owner_snapshot(&f.gte,&a));
    assert(!formatter_step(&f.memory,&f.cpu));
    assert(!memcmp(&saved,&f.cpu,sizeof(saved)));
    assert(musashi_gte_owner_snapshot(&f.gte,&b));
    assert(!memcmp(a.control,b.control,sizeof(a.control)));
    assert(!memcmp(a.data,b.data,sizeof(a.data)));
    assert(a.command_count==b.command_count && a.data_write_count==b.data_write_count && a.data_read_count==b.data_read_count);
    assert(!memcmp(&before,&f.memory,sizeof(before)));
}
static void result_case(const uint8_t *exe,size_t size,int saturated) {
    setup(exe,size,saturated);
    for(uint32_t pc=0x8004945c;pc<=0x80049470;pc+=4)step(pc);
    assert(f.cpu.gte_load_pending && f.cpu.gte_load_reg==3);
    step(0x80049474);step(0x80049478);step(0x8004947c);
    uint32_t w;
    assert(musashi_boot_read32(&f.memory,OUTPUT,&w));
    assert(w==(saturated==1?0x80007fffu:0xff420078u));
    assert(musashi_boot_read32(&f.memory,OUTPUT+4,&w)&&w==(saturated==2?0xfffffef2u:0x14au));
    uint32_t flag=saturated==1?0x81800000u:0;
    assert(gteRegs.CP2C.p[31].d==flag);
    uint32_t old=f.cpu.r[2];step(0x80049480);
    assert(f.cpu.r[2]==old && f.cpu.gte_load_pending && f.cpu.gte_load_reg==2 && f.cpu.gte_load_value==flag);
    before=f.memory;
    FormatterCpu pending=f.cpu;
    f.cpu.npc+=4;refusal();f.cpu=pending;
    f.cpu.merge_pending=1;refusal();f.cpu=pending;
    f.refuse_clock_pc=f.cpu.pc;refusal();f.refuse_clock_pc=0;
    step(0x80049484);
    assert(f.cpu.r[2]==flag && !f.cpu.gte_load_pending && f.cpu.delay_slot);
    step(0x80049488);
    assert(f.cpu.pc==0x8012f188 && !f.cpu.delay_slot);
    assert(musashi_boot_read32(&f.memory,FLAGS,&w)&&w==flag);
    assert(dw==2 && dr==3 && cr==1 && commands==1);
    for(unsigned i=0;i<MUSASHI_RAM_SIZE;i++)
        if(!(i>=(FLAGS&0x1fffff)&&i<(FLAGS&0x1fffff)+4))assert(f.memory.bytes[i]==before.bytes[i]);
    close_fixture();
}
static void transform_failures(const uint8_t *exe,size_t size) {
    for(unsigned i=0;i<2;i++) {
        setup(exe,size,0);
        for(uint32_t pc=0x8004945c;pc<=0x8004946c+4*i;pc+=4)step(pc);
        assert(f.cpu.gte_load_pending);
        reject=2;refusal();close_fixture();
    }
    const uint32_t sites[]={0x80049160,0x800491b8,0x8004945c,0x80049468,0x8004946c,0x80049474,0x80049480};
    /* Rotation/translation require their audited caller and selected
     * SC02 identity. The exact library_vector LWC2 at 8004945c is
     * caller independent. Transfer and clock failures refuse at all sites. */
    for(unsigned i=0;i<7;i++)for(unsigned kind=0;kind<4;kind++) {
        setup(exe,size,0);
        position(sites[i],i==0?0x8012f170:i==1?0x8012f178:0x8012f188);
        if(kind==0)f.cpu.r[31]^=4;
        else if(kind==1)f.cpu.cpu_transfer=NULL;
        else if(kind==2)f.refuse_clock_pc=sites[i];
        else g_overlay_sc02_0031_words=0;
        if((kind==0||kind==3)&&i==2) {
            assert(formatter_step(&f.memory,&f.cpu)); assert(f.cpu.pc==sites[i]+4u);
        } else refusal();
        close_fixture();
    }
    for(unsigned i=0;i<4;i++) {
        setup(exe,size,0);
        position((uint32_t[]){0x8004945c,0x8004946c,0x80049480,0x80049468}[i],0x8012f188);
        reject=(int)i+1;refusal();close_fixture();
    }
    for(unsigned i=0;i<2;i++) {
        setup(exe,size,0);
        position(i?0x80049474:0x8004945c,0x8012f188);
        /* 0x80800000 is past the first-8MB RAM mirror window: genuinely
         * out of RAM. 0x80200000 would alias physical 0 and succeed. */
        f.cpu.r[i?5:4]=0x80800000;refusal();close_fixture();
    }
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long size=ftell(file);assert(size>0);rewind(file);
    uint8_t *exe=malloc((size_t)size);assert(exe&&fread(exe,1,(size_t)size,file)==(size_t)size);assert(!fclose(file));
    result_case(exe,(size_t)size,0);result_case(exe,(size_t)size,1);result_case(exe,(size_t)size,2);transform_failures(exe,(size_t)size);
    free(exe);puts("GTE_TRANSFORM_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED");return 0;
}
