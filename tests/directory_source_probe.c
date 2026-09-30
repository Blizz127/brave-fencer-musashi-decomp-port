/* Source fixture only: test-owned CPU, ISO bytes and post-read parser entry.
 * The real 459E8 miss stops at 45C94; no CD return is fabricated. */
#include "../pc_port/mips_formatter.c"
#include "musashi_disc_media.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STACK 0x801ff000u
#define TABLE 0x80076400u
#define BUFFER 0x80078000u
#define QUERY 0x80079000u
static MusashiBootMemory ram;
static uint8_t *exe;
static size_t exe_size;
typedef struct Clock { unsigned calls, cost; int refuse; MusashiExecutionClock adapter; } Clock;
static int tick(void *p,uint32_t cost) { Clock *c=p; if(c->refuse) return 0; ++c->calls; c->cost+=cost; return 1; }
static void reset(void) { memset(&ram,0,sizeof(ram)); assert(musashi_boot_map_exe(&ram,exe,exe_size)); }
static void put(uint32_t a,uint32_t v) { assert(musashi_boot_write32(&ram,a,v)); }
static uint32_t get(uint32_t a) { uint32_t v; assert(musashi_boot_read32(&ram,a,&v)); return v; }
static uint8_t *span(uint32_t a,unsigned n) { uint8_t *p=musashi_boot_ram_span(&ram,a,n); assert(p); return p; }
static void cpu_init(FormatterCpu *cpu,Clock *clock,uint32_t pc) {
    memset(cpu,0,sizeof(*cpu)); memset(clock,0,sizeof(*clock));
    clock->adapter=(MusashiExecutionClock){clock,tick}; cpu->clock=&clock->adapter;
    cpu->pc=pc; cpu->npc=pc+4; cpu->r[29]=STACK; cpu->r[31]=UINT32_MAX;
    for(unsigned i=16;i<=23;++i) cpu->r[i]=0x12340000+i;
}
static int run(FormatterCpu *cpu,uint32_t target,unsigned limit) {
    for(unsigned i=0;i<limit;++i) {
        if(cpu->pc==target) return 1;
        if(!formatter_step(&ram,cpu)) return 0;
    }
    return 0;
}
static void returned(const FormatterCpu *cpu) {
    assert(cpu->pc==UINT32_MAX && cpu->r[29]==STACK && cpu->r[31]==UINT32_MAX);
    for(unsigned i=16;i<=23;++i) assert(cpu->r[i]==0x12340000+i);
}
static void path(unsigned index,uint32_t parent,const char *name) {
    uint32_t base=0x80076a00u+44u*index;
    put(base+4,parent); memcpy(span(base+12,(unsigned)strlen(name)+1),name,strlen(name)+1);
}
static uint32_t lookup(uint32_t parent,const char *name) {
    FormatterCpu cpu; Clock clock;
    cpu_init(&cpu,&clock,0x80045940); cpu.r[4]=parent; cpu.r[5]=QUERY;
    memcpy(span(QUERY,(unsigned)strlen(name)+1),name,strlen(name)+1);
    assert(run(&cpu,UINT32_MAX,20000)); returned(&cpu); return cpu.r[2];
}
static void lookup_tests(void) {
    FormatterCpu cpu; Clock clock;
    reset(); memset(span(0x80076a00,44*128),0,44*128);
    path(0,1,""); path(1,1,"DATA"); path(2,2,"DATA"); path(3,1,"data");
    assert(lookup(1,"")==1 && lookup(1,"DATA")==2 && lookup(2,"DATA")==3);
    assert(lookup(1,"data")==4 && lookup(1,"ABSENT")==UINT32_MAX);
    path(1,0,"DATA"); assert(lookup(1,"data")==UINT32_MAX); /* zero parent terminates */
    for(unsigned i=0;i<128;++i) path(i,7,"");
    assert(lookup(8,"missing")==UINT32_MAX); /* source128 bound, no name reads */
    reset(); memset(span(0x80076a00,44*128),0,44*128); path(0,1,"AAA");
    cpu_init(&cpu,&clock,0x80045940); cpu.r[4]=1; cpu.r[5]=0x801ffffe;
    memcpy(span(0x801ffffe,2),"AA",2);
    /* KUSEG mirrors 2MB RAM across the first 8MB, so 0x80200000 aliases
     * physical 0 (fixture RAM[0] is 0): the compare mismatches and the
     * lookup reports absent instead of refusing. */
    assert(run(&cpu,UINT32_MAX,1000)); returned(&cpu); assert(cpu.r[2]==UINT32_MAX);
}
static void compare_tests(void) {
    FormatterCpu cpu; Clock clock;
    static const struct { const char *a,*b; uint32_t expected; } cases[]={
        {"same","same",0},{"A","B",UINT32_MAX},{"b","a",1},{"","A",0xffffffbf}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        reset(); cpu_init(&cpu,&clock,0x8005c4dc);
        strcpy((char *)span(QUERY,32),cases[i].a); strcpy((char *)span(QUERY+64,32),cases[i].b);
        cpu.r[4]=QUERY; cpu.r[5]=QUERY+64;
        assert(run(&cpu,UINT32_MAX,1000)); assert(cpu.r[2]==cases[i].expected);
    }
    for(unsigned nulls=0;nulls<3;++nulls) {
        reset(); cpu_init(&cpu,&clock,0x8005c4dc); cpu.r[4]=nulls==1?QUERY:0; cpu.r[5]=nulls==2?QUERY:0;
        assert(run(&cpu,UINT32_MAX,100)); assert(cpu.r[2]==(nulls==0?0:nulls==1?1:UINT32_MAX));
    }
    for(unsigned equal=0;equal<3;++equal) {
        reset(); cpu_init(&cpu,&clock,0x80045640);
        memcpy(span(QUERY,16),"ABCDEFGHIJKLxxx",16); memcpy(span(QUERY+64,16),"ABCDEFGHIJKLyyy",16);
        if(equal==0) span(QUERY+64,16)[11]='!';
        if(equal==2) { span(QUERY,16)[3]=0; span(QUERY+64,16)[3]=0; }
        cpu.r[4]=QUERY; cpu.r[5]=QUERY+64;
        assert(run(&cpu,UINT32_MAX,1000)); assert(cpu.r[2]==(equal!=0));
    }
}
/* Derive the frame through the real prologue, then create a separate parser
 * fixture at its successful-read body. No execution/return of45C94 is claimed. */
static void parser_init(FormatterCpu *cpu,Clock *clock,uint32_t directory) {
    cpu_init(cpu,clock,0x800459e8); cpu->r[4]=directory;
    put(0x8006cf94,0); put(0x800769dc+44*directory,22);
    assert(run(cpu,0x80045c94,100));
    assert(cpu->r[4]==1 && cpu->r[5]==22 && cpu->r[6]==BUFFER);
    assert(cpu->r[29]==STACK-0x40 && cpu->r[31]==0x80045a50);
    cpu->pc=0x80045a88; cpu->npc=0x80045a8c; /* explicit new post-read fixture */
    memset(span(TABLE,24*65),0xa5,24*65);
}
static uint8_t bcd(unsigned n) { return (uint8_t)(((n/10)<<4)|(n%10)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void store_le(uint8_t *p,uint32_t x) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(x>>(8*i)); }
static unsigned oracle(const uint8_t *input,uint8_t output[24*65]) {
    unsigned count=0,position=0; memset(output,0xa5,24*65);
    while(position<2048 && count<64 && input[position]) {
        const uint8_t *r=input+position; uint8_t *o=output+count*24;
        uint32_t frames=le32(r+2)+150;
        o[0]=bcd(frames/4500); o[1]=bcd((frames/75)%60); o[2]=bcd(frames%75);
        memcpy(o+4,r+10,4); /* o[3] is intentionally untouched */
        if(count==0) { o[8]='.'; o[9]=0; }
        else if(count==1) { o[8]='.'; o[9]='.'; o[10]=0; }
        else { assert(r[32]<=16); memcpy(o+8,r+33,r[32]); o[8+r[32]]=0; }
        position+=r[0]; ++count;
    }
    if(count<64) output[count*24+8]=0;
    return count;
}
static unsigned record(uint8_t *out,uint32_t extent,uint32_t size,const char *name) {
    unsigned length=(unsigned)strlen(name); unsigned n=33+length+((33+length)&1);
    memset(out,0,n); out[0]=(uint8_t)n; store_le(out+2,extent); store_le(out+10,size);
    out[32]=(uint8_t)length; memcpy(out+33,name,length); return n;
}
static void parser_case(const uint8_t *input,unsigned expected_count) {
    FormatterCpu cpu; Clock clock; uint8_t expected[24*65];
    reset(); memcpy(span(BUFFER,4096),input,4096); parser_init(&cpu,&clock,1);
    assert(oracle(input,expected)==expected_count);
    assert(run(&cpu,UINT32_MAX,50000)); returned(&cpu);
    assert(cpu.r[2]==1 && get(0x8006cf94)==1);
    assert(!memcmp(span(TABLE,sizeof(expected)),expected,sizeof(expected)));
    /* Real cache hit returns before any sector request/parser mutation. */
    cpu_init(&cpu,&clock,0x800459e8); cpu.r[4]=1;
    assert(run(&cpu,UINT32_MAX,100)); returned(&cpu); assert(cpu.r[2]==1);
    assert(!memcmp(span(TABLE,sizeof(expected)),expected,sizeof(expected)));
}
static void parser_tests(const uint8_t retail[4096]) {
    uint8_t input[4096]={0}; unsigned n=0; FormatterCpu cpu; Clock clock;
    parser_case(input,0);
    n+=record(input+n,22,2048,"\0"); n+=record(input+n,22,2048,"\1");
    n+=record(input+n,12345,0x12345678,"FILE.PAC;1"); n+=record(input+n,0,7,"B");
    parser_case(input,4); parser_case(retail,29);
    memset(input,0,sizeof(input)); memset(input,1,97);
    parser_case(input,64); /* malformed length1 records still obey source count cap */
    /* No identifier-length validation: an oversized name writes beyond its
     * 16-byte output field. The fixture records source behavior, not safety. */
    reset(); memset(input,0,sizeof(input)); n=0;
    n+=record(input+n,22,2048,"."); n+=record(input+n,22,2048,"..");
    n+=record(input+n,50,77,"ABCDEFGHIJKLMNOPQRSTUVWX");
    memcpy(span(BUFFER,4096),input,4096); parser_init(&cpu,&clock,1);
    assert(run(&cpu,UINT32_MAX,10000));
    assert(!memcmp(span(TABLE+48+8,24),"ABCDEFGHIJKLMNOPQRSTUVWX",24));
    /* Sector-end is only a next-record check. A record starting inside the
     * sector can consume fields/name from adjacent mapped RAM. */
    reset(); parser_init(&cpu,&clock,1); memset(span(BUFFER,4096),0,4096);
    record(span(BUFFER+2040,80),75,0x12345678,"CROSS");
    cpu.pc=0x80045ad4; cpu.npc=cpu.pc+4; cpu.r[16]=BUFFER+2040;
    cpu.r[18]=2; cpu.r[17]=48; cpu.r[21]=TABLE; cpu.r[19]=TABLE+56; cpu.r[20]=TABLE+48;
    assert(run(&cpu,UINT32_MAX,10000)); assert(get(TABLE+52)==0x12345678);
    assert(!memcmp(span(TABLE+56,6),"CROSS",6));
    /* By contrast, the KUSEG mirror serves the record tail from physical 0
     * (fixture zeros), so the length-40 record at the top of RAM parses and
     * returns instead of refusing at the second merge group. BCD writes and
     * the untouched o[3] match the oracle. This is not ISO validation. */
    reset(); parser_init(&cpu,&clock,1); cpu.r[16]=0x801ffff8;
    span(0x801ffff8,8)[0]=40; store_le(span(0x801ffffa,4),75);
    assert(run(&cpu,UINT32_MAX,10000)); returned(&cpu);
    assert(cpu.r[2]==1 && get(0x8006cf94)==1);
    assert(span(TABLE,3)[0]==0 && span(TABLE,3)[1]==3 && span(TABLE,3)[2]==0);
    assert(span(TABLE,4)[3]==0xa5);
}
static uint32_t load_bytes(const uint8_t *p) { return le32(p); }
static void merge_tests(void) {
    static const struct { uint32_t pc; unsigned rt,low; } groups[]={
        {0x80045ae4,2,2},{0x80045b0c,3,10}};
    for(unsigned g=0;g<2;++g) for(unsigned offset=0;offset<4;++offset)
        for(unsigned dest_offset=0;dest_offset<4;++dest_offset) {
        FormatterCpu cpu; Clock clock; uint8_t old[4]={0xdd,0xcc,0xbb,0xaa},partial[4];
        uint8_t input[8]={0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe};
        uint8_t expected[8]; uint32_t dest=QUERY+dest_offset,full=load_bytes(input+offset);
        unsigned high=(offset+3)&3u, upper=(offset+3)&~3u;
        reset(); memcpy(span(BUFFER,8),input,8); memset(span(QUERY,8),0xa5,8);
        cpu_init(&cpu,&clock,groups[g].pc); cpu.r[16]=BUFFER+offset-groups[g].low;
        cpu.r[groups[g].rt]=0xaabbccdd;
        memcpy(partial,old,4); memcpy(partial+3-high,input+upper,high+1);
        assert(formatter_step(&ram,&cpu)); assert(cpu.r[groups[g].rt]==0xaabbccdd);
        assert(formatter_step(&ram,&cpu)); assert(cpu.r[groups[g].rt]==load_bytes(partial));
        assert(formatter_step(&ram,&cpu)); assert(cpu.r[groups[g].rt]==full && !cpu.merge_pending);
        if(g==0) cpu.r[29]=dest-0x18; else cpu.r[2]=dest;
        memset(expected,0xa5,8); high=(dest_offset+3)&3u; upper=(dest_offset+3)&~3u;
        memcpy(expected+upper,input+offset+3-high,high+1);
        assert(formatter_step(&ram,&cpu)); assert(!memcmp(span(QUERY,8),expected,8));
        memcpy(expected+dest_offset,input+offset,4-dest_offset);
        assert(formatter_step(&ram,&cpu)); assert(!memcmp(span(QUERY,8),expected,8));
        assert(clock.calls==5 && clock.cost==16);
    }
    for(unsigned g=0;g<2;++g) {
        FormatterCpu cpu,before; Clock clock;
        reset(); cpu_init(&cpu,&clock,groups[g].pc); cpu.r[16]=BUFFER-groups[g].low;
        assert(formatter_step(&ram,&cpu)); before=cpu; clock.refuse=1;
        assert(!formatter_step(&ram,&cpu)); assert(!memcmp(&cpu,&before,sizeof(cpu)));
        assert(clock.calls==1 && clock.cost==3);
        reset(); cpu_init(&cpu,&clock,groups[g].pc); cpu.r[16]=0x1f801800-groups[g].low;
        before=cpu; assert(!formatter_step(&ram,&cpu)); assert(!memcmp(&cpu,&before,sizeof(cpu)) && !clock.calls);
    }
}
static void lb_tests(void) {
    for(unsigned value=0;value<256;++value) {
        FormatterCpu cpu; Clock clock; reset(); cpu_init(&cpu,&clock,0x80045b54);
        cpu.r[3]=QUERY-0x3a1a; *span(QUERY,1)=(uint8_t)value;
        assert(formatter_step(&ram,&cpu));
        assert(cpu.r[3]==(value<128?value:0xffffff00u|value));
        assert(clock.calls==1 && clock.cost==3 && cpu.pc==0x80045b58);
    }
    for(unsigned kind=0;kind<3;++kind) {
        FormatterCpu cpu,before; Clock clock; reset(); cpu_init(&cpu,&clock,0x80045b54);
        /* 0x80800000 is past the first-8MB RAM mirror window: genuinely
         * unmapped. 0x80200000 would alias physical 0 and succeed. */
        cpu.r[3]=(kind==0?0x80800000u:kind==1?0x1f801800u:QUERY)-0x3a1a;
        if(kind==2) clock.refuse=1;
        before=cpu; assert(!formatter_step(&ram,&cpu)); assert(!memcmp(&cpu,&before,sizeof(cpu)));
        assert(clock.calls==0 && clock.cost==0);
    }
}
int main(int argc,char **argv) {
    FILE *file; uint8_t raw[2352],retail[4096]={0}; MusashiDiscMedia *media;
    assert(argc==4 || argc==6); file=fopen(argv[1],"rb"); assert(file);
    exe_size=0x65000; exe=malloc(exe_size); assert(exe);
    assert(fread(exe,1,exe_size,file)==exe_size && fgetc(file)==EOF); assert(!fclose(file));
    if(argc==6) {
        FormatterCpu cpu,before; Clock clock; reset();
        assert(!strcmp(argv[4],"badlb")); cpu_init(&cpu,&clock,(uint32_t)strtoul(argv[5],NULL,16));
        cpu.r[3]=QUERY-0x3a1a; before=cpu;
        assert(!formatter_step(&ram,&cpu)); assert(!memcmp(&before,&cpu,sizeof(cpu)) && !clock.calls);
        free(exe); puts("DIRECTORY_BAD_LB_REFUSED"); return 0;
    }
    media=musashi_disc_media_open_pinned(argv[2],argv[3]); assert(media);
    assert(musashi_disc_media_read_sector(media,22,raw,sizeof(raw))); memcpy(retail,raw+24,2048);
    musashi_disc_media_close(media);
    lookup_tests(); compare_tests(); parser_tests(retail); merge_tests(); lb_tests();
    free(exe); puts("DIRECTORY_SOURCE_PASS fixture_only=1"); return 0;
}
