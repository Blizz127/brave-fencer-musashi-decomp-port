/* Actual source packet/leaf instructions, explicitly synthetic DRAWENV inputs.
 * No GPU device, DMA completion, native frame or heap service is supplied. */
#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define ENV 0x800a6438u
#define PACKET (ENV+0x1cu)
#define STACK 0x801ff000u
static MusashiBootMemory memory, baseline;
static unsigned ticks, accesses;
static int advance(void *unused, uint32_t amount) {
    (void)unused; ticks += amount; ++accesses; return 1;
}
static MusashiExecutionClock clock_device = {NULL,advance};
static void put32(uint32_t a,uint32_t v) { assert(musashi_boot_write32(&memory,a,v)); }
static void put16(uint32_t a,uint16_t v) { assert(musashi_boot_write16(&memory,a,v)); }
static uint32_t get32(uint32_t a) {
    uint32_t v; assert(musashi_boot_read32(&memory,a,&v)); return v;
}
static void init_cpu(FormatterCpu *cpu,uint32_t pc) {
    memset(cpu,0,sizeof(*cpu)); cpu->pc=pc; cpu->npc=pc+4;
    cpu->r[29]=STACK; cpu->r[31]=UINT32_MAX; cpu->clock=&clock_device;
    ticks=accesses=0;
}
static void load(const uint8_t *exe,size_t size) {
    memset(&memory,0xa5,sizeof(memory)); assert(musashi_boot_map_exe(&memory,exe,size));
    /* Explicit fixture GPU type/limits, normally established by ResetGraph. */
    memory.bytes[0x72788]=0; memory.bytes[0x7278a]=0;
    put16(0x8007278c,1024); put16(0x8007278e,512);
}
static uint32_t leaf(uint32_t pc,uint32_t a,uint32_t b,uint32_t c) {
    FormatterCpu cpu; init_cpu(&cpu,pc);
    cpu.r[4]=a; cpu.r[5]=b; cpu.r[6]=c;
    for(unsigned n=0;n<300 && cpu.pc!=UINT32_MAX;++n) assert(formatter_step(&memory,&cpu));
    assert(cpu.pc==UINT32_MAX && cpu.r[29]==STACK);
    return cpu.r[2];
}
static int signed16(uint32_t v) { v&=0xffff;return v<0x8000?(int)v:(int)v-65536; }
static unsigned clamp(int v,unsigned limit) { return v<0?0u:(unsigned)v>=limit?limit-1:(unsigned)v; }
static uint32_t area(uint32_t code,int x,int y) {
    return code | clamp(x,1024) | (clamp(y,512)<<10);
}

static void packet_case(int x,int y,unsigned w,unsigned h) {
    FormatterCpu cpu;
    uint32_t expected[7]={0x06ffffff,0,0,0xe5000000,0xe1000000,0xe2000000,0xe6000000};
    memset(memory.bytes+(ENV&0x1fffff),0,0x5c);
    put16(ENV,(uint16_t)x); put16(ENV+2,(uint16_t)y);
    put16(ENV+4,(uint16_t)w); put16(ENV+6,(uint16_t)h);
    /* Prior tag/unused bytes must not dictate invented trailing commands. */
    put32(PACKET,0x12345678);
    memset(memory.bytes+((PACKET+28)&0x1fffff),0x5a,0x40-28);
    baseline=memory;
    expected[1]=area(0xe3000000,signed16((uint32_t)x),signed16((uint32_t)y));
    expected[2]=area(0xe4000000,signed16((uint32_t)(uint16_t)x+w-1),
                                      signed16((uint32_t)(uint16_t)y+h-1));
    init_cpu(&cpu,0x80059d68);
    cpu.r[4]=ENV;cpu.r[16]=0x16161616;cpu.r[17]=0x17171717;cpu.r[18]=0x18181818;
    unsigned steps=0;
    for(;steps<1000 && cpu.pc!=0x80059df4u;++steps) assert(formatter_step(&memory,&cpu));
    assert(cpu.pc==0x80059df4u && steps<1000);
    assert(cpu.r[29]==STACK-0x20 && cpu.r[31]==0x80059dc4u);
    assert(cpu.r[16]==PACKET && cpu.r[17]==ENV && cpu.r[18]==0x8007278au);
    assert(get32(STACK-4)==UINT32_MAX && get32(STACK-8)==0x18181818);
    assert(get32(STACK-12)==0x17171717 && get32(STACK-16)==0x16161616);
    assert(get32(STACK-0x28)==0x80059dc4u); /* Packer's saved return. */
    assert(cpu.r[2]==0x8005b7b0u && cpu.r[4]==0x8005b710u);
    assert(cpu.r[5]==PACKET && cpu.r[6]==0x40u);
    for(unsigned i=0;i<7;++i) assert(get32(PACKET+4*i)==expected[i]);
    for(unsigned i=0;i<MUSASHI_RAM_SIZE;++i)
        if (!(i>=(PACKET&0x1fffff)&&i<(PACKET&0x1fffff)+28) &&
            !(i>=(STACK&0x1fffff)-0x70 && i<(STACK&0x1fffff)))
            assert(memory.bytes[i]==baseline.bytes[i]);
}

static void helpers(void) {
    const int coords[]={-32768,-1024,-1,0,1,511,512,1023,1024,32767};
    for(unsigned x=0;x<sizeof(coords)/sizeof(coords[0]);++x)
        for(unsigned y=0;y<sizeof(coords)/sizeof(coords[0]);++y) {
            int a=coords[x],b=coords[y];
            assert(leaf(0x8005ab58,(uint32_t)a,(uint32_t)b,0)==area(0xe3000000,a,b));
            assert(leaf(0x8005ac24,(uint32_t)a,(uint32_t)b,0)==area(0xe4000000,a,b));
            uint32_t packed=leaf(0x8005acf0,(uint32_t)a,(uint32_t)b,0);
            assert(packed==(0xe5000000u|((uint32_t)a&0x7ffu)|(((uint32_t)b&0x7ffu)<<11)));
            /* Independently decode signed11 bits: negative inputs do not clamp. */
            unsigned raw=packed&0x7ff;
            assert((raw<1024?(int)raw:(int)raw-2048)==
                   ((a&0x7ff)<1024?(a&0x7ff):(a&0x7ff)-2048));
        }
    const unsigned pages[]={0,1,0x1ff,0x200,0x400,0x800,0xffff};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);++i)
        for(unsigned d=0;d<3;++d) for(unsigned f=0;f<3;++f)
            assert(leaf(0x8005ab00,f,d,pages[i])==
                (0xe1000000u|(pages[i]&0x9ffu)|(d?0x200u:0)|(f?0x400u:0)));
    const int windows[][4]={{0,0,0,0},{8,16,32,64},{255,248,128,256},
                            {-1,-8,-1,-8},{1024,513,7,9},{31,63,255,-32768}};
    for(unsigned i=0;i<sizeof(windows)/sizeof(windows[0]);++i) {
        const int *v=windows[i];
        for(unsigned k=0;k<4;++k) put16(ENV+2*k,(uint16_t)v[k]);
        uint32_t expected=0xe2000000u|(((unsigned)v[0]&255u)>>3<<10)|
            (((unsigned)v[1]&255u)>>3<<15)|(((unsigned)-v[2]&255u)>>3)|
            ((((unsigned)-v[3]&255u)>>3)<<5);
        assert(leaf(0x8005ad34,ENV,0,0)==expected);
    }
    assert(leaf(0x8005ad34,0,0,0)==0);
}

/* Mutation cases use real mapped targets/valid alternative RAM, not merely
 * invalid pointers whose eventual device failure could conceal a missing guard. */
static void guard_case(const uint8_t *exe,size_t size,unsigned site,unsigned mutation) {
    FormatterCpu cpu,before;load(exe,size);
    uint32_t pc=site?0x8005b8bcu:0x80059df4u;
    uint32_t target=site?0x8005b710u:0x8005b7b0u;
    init_cpu(&cpu,pc);
    if(!site) {
        cpu.r[2]=target;cpu.r[4]=0x8005b710;cpu.r[5]=PACKET;
        cpu.r[6]=0x40;cpu.r[7]=0x77777777;cpu.r[17]=ENV;
        switch(mutation) {
        case 1:memcpy(memory.bytes+0x78000,memory.bytes+0x72740,0x40);put32(0x80072780,0x80078000);break;
        case 2:put32(0x80072748,0x80059658);cpu.r[2]=0x80059658;break;
        case 3:put32(0x80072758,0x80059658);cpu.r[4]=0x80059658;break;
        case 4:cpu.r[2]=0x80059658;break;
        case 5:cpu.r[4]=0x80059658;break;
        case 6:cpu.r[5]+=4;break;
        case 7:cpu.r[6]=0x3c;break;
        case 8:cpu.r[5]=0x801ffff0;cpu.r[17]=cpu.r[5]-0x1c;break;
        case 9:cpu.r[5]++;cpu.r[17]++;break;
        }
    } else {
        cpu.r[4]=PACKET;cpu.r[5]=0x55555555;cpu.r[16]=PACKET;
        cpu.r[17]=0x40;cpu.r[18]=0;cpu.r[19]=target;
        switch(mutation) {
        case 1:put32(0x80072758,0x80059658);break;
        case 2:cpu.r[19]=0x80059658;break;
        case 3:put32(0x80072758,0x80059658);cpu.r[19]=0x80059658;break;
        case 4:cpu.r[4]+=4;break;
        case 5:cpu.r[18]=1;break;
        case 6:cpu.r[17]=0x3c;break;
        case 7:cpu.r[4]=cpu.r[16]=0x801ffff0;break;
        case 8:cpu.r[4]++;cpu.r[16]++;break;
        }
    }
    baseline=memory;before=cpu;
    if(mutation) {
        assert(!formatter_step(&memory,&cpu));
        assert(memcmp(&cpu,&before,sizeof(cpu))==0 && !ticks && !accesses);
    } else {
        assert(formatter_step(&memory,&cpu));
        assert(cpu.pc==pc+4 && cpu.npc==target && cpu.r[31]==pc+8);
        assert(cpu.r[5]==before.r[5] && cpu.r[7]==before.r[7]);
        assert(formatter_step(&memory,&cpu));
        assert(cpu.pc==target && cpu.npc==target+4 && cpu.r[31]==pc+8);
        assert(site?cpu.r[5]==0:cpu.r[7]==0);
        assert(ticks==4 && accesses==2);
    }
    assert(memcmp(&memory,&baseline,sizeof(memory))==0);
}
static void unmapped_queue(void) {
    /* Non-function bytes inside main's text: no formatter range covers them,
     * so the fetch must refuse. (The old 0x8005B7xx "holes" have since been
     * carved into real ranges; do not reuse addresses from carved regions.) */
    const uint32_t holes[]={0x80063000,0x8006a000,0x80070000};
    for(unsigned i=0;i<3;++i) {
        FormatterCpu cpu,before;init_cpu(&cpu,holes[i]);before=cpu;baseline=memory;
        assert(!formatter_step(&memory,&cpu));
        assert(memcmp(&cpu,&before,sizeof(cpu))==0 && !ticks && !accesses);
        assert(memcmp(&memory,&baseline,sizeof(memory))==0);
    }
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);rewind(f);
    uint8_t *exe=malloc((size_t)size);assert(exe);
    assert(fread(exe,1,(size_t)size,f)==(size_t)size);assert(!fclose(f));
    load(exe,(size_t)size);
    packet_case(0,0,0,0);packet_case(0,0,320,240);packet_case(-1,-1,1,1);
    packet_case(1023,511,2,2);packet_case(32767,32767,2,2);
    helpers();
    for(unsigned site=0;site<2;++site)
        for(unsigned m=0;m<(site?9u:10u);++m) guard_case(exe,(size_t)size,site,m);
    unmapped_queue();free(exe);
    puts("DRAW_PACKET_PASS fixture_only=1 gpu_dispatch=NOT_EXECUTED");return 0;
}
