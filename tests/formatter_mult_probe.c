#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static unsigned cycles;
static int refuse;
static int tick(void *unused, uint32_t cost) {
    (void)unused;
    if (refuse) return 0;
    cycles += cost;
    return 1;
}
int main(void) {
    static const uint32_t vectors[][4] = {
        {0,0x66666667,0,0}, {0xffffffff,1,0xffffffff,0xffffffff},
        {0x80000000,0xffffffff,0,0x80000000},
        {0x80000000,0x80000000,0x40000000,0},
        {0x7fffffff,0x7fffffff,0x3fffffff,1},
        {0x80000000,0x7fffffff,0xc0000000,0x80000000},
        {0xffffffff,0xffffffff,0,1}
    };
    MusashiBootMemory *memory=calloc(1,sizeof(*memory));
    MusashiExecutionClock clock={NULL,tick};
    FormatterCpu cpu, before;
    unsigned i, step;
    assert(memory);
    for(i=0;i<sizeof(vectors)/sizeof(vectors[0]);++i) {
        memset(&cpu,0,sizeof(cpu)); cpu.clock=&clock;
        cpu.pc=0x800431dc;cpu.npc=cpu.pc+4;
        cpu.r[16]=vectors[i][0];cpu.r[2]=vectors[i][1];
        cpu.hi=0x12345678;cpu.lo=0x87654321;
        before=cpu;cycles=0;refuse=1;
        assert(!formatter_step(memory,&cpu) && !memcmp(&before,&cpu,sizeof(cpu)) && !cycles);
        refuse=0;
        assert(formatter_step(memory,&cpu));
        assert(cpu.hi==vectors[i][2] && cpu.lo==vectors[i][3] && cycles==2);
        assert(cpu.pc==0x800431e0 && cpu.r[16]==vectors[i][0] && cpu.r[2]==vectors[i][1]);
    }
    /* Retail matrix MULTU must preserve unsigned high bits, including
     * operands that signed MULT interprets as negative. */
    {
        static const uint32_t operands[] = {0, 1, 4096, 0x7fffffff,
                                            0x80000000, 0xfffff4b0, 0xffffffff};
        unsigned j;
        for (i=0;i<sizeof(operands)/sizeof(operands[0]);++i)
        for (j=0;j<sizeof(operands)/sizeof(operands[0]);++j) {
            uint64_t expected=(uint64_t)operands[i]*operands[j];
            memset(&cpu,0,sizeof(cpu)); cpu.clock=&clock;
            cpu.pc=0x80049d78; cpu.npc=cpu.pc+4;
            cpu.r[11]=operands[i]; cpu.r[9]=operands[j];
            cpu.hi=0x12345678; cpu.lo=0x87654321;
            before=cpu; cycles=0; refuse=1;
            assert(!formatter_step(memory,&cpu));
            assert(!memcmp(&before,&cpu,sizeof(cpu)) && !cycles);
            refuse=0;
            assert(formatter_step(memory,&cpu));
            assert(cpu.hi==(uint32_t)(expected>>32) && cpu.lo==(uint32_t)expected);
            assert(cpu.pc==0x80049d7c && cpu.npc==0x80049d80 && cycles==2);
            assert(cpu.r[11]==operands[i] && cpu.r[9]==operands[j]);
        }
    }
    /* Actual source magic-division/shift/add and JAL delay-store for every
     * possible BCD track. The fixture never calls a host conversion helper. */
    for(i=0;i<100;++i) {
        uint8_t value=0xff;
        memset(&cpu,0,sizeof(cpu)); cpu.clock=&clock;
        cpu.pc=0x800431d4;cpu.npc=cpu.pc+4;cpu.r[16]=i;cpu.r[29]=0x801fff00;
        for(step=0;step<18;++step) assert(formatter_step(memory,&cpu));
        assert(cpu.pc==0x80043830 && cpu.r[31]==0x8004321c && cpu.r[4]==0x14);
        assert(read8(memory,0x801fff10,&value));
        assert(value==(i/10)*16+i%10);
    }
    {
        const uint32_t calls[]={0x800442bc,0x800442f0,0x80044544,0x80044578,0x800449b4,0x800449e8};
        for(i=0;i<sizeof(calls)/sizeof(calls[0]);++i) {
            memset(&cpu,0,sizeof(cpu));cpu.clock=&clock;
            cpu.pc=calls[i];cpu.npc=cpu.pc+4;cpu.r[2]=0x8003c598;
            before=cpu;cycles=0;
            assert(!formatter_step(memory,&cpu) && !memcmp(&before,&cpu,sizeof(cpu)) && !cycles);
        }
    }
    free(memory);
    puts("MULT_HILO_AND_RETAIL_BCD_PASS");return 0;
}
