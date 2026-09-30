#include "../pc_port/mips_formatter.c"
/* Former compiled word tables: words now come from the local extract. */
#include "probe_code_words.h"
#define kFile80045660Words PROBE_WORDS(0x80045660u, "MAIN", kFile80045660Words)
#define kOverlay0004_800D24D0Words PROBE_WORDS(0x800d24d0u, "MAIN_0004", kOverlay0004_800D24D0Words)
#define kOverlaySc02_8012A094Words PROBE_WORDS(0x8012a094u, "SC02_031", kOverlaySc02_8012A094Words)
#define kOverlaySc02_8013D53CWords PROBE_WORDS(0x8013d53cu, "SC02_031", kOverlaySc02_8013D53CWords)

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    P_LWL = 0x80045720u,
    P_LWR = 0x80045724u,
    P_NOP = 0x80045728u,
    P_SWL = 0x8004572cu,
    P_SWR = 0x80045730u,
    P_LWL_RECORD = 0x800457e4u,
    P_LWR_RECORD = 0x800457e8u,
    P_NOP_RECORD = 0x800457ecu,
    P_SWL_RECORD = 0x800457f0u,
    P_SWR_RECORD = 0x800457f4u,
    DATA_BASE = 0x80078000u,
};

typedef struct Clock {
    unsigned calls;
    unsigned total;
    int refuse;
    MusashiExecutionClock adapter;
} Clock;

static int advance_clock(void *userdata, uint32_t cost) {
    Clock *clock = userdata;
    if (clock->refuse) return 0;
    ++clock->calls;
    clock->total += cost;
    return 1;
}

static uint32_t read_word(MusashiBootMemory *memory, uint32_t address) {
    uint32_t value = 0;
    assert(musashi_boot_read32(memory, address, &value));
    return value;
}

static void write_word(MusashiBootMemory *memory, uint32_t address,
                       uint32_t value) {
    assert(musashi_boot_write32(memory, address, value));
}

static uint32_t merge_lwl(uint32_t memory, uint32_t prior, unsigned shift) {
    static const uint32_t mask[4] = {0x00ffffffu, 0x0000ffffu,
                                     0x000000ffu, 0x00000000u};
    static const unsigned amount[4] = {24u, 16u, 8u, 0u};
    return (memory << amount[shift]) | (prior & mask[shift]);
}

static uint32_t merge_lwr(uint32_t memory, uint32_t prior, unsigned shift) {
    static const uint32_t mask[4] = {0x00000000u, 0xff000000u,
                                     0xffff0000u, 0xffffff00u};
    static const unsigned amount[4] = {0u, 8u, 16u, 24u};
    return (memory >> amount[shift]) | (prior & mask[shift]);
}

static uint32_t merge_swl(uint32_t memory, uint32_t value, unsigned shift) {
    static const uint32_t mask[4] = {0xffffff00u, 0xffff0000u,
                                     0xff000000u, 0x00000000u};
    static const unsigned amount[4] = {24u, 16u, 8u, 0u};
    return (value >> amount[shift]) | (memory & mask[shift]);
}

static uint32_t merge_swr(uint32_t memory, uint32_t value, unsigned shift) {
    static const uint32_t mask[4] = {0x00000000u, 0x000000ffu,
                                     0x0000ffffu, 0x00ffffffu};
    static const unsigned amount[4] = {0u, 8u, 16u, 24u};
    return (value << amount[shift]) | (memory & mask[shift]);
}

static uint32_t byte_oracle(uint32_t first, uint32_t second, unsigned offset) {
    uint8_t bytes[8];
    uint32_t result;
    bytes[0] = (uint8_t)first;
    bytes[1] = (uint8_t)(first >> 8);
    bytes[2] = (uint8_t)(first >> 16);
    bytes[3] = (uint8_t)(first >> 24);
    bytes[4] = (uint8_t)second;
    bytes[5] = (uint8_t)(second >> 8);
    bytes[6] = (uint8_t)(second >> 16);
    bytes[7] = (uint8_t)(second >> 24);
    result = (uint32_t)bytes[offset];
    result |= (uint32_t)bytes[offset + 1u] << 8;
    result |= (uint32_t)bytes[offset + 2u] << 16;
    result |= (uint32_t)bytes[offset + 3u] << 24;
    return result;
}

static void init_cpu(FormatterCpu *cpu, Clock *clock, uint32_t pc) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->pc = pc;
    cpu->npc = pc + 4u;
    clock->adapter.userdata = clock;
    clock->adapter.advance = advance_clock;
    cpu->clock = &clock->adapter;
    cpu->r[2] = 0xa1b2c3d4u;
}

static void test_exact_source_words(void) {
    assert(kFile80045660Words[(P_LWL - 0x80045660u) / 4u] == 0x88a20003u);
    assert(kFile80045660Words[(P_LWR - 0x80045660u) / 4u] == 0x98a20000u);
    assert(kFile80045660Words[(P_NOP - 0x80045660u) / 4u] == 0x00000000u);
    assert(kFile80045660Words[(P_SWL - 0x80045660u) / 4u] == 0xaba2001bu);
    assert(kFile80045660Words[(P_SWR - 0x80045660u) / 4u] == 0xbba20018u);
    assert(kFile80045660Words[(P_LWL_RECORD - 0x80045660u) / 4u] == 0x8a230005u);
    assert(kFile80045660Words[(P_LWR_RECORD - 0x80045660u) / 4u] == 0x9a230002u);
    assert(kFile80045660Words[(P_NOP_RECORD - 0x80045660u) / 4u] == 0x00000000u);
    assert(kFile80045660Words[(P_SWL_RECORD - 0x80045660u) / 4u] == 0xa8430003u);
    assert(kFile80045660Words[(P_SWR_RECORD - 0x80045660u) / 4u] == 0xb8430000u);
}

static void test_load_pair_all_alignments(void) {
    unsigned offset;
    for (offset = 0; offset < 4u; ++offset) {
        MusashiBootMemory memory = {0};
        FormatterCpu cpu;
        Clock clock = {0};
        uint32_t first = 0x10203040u;
        uint32_t second = 0x50607080u;
        uint32_t base = DATA_BASE + (offset & ~3u);
        uint32_t lwr_address = base + offset;
        uint32_t lwl_address = lwr_address + 3u;
        uint32_t first_aligned = lwl_address & ~3u;
        uint32_t expected_lwl;
        uint32_t expected_lwr;
        uint32_t expected_full = byte_oracle(first, second, offset);

        write_word(&memory, base, first);
        write_word(&memory, base + 4u, second);
        init_cpu(&cpu, &clock, P_LWL);
        cpu.r[5] = lwr_address;
        assert(formatter_step(&memory, &cpu));
        expected_lwl = merge_lwl(read_word(&memory, first_aligned),
                                 0xa1b2c3d4u, lwl_address & 3u);
        assert(clock.calls == 1u && clock.total == 3u);
        assert(cpu.r[2] == 0xa1b2c3d4u);

        assert(formatter_step(&memory, &cpu));
        expected_lwr = merge_lwr(read_word(&memory, base), expected_lwl,
                                 lwr_address & 3u);
        assert(cpu.r[2] == expected_lwl);
        assert(clock.calls == 2u && clock.total == 6u);

        assert(formatter_step(&memory, &cpu));
        assert(cpu.r[2] == expected_lwr);
        assert(cpu.r[2] == expected_full);
        assert(clock.calls == 3u && clock.total == 8u);

        /* Repeat the source's second record pair with its distinct offsets. */
        init_cpu(&cpu, &clock, P_LWL_RECORD);
        cpu.r[3] = 0xbad0cafeu;
        cpu.r[17] = DATA_BASE + offset - 2u;
        assert(formatter_step(&memory, &cpu));
        assert(cpu.r[3] == 0xbad0cafeu);
        assert(formatter_step(&memory, &cpu));
        assert(formatter_step(&memory, &cpu));
        assert(cpu.r[3] == expected_full);
    }
}

static void test_store_pair_all_alignments(void) {
    unsigned offset;
    for (offset = 0; offset < 4u; ++offset) {
        MusashiBootMemory memory = {0};
        FormatterCpu cpu;
        Clock clock = {0};
        uint32_t old = 0x11223344u;
        uint32_t value = 0xa1b2c3d4u;
        uint32_t base = DATA_BASE + 0x20u;
        uint32_t swr_address;
        uint32_t swl_address;
        uint32_t stack = base + offset;
        uint32_t swl_aligned;
        uint32_t swr_aligned;
        uint32_t after_swl;
        uint32_t swr_prior;

        swl_address = stack + 0x1bu;
        swr_address = stack + 0x18u;

        swl_aligned = swl_address & ~3u;
        swr_aligned = swr_address & ~3u;
        write_word(&memory, swl_aligned, old);
        if (swr_aligned != swl_aligned)
            write_word(&memory, swr_aligned, old);
        init_cpu(&cpu, &clock, P_SWL);
        cpu.r[29] = stack;
        cpu.r[2] = value;
        assert(formatter_step(&memory, &cpu));
        after_swl = merge_swl(old, value, swl_address & 3u);
        assert(read_word(&memory, swl_aligned) == after_swl);

        init_cpu(&cpu, &clock, P_SWR);
        cpu.r[29] = stack;
        cpu.r[2] = value;
        assert(formatter_step(&memory, &cpu));
        swr_prior = swr_aligned == swl_aligned ? after_swl : old;
        assert(read_word(&memory, swr_aligned) ==
               merge_swr(swr_prior, value, swr_address & 3u));
        assert(clock.calls == 2u && clock.total == 8u);
    }
}

static void test_refusal_preserves_pending_and_clock(void) {
    MusashiBootMemory memory = {0};
    FormatterCpu cpu;
    Clock clock = {0};
    uint32_t before;
    write_word(&memory, DATA_BASE, 0x10203040u);
    init_cpu(&cpu, &clock, P_LWL);
    cpu.r[5] = DATA_BASE;
    assert(formatter_step(&memory, &cpu));
    before = cpu.r[2];
    cpu.pc = P_NOP;
    cpu.npc = P_NOP + 4u;
    assert(!formatter_step(&memory, &cpu));
    assert(cpu.r[2] == before);
    assert(clock.calls == 1u && clock.total == 3u);
}

static void test_mmio_and_end_refusal(void) {
    MusashiBootMemory memory = {0};
    FormatterCpu cpu;
    Clock clock = {0};
    uint32_t before = 0xa1b2c3d4u;

    init_cpu(&cpu, &clock, P_LWL);
    cpu.r[5] = 0x1f801000u;
    cpu.r[2] = before;
    assert(!formatter_step(&memory, &cpu));
    assert(cpu.r[2] == before && clock.calls == 0u);

    init_cpu(&cpu, &clock, P_SWL);
    cpu.r[29] = 0x1f801000u - 0x1bu;
    assert(!formatter_step(&memory, &cpu));
    assert(clock.calls == 0u);

    /* End of the mirrored RAM window: 0x80200000 would alias physical 0
     * and succeed. */
    init_cpu(&cpu, &clock, P_LWL);
    cpu.r[5] = 0x807ffffdu;
    assert(!formatter_step(&memory, &cpu));
    assert(clock.calls == 0u);
}

static void test_movie_header_copy(void) {
    MusashiBootMemory memory={0};FormatterCpu cpu;Clock clock;unsigned site,offset,n;
    for(site=0;site<2;site++)for(offset=0;offset<4;offset++) {
        uint8_t *src=musashi_boot_ram_span(&memory,DATA_BASE,16);
        uint8_t *dst=musashi_boot_ram_span(&memory,DATA_BASE+0x40,16);
        for(n=0;n<16;n++){src[n]=(uint8_t)(n*37u+11u);dst[n]=0xa5;}
        init_cpu(&cpu,&clock,site?0x800469fcu:0x80046fd0u);
        if(site) {
            cpu.r[3]=DATA_BASE+offset-0x1cu;
            cpu.r[6]=DATA_BASE+0x40u+offset;
            cpu.r[2]=0xdeadbeefu;
        } else {
            cpu.r[29]=DATA_BASE+offset-0x28u;
            cpu.r[2]=DATA_BASE+0x40u+offset-0x1cu;
            cpu.r[4]=0xdeadbeefu;
        }
        for(n=0;n<5;n++)assert(formatter_step(&memory,&cpu));
        for(n=0;n<16;n++)assert(dst[n]==(n>=offset&&n<offset+4?src[n]:0xa5));
    }
}

static void test_movie_rectangle_copy(void) {
    static MusashiBootMemory memory;FormatterCpu cpu;Clock clock;unsigned offset,n;
    memset(&memory,0,sizeof(memory));
    probe_load_image(&memory,"MAIN_0004");  /* site groups verify the loaded code */
    for(offset=0;offset<4;offset++) {
        uint8_t *src=musashi_boot_ram_span(&memory,DATA_BASE,16);
        uint8_t *dst=musashi_boot_ram_span(&memory,DATA_BASE+0x40,16);
        for(n=0;n<16;n++){src[n]=(uint8_t)(n*29u+7u);dst[n]=0xa5;}
        init_cpu(&cpu,&clock,0x800d2514u);
        cpu.r[5]=DATA_BASE+offset;
        cpu.r[29]=DATA_BASE+0x40u+offset-0x10u;
        /* MAIN4 fetches require the actual loaded instruction identity. */
        g_overlay_0004_words=1;
        for(n=0;n<8;n++) {
            uint32_t word;
            word=kOverlay0004_800D24D0Words[(0x800d2514u-0x800d24d0u)/4u+n];
            assert(musashi_boot_write32(&memory,0x800d2514u+n*4u,word));
        }
        for(n=0;n<8;n++)assert(formatter_step(&memory,&cpu));
        for(n=0;n<16;n++)assert(dst[n]==(n>=offset&&n<offset+8?src[n]:0xa5));
        g_overlay_0004_words=0;
    }
}

static void test_movie_inline_callback_guard(void) {
    MusashiBootMemory memory={0};FormatterCpu cpu={0};
    MusashiCdIrqServices services={0};CdIrqReturn frames[4]={{0}};
    uint32_t saved=0x8006cb14u; unsigned i;
    g_overlay_0004_words=1;
    frames[0].target=0x80046610u;frames[0].pc=0x80045308u;
    frames[1].pc=0x80046620u;frames[2].pc=0x80046db0u;frames[3].pc=0x8004358cu;
    cpu.pc=0x80046610u;cpu.r[29]=saved-0x100u;cpu.r[4]=1;cpu.r[5]=0x800763e0u;
    assert(musashi_boot_write32(&memory,0x8006cc88u,cpu.pc));
    *musashi_boot_ram_span(&memory,0x8006cf65u,1)=1;
    assert(cd_irq_call(&memory,&cpu,&services,frames,4,saved,0x80044544u,0x0040f809u));
    for(i=0;i<4;i++) {
        frames[i].pc^=4;
        assert(!cd_irq_call(&memory,&cpu,&services,frames,4,saved,0x80044544u,0x0040f809u));
        frames[i].pc^=4;
    }
    cpu.r[4]=2;
    assert(!cd_irq_call(&memory,&cpu,&services,frames,4,saved,0x80044544u,0x0040f809u));
    cpu.r[4]=1;cpu.r[29]-=4;
    assert(!cd_irq_call(&memory,&cpu,&services,frames,4,saved,0x80044544u,0x0040f809u));
    cpu.r[29]+=4;assert(musashi_boot_write32(&memory,0x8006cc88u,0x800433c0u));
    assert(!cd_irq_call(&memory,&cpu,&services,frames,4,saved,0x80044544u,0x0040f809u));
    g_overlay_0004_words=0;
}

static void test_camera_snapshot_merge(void) {
    unsigned offset,n;
    for(offset=0;offset<4;offset++) {
        MusashiBootMemory memory={0};FormatterCpu cpu;Clock clock;
        uint32_t base=DATA_BASE+offset;
        unsigned char *bytes=musashi_boot_ram_span(&memory,DATA_BASE,64);
        unsigned char before[64];
        for(n=0;n<64;n++)bytes[n]=(unsigned char)(n*7u+3u);
        memcpy(before,bytes,64);
        probe_load_image(&memory,"SC02_031");
        g_fetch_memory=&memory;code_sites_invalidate();
        init_cpu(&cpu,&clock,0x8012a0a4u);cpu.r[4]=base;
        g_overlay_sc02_0031_words=1;
        for(n=0;n<8;n++) {
            uint32_t word=kOverlaySc02_8012A094Words[4+n];
            write_word(&memory,0x8012a0a4u+4*n,word);
            assert(merge_kind_for(0x8012a0a4u+4*n,word)!=MERGE_NONE);
            assert(merge_kind_for(0x8012a0a4u+4*n,word^1u)==MERGE_NONE);
        }
        for(n=0;n<8;n++)assert(formatter_step(&memory,&cpu));
        for(n=0;n<64;n++)assert(bytes[n]==
            (n>=offset+24u&&n<offset+32u?before[n+8u]:before[n]));
        g_overlay_sc02_0031_words=0;
    }
}

static void palette_setup(MusashiBootMemory *memory,FormatterCpu *cpu,
                          Clock *clock,unsigned offset,unsigned last) {
    memset(memory,0,sizeof(*memory));memset(clock,0,sizeof(*clock));
    probe_load_image(memory,"SC02_031");  /* site groups verify the loaded code */
    init_cpu(cpu,clock,0x8013d5fcu);
    cpu->r[1]=0x80180000u+offset;cpu->r[2]=offset;cpu->r[6]=0x801b79b8u;
    g_overlay_sc02_0031_words=1;
    for(uint32_t pc=0x8013d5fc;pc<=0x8013d680;pc+=4)
        write_word(memory,pc,kOverlaySc02_8013D53CWords[(pc-0x8013d53cu)/4]);
    uint8_t *src=musashi_boot_ram_span(memory,0x801855a0,16);
    for(unsigned i=0;i<16;i++)src[i]=(uint8_t)(17*i+9);
    src[offset+8]=(uint8_t)last;
    memset(musashi_boot_ram_span(memory,0x801b79b4,20),0xa5,20);
    memset(musashi_boot_ram_span(memory,0x801b7a54,20),0x5a,20);
}
static void palette_refusal(MusashiBootMemory *memory,FormatterCpu *cpu,Clock *clock) {
    FormatterCpu saved=*cpu;unsigned calls=clock->calls,total=clock->total;
    static MusashiBootMemory prior;prior=*memory;
    assert(!formatter_step(memory,cpu));
    assert(!memcmp(&saved,cpu,sizeof(saved)));
    assert(!memcmp(&prior,memory,sizeof(prior)));
    assert(clock->calls==calls&&clock->total==total);
}
static void test_palette_copy_merge(void) {
    static MusashiBootMemory memory;
    const uint32_t sites[]={0x8013d5fc,0x8013d608,0x8013d614,0x8013d620,
        0x8013d630,0x8013d634,0x8013d638,0x8013d63c,
        0x8013d654,0x8013d658,0x8013d65c,0x8013d660,
        0x8013d664,0x8013d668,0x8013d66c,0x8013d670};
    for(unsigned offset=0;offset<4;offset++)for(unsigned tail=0;tail<2;tail++) {
        FormatterCpu cpu;Clock clock;unsigned last=tail?0xff:0x80;
        palette_setup(&memory,&cpu,&clock,offset,last);
        /* Site admission reads the verified code image: bind this memory. */
        g_fetch_memory=&memory;code_sites_invalidate();
        for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++) {
            uint32_t word=read_word(&memory,sites[i]);
            assert(merge_kind_for(sites[i],word)!=MERGE_NONE);
            for(unsigned bit=0;bit<32;bit++)assert(merge_kind_for(sites[i],word^(1u<<bit))==MERGE_NONE);
        }
        for(uint32_t pc=0x8013d5fc;pc<=0x8013d680;pc+=4) {
            assert(cpu.pc==pc);assert(formatter_step(&memory,&cpu));
            if(pc==0x8013d62c)assert(cpu.r[5]==(tail?0xffffffffu:0xffffff80u));
            if(pc==0x8013d674)assert(cpu.r[2]==(tail?0xffffffffu:0xffffff80u));
        }
        const uint8_t *src=musashi_boot_ram_span(&memory,0x801855a0+offset,9);
        const uint8_t *a=musashi_boot_ram_span(&memory,0x801b79b4,20);
        const uint8_t *b=musashi_boot_ram_span(&memory,0x801b7a54,20);
        for(unsigned i=0;i<20;i++) {
            assert(a[i]==(i>=4&&i<13?src[i-4]:0xa5));
            assert(b[i]==(i>=4&&i<13?src[i-4]:0x5a));
        }
    }
    /* Genuine pending states at all eight loads, rejected altered successor,
     * pending target register, and unavailable clock. */
    const uint32_t loads[]={0x8013d5fc,0x8013d608,0x8013d614,0x8013d620,
        0x8013d654,0x8013d658,0x8013d65c,0x8013d660};
    for(unsigned i=0;i<8;i++) {
        FormatterCpu cpu;Clock clock;palette_setup(&memory,&cpu,&clock,1,0x80);
        while(cpu.pc<=loads[i])assert(formatter_step(&memory,&cpu));
        assert(cpu.merge_pending);
        FormatterCpu pending=cpu;
        cpu.npc+=4;palette_refusal(&memory,&cpu,&clock);cpu=pending;
        cpu.merge_reg^=1;palette_refusal(&memory,&cpu,&clock);cpu=pending;
        cpu.merge_kind=cpu.merge_kind==MERGE_LWL?MERGE_LWR:MERGE_LWL;
        palette_refusal(&memory,&cpu,&clock);cpu=pending;
        clock.refuse=1;palette_refusal(&memory,&cpu,&clock);
    }
    for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++) {
        FormatterCpu cpu;Clock clock;palette_setup(&memory,&cpu,&clock,0,0xff);
        cpu.pc=sites[i];cpu.npc=sites[i]+4;
        g_overlay_sc02_0031_words=0;palette_refusal(&memory,&cpu,&clock);
        g_overlay_sc02_0031_words=1;
        write_word(&memory,sites[i],read_word(&memory,sites[i])^1u);
        palette_refusal(&memory,&cpu,&clock);
    }
    /* Out-of-RAM and MMIO accesses must fail before pending publication. */
    for(unsigned i=0;i<2;i++) {
        FormatterCpu cpu;Clock clock;palette_setup(&memory,&cpu,&clock,0,0x80);
        /* 0x80800000 is past the first-8MB RAM mirror window: genuinely
         * out of RAM. 0x80200000 would alias physical 0 and succeed. */
        cpu.r[1]=(i?0x1f801000u:0x80800000u)-0x55a3u;
        palette_refusal(&memory,&cpu,&clock);
        palette_setup(&memory,&cpu,&clock,0,0x80);
        cpu.pc=0x8013d630;cpu.npc=cpu.pc+4;
        cpu.r[6]=(i?0x1f801000u:0x80800000u)-3u;
        palette_refusal(&memory,&cpu,&clock);
    }
    g_overlay_sc02_0031_words=0;
}

int main(void) {
    test_palette_copy_merge();
    test_camera_snapshot_merge();
    test_movie_inline_callback_guard();test_movie_rectangle_copy();test_movie_header_copy();test_exact_source_words();
    test_load_pair_all_alignments();
    test_store_pair_all_alignments();
    test_refusal_preserves_pending_and_clock();
    test_mmio_and_end_refusal();
    puts("PVD merge fixture: exact words, four alignments, delayed pairs, stores, and refusal passed");
    return 0;
}
