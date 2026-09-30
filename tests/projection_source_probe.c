/* Actual projection/source queue callers with a synthetic CPU/stack and RAM.
 * No captured post-state, GPU command acceptance, DMA completion or native claim. */
#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static MusashiBootMemory memory, expected, saved;
static FormatterCpu cpu;
static uint32_t refuse_pc;
static unsigned cycles,calls;
static int advance(void *p,uint32_t cost) {
    uint32_t instruction;unsigned opcode;
    (void)p;assert(formatter_fetch(NULL,&cpu,&instruction));opcode=instruction>>26;
    assert(cost==2u+((opcode>=32u&&opcode<=43u)||opcode==46u));
    calls++;if(cpu.pc==refuse_pc)return 0;cycles+=cost;return 1;
}
static MusashiExecutionClock clock_device={NULL,advance};
static void put16(MusashiBootMemory *m,uint32_t p,uint16_t v) {m->bytes[p]=(uint8_t)v;m->bytes[p+1]=(uint8_t)(v>>8);}
static void put32(MusashiBootMemory *m,uint32_t p,uint32_t v) {for(unsigned j=0;j<4;j++)m->bytes[p+j]=(uint8_t)(v>>(8*j));}
static uint32_t word(uint32_t a){uint32_t v;assert(musashi_boot_read32(&memory,a,&v));return v;}
static uint16_t half(uint32_t a){uint16_t v;assert(musashi_boot_read16(&memory,a,&v));return v;}
static void setup(uint32_t width,uint32_t height) {
    memset(&memory,0xa5,sizeof(memory));expected=memory;memset(&cpu,0,sizeof(cpu));
    cpu.pc=0x8005283c;cpu.npc=cpu.pc+4;cpu.r[4]=width;cpu.r[5]=height;
    cpu.r[29]=0x801ff000;cpu.r[30]=0x801ff080;cpu.r[31]=0x8005262c;
    cpu.hi=0x12345678;cpu.lo=0x87654321;cpu.clock=&clock_device;
    cycles=calls=refuse_pc=0;
}
static int until(uint32_t target) {
    for(unsigned n=0;n<500;n++) {
        if(cpu.pc==target)return 1;
        if(!formatter_step(&memory,&cpu))return 0;
    }
    return 0;
}
static void oracle(uint32_t width,uint32_t height) {
    width&=0xffff;height&=0xffff;
    put32(&expected,0xae618,width);put32(&expected,0xae61c,height);
    /* MATRIX has nine halfwords, untouched two-byte padding and three
     * translation words. Source explicitly clears its six off-diagonals. */
    for(unsigned i=0;i<9;i++)put16(&expected,0xae620+2*i,(i%4)==0?0x1000:0);
    for(unsigned i=0;i<3;i++)put32(&expected,0xae634+4*i,0);
    memcpy(expected.bytes+0xae7f0,expected.bytes+0xae620,32);
    memcpy(expected.bytes+0xa63f0,expected.bytes+0xae620,32);
    for(unsigned i=0;i<3;i++)put16(&expected,0xa63f0+8*i,0);
    memcpy(expected.bytes+0xa6558,expected.bytes+0xa63f0,32);
    put16(&expected,0xae7f8,(uint16_t)(((height<<14)/width)/3));
    for(unsigned i=0;i<4;i++)put16(&expected,0xa4f3c+2*i,0);
    put16(&expected,0xa5e50,0);put16(&expected,0xa5e52,0);
    put16(&expected,0xae820,0);put16(&expected,0xae822,0);
    put16(&expected,0xae824,(uint16_t)width);put16(&expected,0xae826,(uint16_t)height);
    expected.bytes[0x78813]=expected.bytes[0x78823]=3;
    expected.bytes[0x78817]=expected.bytes[0x78827]=2;
    put32(&expected,0xc7c70,1);
}
static void projection(void) {
    const uint32_t widths[]={320,160,333,0xabcd0140};
    const uint32_t heights[]={240,240,200,0x123400f0};
    for(unsigned i=0;i<4;i++) {
        uint32_t ratio=((heights[i]&0xffff)<<14)/(widths[i]&0xffff);
        uint64_t product=(uint64_t)ratio*0x55555556u;
        setup(widths[i],heights[i]);oracle(widths[i],heights[i]);
        assert(until(0x8005262c));assert(!memcmp(&memory,&expected,sizeof(memory)));
        assert(cpu.r[29]==0x801ff000&&cpu.r[30]==0x801ff080&&cpu.r[31]==0x8005262c&&cpu.r[2]==1);
        assert(cpu.hi==(uint32_t)(product>>32)&&cpu.lo==(uint32_t)product);
        assert(half(0x800ae632)==0xa5a5&&half(0x800ae802)==0xa5a5&&half(0x800a6402)==0xa5a5&&half(0x800a656a)==0xa5a5);
        if(i==0)assert(ratio==0x3000&&half(0x800ae7f8)==0x1000);
    }
}
static void refusals(void) {
    FormatterCpu prior;
    setup(320,240);assert(until(0x80052868));prior=cpu;saved=memory;
    unsigned old_cycles=cycles,old_calls=calls;refuse_pc=cpu.pc;
    assert(!formatter_step(&memory,&cpu));assert(!memcmp(&prior,&cpu,sizeof(cpu))&&!memcmp(&saved,&memory,sizeof(memory)));
    assert(cycles==old_cycles&&calls==old_calls+1);
    setup(0,240);assert(!until(0x8005262c));assert(cpu.pc==0x80052874);
    assert(word(0x800ae618)==0&&word(0x800ae61c)==240);
    assert(cpu.lo==UINT32_MAX&&cpu.hi==(240u<<14));
    prior=cpu;saved=memory;old_cycles=cycles;old_calls=calls;
    assert(!formatter_step(&memory,&cpu));assert(!memcmp(&prior,&cpu,sizeof(cpu))&&!memcmp(&saved,&memory,sizeof(memory)));
    assert(cycles==old_cycles+2&&calls==old_calls+1); /* Refused BREAK is fetched and charged. */
}
static void queue_callers(void) {
    for(unsigned which=0;which<3;which++) {
        setup(320,240);assert(until(0x8005262c));
        /* Explicit parent-side test inputs: current index0, two offset pairs,
         * flags4 or alternate0. Projection prerequisite is executed above. */
        put16(&memory,0xc7c74,0);put16(&memory,0xc7c88,which==2?0:4);
        put16(&memory,0x794e0,12);put16(&memory,0x794e2,320);
        put16(&memory,0x794e4,(uint16_t)-4);put16(&memory,0x794e6,240);
        cpu.pc=which==0?0x80052d00:0x80052bec;cpu.npc=cpu.pc+4;
        cpu.delay_slot=0;cpu.branch_pc=0;cpu.r[31]=which==0?0x80052634:0x8005263c;
        assert(until(which==2?0x8004921c:0x80059d68));
        if(which==0){
            assert(cpu.r[4]==0x800a6438&&cpu.r[29]==0x801fefe8&&cpu.r[31]==0x80052d80);
            assert(half(0x800a6438)==12&&half(0x800a643a)==(uint16_t)-4);
            assert(half(0x800a643c)==320&&half(0x800a643e)==240);
            assert(word(0x801feff8)==0x80052634);
        } else if(which==1){
            assert(cpu.r[4]==0x800a6438&&cpu.r[29]==0x801fefe0&&cpu.r[31]==0x80052c74);
            assert(half(0x800a6440)==12&&half(0x800a6442)==(uint16_t)-4);
            assert(half(0x800a6548)==0&&half(0x800a654a)==0&&word(0x801feff8)==0x8005263c);
        } else {
            assert(cpu.r[4]==320&&cpu.r[5]==240&&cpu.r[29]==0x801fefe0&&cpu.r[31]==0x80052cd8);
        }
        assert(!cpu.delay_slot&&!cpu.branch_pc);
    }
}
int main(void){projection();refusals();queue_callers();puts("PROJECTION_SOURCE_PASS fixture_only=1 queue_completion=NOT_RUN");return 0;}
