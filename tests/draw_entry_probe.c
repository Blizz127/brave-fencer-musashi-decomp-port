/* Formatter-only post-heap graphics fixture. All guest source instructions
 * before the mode-0 boundary execute through formatter_step. */
#include "../pc_port/mips_formatter.c"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_SP 0x801fffa8u
#define RETURN_PC 0x80014278u
#define STOP_MODE0 0x80059234u

typedef struct Fixture {
    unsigned clock_calls;
    unsigned cycles;
    int refuse_clock;
    MusashiExecutionClock clock;
} Fixture;

static int advance(void *userdata, uint32_t cost) {
    Fixture *fixture = userdata;
    if (fixture->refuse_clock) return 0;
    fixture->clock_calls++;
    fixture->cycles += cost;
    return 1;
}

static uint32_t word(const MusashiBootMemory *memory, uint32_t address) {
    uint32_t value;
    assert(musashi_boot_read32((MusashiBootMemory *)memory, address, &value));
    return value;
}

static uint16_t half(const MusashiBootMemory *memory, uint32_t address) {
    uint16_t value;
    assert(musashi_boot_read16((MusashiBootMemory *)memory, address, &value));
    return value;
}

typedef struct StackWrite {
    uint32_t address;
    uint32_t value;
} StackWrite;

static const StackWrite EXPECTED_STACK_WRITES[] = {
    {0x801fff28u, 0x000000f0u},
    {0x801fff2cu, 0x00000140u},
    {0x801fff30u, 0x10000012u},
    {0x801fff34u, 0x10000013u},
    {0x801fff38u, 0x10000014u},
    {0x801fff3cu, 0x10000015u},
    {0x801fff40u, 0x80052610u},
    {0x801fff58u, 0x00000000u},
    {0x801fff60u, 0x10000010u},
    {0x801fff64u, 0x10000011u},
    {0x801fff68u, 0x80014650u},
    {0x801fff80u, 0x00000000u},
    {0x801fff88u, 0x800143bcu},
    {0x801fffa0u, RETURN_PC},
};

static void seed_fixture_regions(MusashiBootMemory *memory) {
    /* Keep the EXE's source tables and pointer cells, but make the observed
     * stack/record destinations independent sentinel regions. */
    memset(musashi_boot_ram_span(memory, ENTRY_SP - 0x200u, 0x204u), 0xa5, 0x204u);
    memset(musashi_boot_ram_span(memory, 0x800a6518u, 0x40u), 0xa5, 0x40u);
    memset(musashi_boot_ram_span(memory, 0x800af7bcu, 8u), 0xa5, 8u);
}

static void init_cpu(FormatterCpu *cpu, Fixture *fixture) {
    unsigned i;
    memset(cpu, 0, sizeof(*cpu));
    fixture->clock = (MusashiExecutionClock){fixture, advance};
    cpu->clock = &fixture->clock;
    cpu->pc = 0x800143acu;
    cpu->npc = cpu->pc + 4u;
    cpu->r[2] = 0x22222222u;
    cpu->r[4] = 0x44444444u;
    cpu->r[5] = 0x55555555u;
    cpu->r[6] = 0x66666666u;
    cpu->r[7] = 0x77777777u;
    cpu->r[8] = 0x88888888u;
    cpu->r[9] = 0x99999999u;
    cpu->r[10] = 0xaaaaaaaa;
    cpu->r[11] = 0xbbbbbbbb;
    cpu->r[12] = 0xcccccccc;
    cpu->r[13] = 0xdddddddd;
    cpu->r[14] = 0xeeeeeeee;
    cpu->r[15] = 0xffffffffu;
    for (i = 16; i < 24; ++i) cpu->r[i] = 0x10000000u + i;
    cpu->r[28] = 0x28282828u;
    cpu->r[29] = ENTRY_SP;
    cpu->r[30] = 0x30303030u;
    cpu->r[31] = RETURN_PC;
}

static void run_to_mode0(MusashiBootMemory *memory, FormatterCpu *cpu) {
    unsigned steps = 0;
    int saw_145ec = 0;
    int saw_525dc = 0;
    int saw_52654 = 0;
    while (cpu->pc != STOP_MODE0 && steps++ < 2000u) {
        if (cpu->pc == 0x800145ecu) {
            assert(cpu->r[4] == 1u && cpu->r[31] == 0x800143bcu);
            saw_145ec = 1;
        }
        if (cpu->pc == 0x800525dcu) {
            assert(cpu->r[4] == 320u && cpu->r[5] == 240u &&
                   cpu->r[6] == 4u && cpu->r[7] == 0u &&
                   cpu->r[31] == 0x80014650u);
            saw_525dc = 1;
        }
        if (cpu->pc == 0x80052654u) {
            assert(cpu->r[4] == 320u && cpu->r[5] == 240u &&
                   cpu->r[6] == 4u && cpu->r[7] == 0u &&
                   cpu->r[31] == 0x80052610u);
            saw_52654 = 1;
        }
        assert(formatter_step(memory, cpu));
    }
    assert(cpu->pc == STOP_MODE0 && saw_145ec && saw_525dc && saw_52654);
    assert(cpu->npc == STOP_MODE0 + 4u);
    assert(cpu->r[4] == 0u && cpu->r[31] == 0x800526a8u);
    assert(cpu->r[29] == 0x801fff18u);
}

static void test_clock_refusal(const uint8_t *exe, size_t exe_size) {
    MusashiBootMemory memory, before;
    FormatterCpu cpu, cpu_before;
    Fixture fixture = {0};
    Fixture fixture_before;
    memset(&memory, 0xa5, sizeof(memory));
    assert(musashi_boot_map_exe(&memory, exe, exe_size));
    seed_fixture_regions(&memory);
    before = memory;
    init_cpu(&cpu, &fixture);
    cpu_before = cpu;
    fixture.refuse_clock = 1;
    fixture_before = fixture;
    assert(!formatter_step(&memory, &cpu));
    assert(!memcmp(&memory, &before, sizeof(memory)));
    assert(!memcmp(&cpu, &cpu_before, sizeof(cpu)));
    assert(!memcmp(&fixture, &fixture_before, sizeof(fixture)));
}

static void test_draw_path(const uint8_t *exe, size_t exe_size) {
    MusashiBootMemory memory, before_records;
    FormatterCpu cpu;
    Fixture fixture = {0};
    memset(&memory, 0xa5, sizeof(memory));
    assert(musashi_boot_map_exe(&memory, exe, exe_size));
    seed_fixture_regions(&memory);
    before_records = memory;
    init_cpu(&cpu, &fixture);
    run_to_mode0(&memory, &cpu);
    for (uint32_t address = ENTRY_SP - 0x200u; address < ENTRY_SP + 4u;
         address += 4u) {
        uint32_t now = word(&memory, address);
        uint32_t old = word(&before_records, address);
        if (now != old) {
            size_t i;
            int allowed = 0;
            for (i = 0; i < sizeof(EXPECTED_STACK_WRITES) /
                            sizeof(EXPECTED_STACK_WRITES[0]); ++i) {
                if (EXPECTED_STACK_WRITES[i].address == address) {
                    assert(now == EXPECTED_STACK_WRITES[i].value);
                    allowed = 1;
                    break;
                }
            }
            assert(allowed);
        }
    }
    for (size_t i = 0; i < sizeof(EXPECTED_STACK_WRITES) /
                         sizeof(EXPECTED_STACK_WRITES[0]); ++i) {
        assert(word(&memory, EXPECTED_STACK_WRITES[i].address) ==
               EXPECTED_STACK_WRITES[i].value);
        assert(word(&before_records, EXPECTED_STACK_WRITES[i].address) ==
               0xa5a5a5a5u);
    }
    assert(half(&memory, 0x800af7bcu) == 320u);
    assert(half(&memory, 0x800af7beu) == 240u);
    assert(half(&memory, 0x800af7c0u) == 0u);
    /* The 143AC render records remain unpublished before 145EC returns. */
    assert(!memcmp(musashi_boot_ram_span(&memory, 0x800a6518u, 0x40u),
                   musashi_boot_ram_span(&before_records, 0x800a6518u, 0x40u), 0x40u));
    /* The source frames are live at the mode-0 call; the preceding scan
     * rejects every stack mutation outside this exact retail write set. */
    assert(word(&memory, 0x801fff80u) == 0u);
    assert(word(&memory, 0x801fffa0u) == RETURN_PC);
    assert(word(&memory, 0x801fff88u) == 0x800143bcu);
}

int main(int argc, char **argv) {
    FILE *file;
    uint8_t *exe;
    long size;
    assert(argc == 2);
    file = fopen(argv[1], "rb");
    if (!file) return 77;
    assert(!fseek(file, 0, SEEK_END));
    size = ftell(file);
    assert(size > 0 && !fseek(file, 0, SEEK_SET));
    exe = malloc((size_t)size);
    assert(exe && fread(exe, 1, (size_t)size, file) == (size_t)size);
    assert(!fclose(file));
    test_clock_refusal(exe, (size_t)size);
    test_draw_path(exe, (size_t)size);
    free(exe);
    puts("DRAW_ENTRY_PASS fixture_only=1 mode0_boundary=1");
    return 0;
}
