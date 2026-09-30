/* Pinned source instructions with synthetic retained caller frame/DISPENV.
 * Checked provider deliberately refuses GP1: no native boot or scanout proof. */
#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define ENV 0x800a6498u
#define STACK 0x801ff000u
static MusashiBootMemory ram,before_ram;
static unsigned ticks,clock_calls,writes;
static uint32_t last_address,last_word;
static int clock_accept;
static int clock_step(void *p,uint32_t n) {
    (void)p;++clock_calls;if(!clock_accept)return 0;ticks+=n;return 1;
}
static int refuse_write(void *p,uint32_t address,uint32_t value) {
    (void)p;++writes;last_address=address;last_word=value;return 0;
}
static MusashiExecutionClock clock_device={NULL,clock_step};
static MusashiGpuIoDevice io={.userdata=NULL,.write32=refuse_write};
static void put32(uint32_t address,uint32_t value) {assert(musashi_boot_write32(&ram,address,value));}
static uint32_t get32(uint32_t address) {uint32_t value;assert(musashi_boot_read32(&ram,address,&value));return value;}
static void fresh(const uint8_t *exe,size_t size,FormatterCpu *cpu,uint32_t pc) {
    memset(&ram,0xa5,sizeof(ram));assert(musashi_boot_map_exe(&ram,exe,size));
    memset(cpu,0,sizeof(*cpu));cpu->pc=pc;cpu->npc=pc+4;
    cpu->r[29]=STACK;cpu->r[31]=UINT32_MAX;cpu->clock=&clock_device;cpu->gpu_io_device=&io;
    ticks=clock_calls=writes=0;clock_accept=1;last_address=last_word=0;
}
static void getters(const uint8_t *exe,size_t size) {
    const uint32_t values[]={0,1,0x12345678,0xffffffff};
    for(unsigned i=0;i<4;i++) {
        FormatterCpu cpu;fresh(exe,size,&cpu,0x80043078);
        for(unsigned k=1;k<29;k++)cpu.r[k]=0x01010101u*k;
        FormatterCpu before=cpu;put32(0x8006cbe8,values[i]);before_ram=ram;
        for(unsigned n=0;n<4;n++)assert(formatter_step(&ram,&cpu));
        assert(cpu.pc==UINT32_MAX && cpu.r[2]==values[i] && ticks==9 && clock_calls==4 && !writes);
        for(unsigned k=1;k<32;k++)if(k!=2)assert(cpu.r[k]==before.r[k]);
        assert(!memcmp(&ram,&before_ram,sizeof(ram)));
    }
}
static void first_display(const uint8_t *exe,size_t size,int missing) {
    FormatterCpu cpu;fresh(exe,size,&cpu,0x80052704);
    /* Explicit retained52654 frame fixture; actual native continuation separate. */
    cpu.r[2]=0x800a6438;cpu.r[16]=0;cpu.r[17]=240;cpu.r[18]=320;
    cpu.r[19]=cpu.r[20]=4;cpu.r[21]=0;cpu.r[31]=0x80052704;
    put32(STACK+0x28,0x80052610);
    put32(0x8006cbe8,0); /* Source-established NTSC value, synthetic fixture. */
    ram.bytes[0x72788]=ram.bytes[0x7278a]=ram.bytes[0x7278b]=0;
    memset(ram.bytes+0x727f4,0xff,20); /* Earlier5933C source reset effect. */
    memset(ram.bytes+0x78874,0x5a,0x100); /* Detect an unaccepted cache write. */
    memset(ram.bytes+(ENV&0x1fffff),0xa5,20);
    if(missing)cpu.gpu_io_device=NULL;
    before_ram=ram;
    unsigned n;
    for(n=0;n<400 && cpu.pc!=0x8005b690u;n++)assert(formatter_step(&ram,&cpu));
    assert(n<400 && cpu.pc==0x8005b690u && !writes);
    assert(cpu.r[4]==0x05000000u && cpu.r[2]==0x1f801814u && cpu.r[31]==0x8005a088u);
    assert(cpu.r[29]==STACK-0x28 && cpu.r[16]==ENV);
    assert(get32(STACK-8)==0x8005279c && get32(STACK-12)==4);
    assert(get32(STACK-16)==320 && get32(STACK-20)==240 && get32(STACK-24)==ENV);
    assert(get32(STACK+0x28)==0x80052610);
    const uint8_t expected[20]={0,0,0,0,0x40,1,0xf0,0,0,0,0,0,0,0,0,0,0,0,0xa5,0xa5};
    assert(!memcmp(ram.bytes+(ENV&0x1fffff),expected,20));
    assert(ram.bytes[0xc7c88]==4 && ram.bytes[0xc7c89]==0);
    for(unsigned i=0;i<MUSASHI_RAM_SIZE;i++)
        if(!(i>=(ENV&0x1fffff)&&i<(ENV&0x1fffff)+18) &&
           !(i>=(STACK&0x1fffff)-24&&i<(STACK&0x1fffff)-4) &&
           !(i>=0xc7c88&&i<0xc7c8a))assert(ram.bytes[i]==before_ram.bytes[i]);
    FormatterCpu before=cpu;before_ram=ram;
    unsigned prior=ticks,prior_calls=clock_calls;
    assert(!formatter_step(&ram,&cpu));
    assert(!memcmp(&cpu,&before,sizeof(cpu)) && !memcmp(&ram,&before_ram,sizeof(ram)));
    assert(ticks==prior+3 && clock_calls==prior_calls+1);
    assert(writes==(missing?0u:1u));
    if(!missing)assert(last_address==0x1f801814u && last_word==0x05000000u);
    assert(ram.bytes[0x78879]==0x5a); /* SW refusal precedes actual SB. */
}
static void dispatch_guard(const uint8_t *exe,size_t size,unsigned mutation) {
    FormatterCpu cpu;fresh(exe,size,&cpu,0x8005a080);
    cpu.r[2]=0x8005b684;cpu.r[4]=0x05000000;cpu.r[16]=ENV;
    switch(mutation) {
    case 1:memcpy(ram.bytes+0x78000,ram.bytes+0x72740,0x40);put32(0x80072780,0x80078000);break;
    case 2:put32(0x80072750,0x8005b710);cpu.r[2]=0x8005b710;break;
    case 3:cpu.r[2]=0x8005b710;break;
    case 4:put32(0x80072750,0x8005b710);break;
    case 5:clock_accept=0;break;
    }
    FormatterCpu before=cpu;before_ram=ram;
    if(mutation) {
        assert(!formatter_step(&ram,&cpu));
        assert(!memcmp(&cpu,&before,sizeof(cpu)) && !ticks && !writes);
        assert(clock_calls==(mutation==5?1u:0u));
    } else {
        assert(formatter_step(&ram,&cpu));
        assert(cpu.pc==0x8005a084 && cpu.npc==0x8005b684 && cpu.r[31]==0x8005a088);
        assert(formatter_step(&ram,&cpu));
        assert(cpu.pc==0x8005b684 && cpu.npc==0x8005b688 && cpu.r[31]==0x8005a088);
        assert(cpu.r[4]==0x05000000 && ticks==4 && clock_calls==2 && !writes);
    }
    assert(!memcmp(&ram,&before_ram,sizeof(ram)));
}
static void mmio_guard(const uint8_t *exe,size_t size,unsigned mutation) {
    FormatterCpu cpu;fresh(exe,size,&cpu,0x8005b690);
    cpu.r[2]=0x1f801814;cpu.r[4]=0x05000000;
    switch(mutation) {
    case 0:put32(0x8007285c,0x1f801810);cpu.r[2]=0x1f801810;break;
    case 1:put32(0x8007285c,0x1f8010a8);cpu.r[2]=0x1f8010a8;break;
    case 2:cpu.r[2]=0x1f801810;break;
    case 3:put32(0x8007285c,0x1f801810);break;
    }
    FormatterCpu before=cpu;before_ram=ram;
    assert(!formatter_step(&ram,&cpu));
    assert(!memcmp(&cpu,&before,sizeof(cpu)) && !memcmp(&ram,&before_ram,sizeof(ram)));
    assert(!ticks && !clock_calls && !writes);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);rewind(f);
    uint8_t *exe=malloc((size_t)size);assert(exe);
    assert(fread(exe,1,(size_t)size,f)==(size_t)size);assert(!fclose(f));
    getters(exe,(size_t)size);first_display(exe,(size_t)size,0);first_display(exe,(size_t)size,1);
    for(unsigned m=0;m<6;m++)dispatch_guard(exe,(size_t)size,m);
    for(unsigned m=0;m<4;m++)mmio_guard(exe,(size_t)size,m);
    free(exe);puts("DISPLAY_ENTRY_PASS synthetic_frame=1 gp1_05=REFUSED scanout=NOT_EXECUTED");return 0;
}
