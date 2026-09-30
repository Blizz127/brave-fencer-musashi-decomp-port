/* Actual 77-word primitive leaf, real GTE, explicit fixture geometry. */
#define main prior_gte_fixture_main
#define MUSASHI_GTE_REAL_OPERATOR 1
#include "gte_init_source_probe.c"
#undef main
static int primitive_write(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t v) {
    return musashi_gte_owner_write_data(&((Fixture*)p)->gte,c,r,v);
}
static int primitive_read(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t *v) {
    return musashi_gte_owner_read_data(&((Fixture*)p)->gte,c,r,v);
}
static int primitive_control(void *p,const MusashiCpuContext *c,uint32_t r,uint32_t *v) {
    return musashi_gte_owner_read_control(&((Fixture*)p)->gte,c,r,v);
}
static int primitive_command(void *p,const MusashiCpuContext *c,uint32_t w) {
    return musashi_gte_owner_command(&((Fixture*)p)->gte,c,w);
}
enum { OT=0x800a0000u,PACKET=0x800b0000u,FACE=0x800b0200u,
       VERTEX=0x800b0400u,NORMAL=0x800b0500u,CONFIG=0x800b0600u };
static void word(uint32_t address,uint32_t value) {
    assert(musashi_boot_write32(&f.memory,address,value));
}
static uint32_t read_word(uint32_t address) {
    uint32_t value;assert(musashi_boot_read32(&f.memory,address,&value));return value;
}
static void setup(const uint8_t *exe,size_t size) {
    init(exe,size);assert(run()==MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    f.transfer.write_data=primitive_write;f.transfer.read_data=primitive_read;
    f.transfer.read_control=primitive_control;f.transfer.command=primitive_command;
    MusashiCpuContext c;assert(musashi_boot_cpu_context(&f.cpu,MUSASHI_CPU_CONTEXT_SOURCE,&c));
    uint32_t controls[32]={0};
    controls[0]=controls[2]=controls[4]=0x1000;
    controls[8]=controls[10]=controls[12]=0x1000;
    controls[16]=controls[18]=controls[20]=0x1000;
    controls[24]=160u<<16;controls[25]=120u<<16;controls[26]=256;
    controls[29]=1365;controls[30]=1024;
    for(unsigned i=0;i<31;i++)
        if(i<8 || i>12)assert(musashi_gte_owner_write_control(&f.gte,&c,i,controls[i]));
    /* The native library owns the light-matrix lease, as in the port. */
    assert(musashi_gte_owner_native_begin(&f.gte));
    for(unsigned i=8;i<=12;i++)gteRegs.CP2C.p[i].d=controls[i];
    musashi_gte_owner_native_end(&f.gte,0);
    const int16_t vertices[3][3]={{-10,-10,512},{10,-10,512},{0,10,512}};
    for(unsigned i=0;i<3;i++) {
        word(VERTEX+8*i,(uint16_t)vertices[i][0]|((uint32_t)(uint16_t)vertices[i][1]<<16));
        word(VERTEX+8*i+4,(uint16_t)vertices[i][2]);
    }
    word(NORMAL,0x04000400);word(NORMAL+4,0x400);
    word(FACE+4,0x20ffffff);word(FACE+8,0);word(FACE+12,0x00020001);
    word(CONFIG+4,OT);word(CONFIG+8,0);word(OT+511*4,0x00abcdef);
    for(unsigned i=0;i<5;i++)word(PACKET+4*i,0xcccccccc);
    word(STACK+0x10,1);word(STACK+0x14,0);word(STACK+0x18,CONFIG);
    f.cpu.r[4]=FACE;f.cpu.r[5]=VERTEX;f.cpu.r[6]=NORMAL;f.cpu.r[7]=PACKET;
    f.cpu.r[29]=STACK;f.cpu.r[31]=0x8005652c;
    f.cpu.pc=0x8004a660;f.cpu.npc=f.cpu.pc+4;
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *fp=fopen(argv[1],"rb");assert(fp);
    assert(!fseek(fp,0,SEEK_END));long n=ftell(fp);assert(n>0);rewind(fp);
    uint8_t *exe=malloc((size_t)n);assert(exe);
    assert(fread(exe,1,(size_t)n,fp)==(size_t)n);fclose(fp);
    setup(exe,(size_t)n);
    unsigned steps=0;
    while(f.cpu.pc!=0x8005652c && steps++<100)assert(formatter_step(&f.memory,&f.cpu));
    assert(f.cpu.pc==0x8005652c && f.cpu.r[2]==FACE+16);
    assert(read_word(PACKET)==0x04abcdef);
    assert(read_word(PACKET+4)==0x203f3f3f);
    assert(read_word(PACKET+8)==((115u<<16)|155));
    assert(read_word(PACKET+12)==((115u<<16)|165));
    assert(read_word(PACKET+16)==((125u<<16)|160));
    assert(read_word(OT+511*4)==(PACKET&0x00ffffff));
    close_fixture();
    setup(exe,(size_t)n);
    /* Reverse the winding: no packet or ordering-table link is emitted. */
    word(FACE+12,0x00010002);steps=0;
    while(f.cpu.pc!=0x8005652c && steps++<100)assert(formatter_step(&f.memory,&f.cpu));
    assert(f.cpu.pc==0x8005652c && f.cpu.r[2]==FACE+16);
    for(unsigned i=0;i<5;i++)assert(read_word(PACKET+4*i)==0xcccccccc);
    assert(read_word(OT+511*4)==0x00abcdef);
    close_fixture();
    setup(exe,(size_t)n);
    f.cpu.pc=0x8004a68c;f.cpu.npc=f.cpu.pc+4;
    /* The fixture fetches from its licensed image seam, not the mutable
     * data-RAM shadow. Change that image to exercise the digest gate. */
    assert(g_code_image);
    g_code_image[0x8004a68c-CODE_IMAGE_BASE+CODE_IMAGE_TEXT]^=4u;
    FormatterCpu saved=f.cpu;MusashiGteSnapshot a,b;
    assert(musashi_gte_owner_snapshot(&f.gte,&a));
    assert(!formatter_step(&f.memory,&f.cpu));
    assert(!memcmp(&saved,&f.cpu,sizeof(saved)));
    assert(musashi_gte_owner_snapshot(&f.gte,&b));
    assert(!memcmp(a.data,b.data,sizeof(a.data)) && a.command_count==b.command_count);
    assert(a.data_write_count==b.data_write_count && a.data_read_count==b.data_read_count);
    close_fixture();free(exe);
    puts("GTE_PRIMITIVE_SOURCE_PASS fixture_only=1 native=NOT_CLAIMED");return 0;
}
