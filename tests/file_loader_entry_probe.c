/* Synthetic CPU entry fixture. The EXE supplies pathname/cache authority;
 * this is not a successful file read or a native lifecycle replay. */
#include "../pc_port/mips_formatter.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cycles;
static int refuse_clock;
static unsigned word_calls;
static int refuse_word;
static uint32_t word_address, word_value;
static int read_word(void *unused,uint32_t address,uint32_t *value) {
    (void)unused; ++word_calls; word_address=address;
    *value=0x12345678; /* Negative provider deliberately touches output. */
    return !refuse_word;
}
static int write_word(void *unused,uint32_t address,uint32_t value) {
    (void)unused; ++word_calls; word_address=address; word_value=value;
    return !refuse_word;
}
static int tick(void *unused, uint32_t cost) {
    (void)unused;
    if (refuse_clock) return 0;
    cycles += cost;
    return 1;
}
static uint32_t word(MusashiBootMemory *m, uint32_t address) {
    uint32_t value;
    assert(musashi_boot_read32(m,address,&value));
    return value;
}
int main(int argc, char **argv) {
    static const uint32_t cleared[] = {0x800ae708,0x800ba1b4,0x800747f0,
        0x800747f4,0x800ae6f0,0x800ae6f8,0x800ae6fc,0x800ae700,
        0x800ae74c,0x800ae70c};
    MusashiBootMemory *m=malloc(sizeof(*m));
    uint8_t *exe=malloc(0x65000);
    MusashiExecutionClock clock={NULL,tick};
    FormatterCpu cpu={0}, before;
    unsigned i, steps=0;
    FILE *file;
    assert(argc==2 && m && exe);
    file=fopen(argv[1],"rb"); assert(file);
    assert(fread(exe,1,0x65000,file)==0x65000 && fgetc(file)==EOF);
    assert(!fclose(file));
    memset(m,0xa5,sizeof(*m));
    assert(musashi_boot_map_exe(m,exe,0x65000));
    cpu.pc=0x8001971c; cpu.npc=cpu.pc+4; cpu.clock=&clock;
    cpu.r[29]=0x801fff00; cpu.r[30]=0x801ffec0; cpu.r[31]=0x80010204;
    for(i=16;i<24;++i) cpu.r[i]=0x11110000+i;
    before=cpu; refuse_clock=1;
    assert(!formatter_step(m,&cpu) && !memcmp(&before,&cpu,sizeof(cpu)) && !cycles);
    assert(word(m,0x801ffefc)==0xa5a5a5a5);
    refuse_clock=0;
    while(cpu.pc!=0x8001974c && steps++<100) assert(formatter_step(m,&cpu));
    assert(cpu.pc==0x8001974c && cpu.r[29]==0x801ffea8);
    assert(word(m,0x801ffefc)==0x80010204 && word(m,0x801ffef8)==0x801ffec0);
    for(i=0;i<sizeof(cleared)/sizeof(cleared[0]);++i) assert(word(m,cleared[i])==0);
    assert(word(m,0x800ae728)==0x80010000 && word(m,0x800ae748)==1);
    /* Both byte clears preserve their untouched neighboring bytes. */
    assert(word(m,0x800ae798)==0xa5a50000);
    assert(word(m,0x800ae704)==0xa5a5a5a5);
    assert(word(m,0x8006cf98)==0xffffffff && word(m,0x8006cc9c)==0);
    while(cpu.pc!=0x80045374 && steps++<140) assert(formatter_step(m,&cpu));
    assert(cpu.pc==0x80045374 && cpu.r[4]==0x80063058 && cpu.r[5]==0x80063044);
    assert(cpu.r[31]==0x80019768 && cpu.r[21]==0);
    assert(!strcmp((char *)musashi_boot_ram_span(m,cpu.r[5],13),"\\DEBUG.BIN;1"));
    while(cpu.pc!=0x80045660 && steps++<180) assert(formatter_step(m,&cpu));
    assert(cpu.pc==0x80045660 && cpu.r[29]==0x801ffe60 && cpu.r[31]==0x800453b4);
    assert(word(m,0x801ffea4)==0x80019768 && word(m,0x801ffefc)==0x80010204);
    assert(word(m,0x8006cf98)==0xffffffff && word(m,0x8006cc9c)==0);
    while(cpu.pc!=0x800435cc && steps++<300) assert(formatter_step(m,&cpu));
    assert(cpu.pc==0x800435cc && cpu.r[29]==0x801ffdf8 && cpu.r[31]==0x80045cc8);
    assert(cpu.r[4]==2 && cpu.r[5]==0x801ffe08 && cpu.r[6]==0);
    assert(cpu.r[16]==0x80078000 && cpu.r[17]==1);
    assert(word(m,cpu.r[5])==0xa5160200); /* LBA16 +150 -> 00:02:16, pad untouched. */
    assert(word(m,0x80078000)==0xa5a5a5a5); /* No sector supplied by fixture. */
    {
        MusashiCdRegisterDevice bridge={.read32=read_word,.write32=write_word};
        static const uint32_t addresses[]={0x1f801018,0x1f8010b0,0x1f8010b4,0x1f8010b8,0x1f8010f0};
        uint32_t value=0xabcdef01;
        assert(!cpu_read32(m,&cpu,0x1f8010b8,&value) && value==0xabcdef01);
        cpu.cd_register_device=&bridge;
        for(i=0;i<sizeof(addresses)/sizeof(addresses[0]);++i) {
            word_calls=0;refuse_word=1;value=0xabcdef01;
            assert(!cpu_read32(m,&cpu,addresses[i],&value));
            assert(value==0xabcdef01 && word_calls==1 && word_address==addresses[i]);
            assert(!cpu_write32(m,&cpu,addresses[i],0x87654321) && word_calls==2);
            refuse_word=0;
            assert(cpu_read32(m,&cpu,addresses[i],&value) && value==0x12345678);
            assert(cpu_write32(m,&cpu,addresses[i],0x87654321) && word_value==0x87654321);
        }
        word_calls=0;
        assert(!cpu_read32(m,&cpu,0x1f8010b9,&value));
        assert(!cpu_write32(m,&cpu,0x1f8010b9,0));
        assert(!cpu_read32(m,&cpu,0x1f8010a0,&value));
        assert(!cpu_write32(m,&cpu,0x1f8010a0,0) && !word_calls);
        assert(cpu_write32(m,&cpu,0x1f801020,0x1323) && word_calls==1);
        assert(word_address==0x1f801020 && word_value==0x1323);
    }
    free(exe);free(m);
    puts("FILE_LOADER_ENTRY_PASS fixture_only=1 sector=16 transfer=NOT_RUN");
    return 0;
}
