#include "../pc_port/mips_formatter.c"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    TIMER2_COUNT = 0x1f801120u,
    TIMER2_MODE = 0x1f801124u,
    TIMER2_TARGET = 0x1f801128u,
    START_WORD = 0x80078f24u,
    LIMIT_WORD = 0x800c5320u,
};

typedef struct TimerFixture {
    uint16_t count;
    uint16_t mode;
    uint16_t target_values[2];
    unsigned target_reads;
    unsigned count_reads;
    unsigned mode_reads;
    uint32_t fail_address;
} TimerFixture;

static int timer_read16(void *userdata, uint32_t address, uint16_t *value) {
    TimerFixture *fixture = userdata;
    if (!fixture || !value || address == fixture->fail_address) return 0;
    if (address == TIMER2_COUNT) {
        fixture->count_reads++;
        *value = fixture->count;
        return 1;
    }
    if (address == TIMER2_MODE) {
        fixture->mode_reads++;
        *value = fixture->mode;
        return 1;
    }
    if (address == TIMER2_TARGET) {
        unsigned index = fixture->target_reads++;
        *value = fixture->target_values[index < 2u ? index : 1u];
        return 1;
    }
    return 0;
}

static void write_word(MusashiBootMemory *memory, uint32_t address,
                       uint32_t value) {
    assert(musashi_boot_write32(memory, address, value));
}

static uint32_t read_word(MusashiBootMemory *memory, uint32_t address) {
    uint32_t value = 0;
    assert(musashi_boot_read32(memory, address, &value));
    return value;
}

static void fixture_input(MusashiInputDevice *input, TimerFixture *fixture) {
    memset(input, 0, sizeof(*input));
    input->userdata = fixture;
    input->read16 = timer_read16;
}

static int run_words(MusashiBootMemory *memory, FormatterCpu *cpu,
                     uint32_t start, unsigned budget) {
    unsigned step;
    cpu->pc = start;
    cpu->npc = start + 4u;
    cpu->r[31] = UINT_MAX;
    for (step = 0; step < budget && cpu->pc != UINT_MAX; ++step) {
        if (!formatter_step(memory, cpu)) {
            fprintf(stderr, "step failure start=%08x step=%u pc=%08x\n",
                    start, step, cpu->pc);
            return 0;
        }
    }
    return cpu->pc == UINT_MAX;
}

static int run_start(MusashiBootMemory *memory,
                     const MusashiInputDevice *input, uint32_t limit,
                     uint32_t *old_limit, uint32_t *old_start) {
    FormatterCpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.input_device = input;
    cpu.r[4] = limit;
    if (old_limit) *old_limit = read_word(memory, LIMIT_WORD);
    if (old_start) *old_start = read_word(memory, START_WORD);
    return run_words(memory, &cpu, 0x8005fba8u, 128u);
}

static int run_poll(MusashiBootMemory *memory,
                    const MusashiInputDevice *input, int32_t *published) {
    FormatterCpu cpu;
    int32_t before = *published;
    memset(&cpu, 0, sizeof(cpu));
    cpu.input_device = input;
    if (!run_words(memory, &cpu, 0x8005fbc8u, 128u)) {
        assert(*published == before);
        return 0;
    }
    *published = (int32_t)cpu.r[2];
    return 1;
}

static void test_start_delay_slot_and_refusal(void) {
    MusashiBootMemory memory;
    MusashiInputDevice input;
    TimerFixture fixture;
    uint32_t before_limit, before_start;
    memset(&memory, 0, sizeof(memory));
    memset(&fixture, 0, sizeof(fixture));
    fixture.count = 0x2345u;
    fixture_input(&input, &fixture);
    write_word(&memory, LIMIT_WORD, 0xdeadbeefu);
    write_word(&memory, START_WORD, 0xcafebabeu);
    assert(run_start(&memory, &input, 0xfeedcafeu, NULL, NULL));
    assert(fixture.count_reads == 1u);
    assert(read_word(&memory, LIMIT_WORD) == 0xfeedcafeu);
    assert(read_word(&memory, START_WORD) == 0x2345u);

    memset(&memory, 0, sizeof(memory));
    fixture.fail_address = TIMER2_COUNT;
    fixture.count_reads = fixture.target_reads = fixture.mode_reads = 0;
    write_word(&memory, LIMIT_WORD, 0xdeadbeefu);
    write_word(&memory, START_WORD, 0xcafebabeu);
    before_limit = read_word(&memory, LIMIT_WORD);
    before_start = read_word(&memory, START_WORD);
    assert(!run_start(&memory, &input, 0xfeedcafeu, NULL, NULL));
    assert(fixture.count_reads == 0u);
    assert(read_word(&memory, LIMIT_WORD) == before_limit);
    assert(read_word(&memory, START_WORD) == before_start);
}

static void prepare_poll(MusashiBootMemory *memory, MusashiInputDevice *input,
                         TimerFixture *fixture, uint16_t count, uint16_t mode,
                         uint32_t start, uint32_t limit) {
    memset(memory, 0, sizeof(*memory));
    memset(fixture, 0, sizeof(*fixture));
    fixture->count = count;
    fixture->mode = mode;
    fixture_input(input, fixture);
    write_word(memory, START_WORD, start);
    write_word(memory, LIMIT_WORD, limit);
}

static void test_poll_threshold_and_mode_division(void) {
    MusashiBootMemory memory;
    MusashiInputDevice input;
    TimerFixture fixture;
    int32_t result;

    prepare_poll(&memory, &input, &fixture, 107u, 0u, 100u, 1u);
    result = 0x13579bdf;
    assert(run_poll(&memory, &input, &result));
    assert(result == 0);
    assert(fixture.mode_reads == 1u);

    prepare_poll(&memory, &input, &fixture, 108u, 0u, 100u, 1u);
    result = 0x13579bdf;
    assert(run_poll(&memory, &input, &result));
    assert(result == 1);

    prepare_poll(&memory, &input, &fixture, 101u, 0x200u, 100u, 1u);
    result = 0x13579bdf;
    assert(run_poll(&memory, &input, &result));
    assert(result == 1);
}

static void test_wrap_target_zero_and_double_nonzero_read(void) {
    MusashiBootMemory memory;
    MusashiInputDevice input;
    TimerFixture fixture;
    int32_t result;

    prepare_poll(&memory, &input, &fixture, 2u, 0u, 0xfffeu, 1u);
    fixture.target_values[0] = 0;
    result = 0x2468ace0;
    assert(run_poll(&memory, &input, &result));
    assert(result == 0);
    assert(fixture.target_reads == 1u);

    prepare_poll(&memory, &input, &fixture, 50u, 0u, 100u, 1000u);
    fixture.target_values[0] = 5u;
    fixture.target_values[1] = 1000u;
    result = 0x2468ace0;
    assert(run_poll(&memory, &input, &result));
    assert(result == 0);
    assert(fixture.target_reads == 2u);
}

static void test_poll_failures_preserve_stores_and_result(void) {
    MusashiBootMemory memory;
    MusashiInputDevice input;
    TimerFixture fixture;
    int32_t result;
    uint32_t before_limit, before_start;

    prepare_poll(&memory, &input, &fixture, 120u, 0u, 100u, 2u);
    result = 0x11223344;
    fixture.fail_address = TIMER2_COUNT;
    before_limit = read_word(&memory, LIMIT_WORD);
    before_start = read_word(&memory, START_WORD);
    assert(!run_poll(&memory, &input, &result));
    assert(result == 0x11223344);
    assert(read_word(&memory, LIMIT_WORD) == before_limit);
    assert(read_word(&memory, START_WORD) == before_start);

    prepare_poll(&memory, &input, &fixture, 120u, 0u, 100u, 2u);
    result = 0x11223344;
    fixture.fail_address = TIMER2_MODE;
    before_limit = read_word(&memory, LIMIT_WORD);
    before_start = read_word(&memory, START_WORD);
    assert(!run_poll(&memory, &input, &result));
    assert(result == 0x11223344);
    assert(read_word(&memory, LIMIT_WORD) == before_limit);
    assert(read_word(&memory, START_WORD) == before_start);

    prepare_poll(&memory, &input, &fixture, 2u, 0u, 0xfffeu, 2u);
    result = 0x11223344;
    fixture.fail_address = TIMER2_TARGET;
    before_limit = read_word(&memory, LIMIT_WORD);
    before_start = read_word(&memory, START_WORD);
    assert(!run_poll(&memory, &input, &result));
    assert(result == 0x11223344);
    assert(read_word(&memory, LIMIT_WORD) == before_limit);
    assert(read_word(&memory, START_WORD) == before_start);
}

int main(void) {
    test_start_delay_slot_and_refusal();
    test_poll_threshold_and_mode_division();
    test_wrap_target_zero_and_double_nonzero_read();
    test_poll_failures_preserve_stores_and_result();
    puts("input timer word probe: PASS");
    return 0;
}
