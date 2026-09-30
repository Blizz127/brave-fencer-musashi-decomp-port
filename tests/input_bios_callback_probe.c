#include "musashi_boot_memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VERIFIER 0x8005d6ccu
#define HANDLER 0x8005d734u
#define VERIFIER_CALL 0x8005d718u
#define HANDLER_CALL 0x8005d7fcu
#define VERIFIER_CALLBACK 0x8005d6ccu
#define HANDLER_CHILD 0x8005d9c4u
#define I_STAT 0x1f801070u
#define I_MASK 0x1f801074u
#define INPUT_STATE 0x800729bcu
#define OPTIONAL_VERIFIER 0x80072984u
#define HANDLER_ENABLED 0x80072994u
#define HANDLER_ACTIVE 0x800729acu
#define HANDLER_COUNTER 0x800729b0u
#define HANDLER_RESULT 0x800729c4u
#define HANDLER_RECORD_BASE 0x80072990u
#define HANDLER_RECORD_INDEX 0x8007299cu
#define HANDLER_RECORD_OFFSET 0x800729a0u
#define INPUT_COUNTER_A 0x80078998u
#define INPUT_COUNTER_B 0x8007899cu

typedef struct FrameGuard {
    uint8_t before[16];
    MusashiBiosCallbackFrame frame;
    uint8_t after[16];
} FrameGuard;

typedef struct InputFixture {
    MusashiBootMemory *memory;
    FrameGuard *guard;
    MusashiInputDevice *device;
    uint32_t mask;
    uint32_t stat;
    unsigned read_count;
    unsigned write_count;
    int refuse_reads;
    int recurse;
    int recurse_done;
    int recurse_return;
    unsigned serial_writes;
    unsigned timer_reads;
    int timer_available;
    int32_t recurse_output;
    MusashiResetGraphPrefixStop recurse_stop;
} InputFixture;

static int serial_write16(void *userdata, uint32_t address, uint16_t value) {
    InputFixture *fixture = userdata;
    static const uint32_t addresses[] = {
        0x1f80104au, 0x1f80104au, 0x1f801048u, 0x1f80104eu, 0x1f80104au
    };
    static const uint16_t values[] = {0x40, 0, 0x0d, 0x88, 0x1003};
    unsigned i = fixture->serial_writes++;
    assert(i < 5 && address == addresses[i] && value == values[i]);
    return 1;
}

static int serial_read16(void *userdata, uint32_t address, uint16_t *value) {
    InputFixture *fixture = userdata;
    assert(address == 0x1f801120u);
    ++fixture->timer_reads;
    if (!fixture->timer_available) return 0;
    *value = 0x4567; /* Explicit unit-test device input, never production seed. */
    return 1;
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

static int input_read32(void *userdata, uint32_t address, uint32_t *value) {
    InputFixture *fixture = userdata;
    if (!fixture || !value || fixture->refuse_reads ||
        (address != I_STAT && address != I_MASK)) return 0;
    fixture->read_count++;
    if (fixture->recurse && !fixture->recurse_done) {
        int32_t nested_output = 0x13579bdf;
        MusashiResetGraphPrefixStop nested_stop = {0xa5a5a5a5u, 0x5a5a5a5au};
        fixture->recurse_done = 1;
        fixture->recurse_return = musashi_boot_execute_input_bios_callback(
            fixture->memory, fixture->device, &fixture->guard->frame,
            VERIFIER, 0, &nested_output, &nested_stop);
        fixture->recurse_output = nested_output;
        fixture->recurse_stop = nested_stop;
    }
    *value = address == I_STAT ? fixture->stat : fixture->mask;
    return 1;
}

static int input_write32(void *userdata, uint32_t address, uint32_t value) {
    InputFixture *fixture = userdata;
    (void)value;
    if (!fixture || (address != I_STAT && address != I_MASK)) return 0;
    fixture->write_count++;
    return 1;
}

static void fixture_init(InputFixture *fixture, MusashiBootMemory *memory,
                         FrameGuard *guard, MusashiInputDevice *device) {
    memset(fixture, 0, sizeof(*fixture));
    memset(device, 0, sizeof(*device));
    memset(guard->before, 0x3c, sizeof(guard->before));
    memset(guard->after, 0xc3, sizeof(guard->after));
    musashi_boot_bios_callback_frame_init(&guard->frame);
    memset(guard->frame.scratch, 0xa5, sizeof(guard->frame.scratch));
    fixture->memory = memory;
    fixture->guard = guard;
    fixture->device = device;
    device->userdata = fixture;
    device->read32 = input_read32;
    device->write32 = input_write32;
}

static void fresh_memory(MusashiBootMemory *memory) {
    memset(memory->bytes, 0xa5, sizeof(memory->bytes));
}

static void assert_guard(const FrameGuard *guard) {
    uint8_t before[16];
    uint8_t after[16];
    memset(before, 0x3c, sizeof(before));
    memset(after, 0xc3, sizeof(after));
    assert(memcmp(guard->before, before, sizeof(before)) == 0);
    assert(memcmp(guard->after, after, sizeof(after)) == 0);
}

static void assert_memory_same_except(const MusashiBootMemory *before,
                                      const MusashiBootMemory *after,
                                      const uint32_t *allowed,
                                      size_t allowed_count) {
    size_t offset;
    size_t index;
    for (offset = 0; offset < MUSASHI_RAM_SIZE; ++offset) {
        int exception = 0;
        for (index = 0; index < allowed_count; ++index) {
            uint32_t address = allowed[index] - 0x80000000u;
            if (offset >= address && offset < address + 4u) {
                exception = 1;
                break;
            }
        }
        if (!exception) assert(before->bytes[offset] == after->bytes[offset]);
    }
}

static int run_callback(InputFixture *fixture, uint32_t target,
                        int32_t *result, MusashiResetGraphPrefixStop *stop) {
    return musashi_boot_execute_input_bios_callback(
        fixture->memory, fixture->device, &fixture->guard->frame,
        target, 0, result, stop);
}

static void setup_verifier(MusashiBootMemory *memory) {
    write_word(memory, INPUT_STATE, I_STAT);
    write_word(memory, OPTIONAL_VERIFIER, 0);
}

static void test_verifier_live_interrupt_state(void) {
    static const uint32_t states[][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    unsigned index;
    MusashiBootMemory memory;
    MusashiBootMemory before;
    FrameGuard guard;
    MusashiInputDevice device;
    InputFixture fixture;

    fresh_memory(&memory);
    fixture_init(&fixture, &memory, &guard, &device);
    setup_verifier(&memory);
    for (index = 0; index < 4; ++index) {
        int32_t result = 0x2468ace0;
        MusashiResetGraphPrefixStop stop = {0, 0};
        before = memory;
        fixture.mask = states[index][0];
        fixture.stat = states[index][1];
        guard.frame.busy = 0;
        guard.frame.faulted = 0;
        assert(run_callback(&fixture, VERIFIER, &result, &stop));
        assert(result == (states[index][0] && states[index][1] ? 1 : 0));
        assert(guard.frame.busy == 0 && guard.frame.faulted == 0);
        assert_memory_same_except(&before, &memory, NULL, 0);
        assert_guard(&guard);
    }
}

static void test_verifier_refusal_and_reentrant_busy_guard(void) {
    MusashiBootMemory memory;
    MusashiBootMemory before;
    FrameGuard guard;
    MusashiInputDevice device;
    InputFixture fixture;
    int32_t result;
    MusashiResetGraphPrefixStop stop;

    fresh_memory(&memory);
    fixture_init(&fixture, &memory, &guard, &device);
    setup_verifier(&memory);
    fixture.mask = fixture.stat = 1;
    fixture.refuse_reads = 1;
    result = 0x2468ace0;
    stop.call_address = stop.target_address = 0xaaaaaaaa;
    before = memory;
    assert(!run_callback(&fixture, VERIFIER, &result, &stop));
    assert(result == 0x2468ace0);
    assert(guard.frame.busy == 0 && guard.frame.faulted == 1);
    assert_memory_same_except(&before, &memory, NULL, 0);
    assert_guard(&guard);

    fixture_init(&fixture, &memory, &guard, &device);
    setup_verifier(&memory);
    fixture.mask = fixture.stat = 1;
    fixture.recurse = 1;
    result = 0x2468ace0;
    stop.call_address = stop.target_address = 0xbbbbbbbb;
    assert(run_callback(&fixture, VERIFIER, &result, &stop));
    assert(result == 1);
    assert(fixture.recurse_return == 0);
    assert(fixture.recurse_output == 0x13579bdf);
    assert(fixture.recurse_stop.call_address == 0xa5a5a5a5u &&
           fixture.recurse_stop.target_address == 0x5a5a5a5a);
    assert(guard.frame.busy == 0 && guard.frame.faulted == 0);
    assert_guard(&guard);
}

static void test_optional_verifier_callback_refuses_after_delay_slot(void) {
    MusashiBootMemory memory;
    MusashiBootMemory before;
    uint8_t scratch_before[sizeof(((MusashiBiosCallbackFrame *)0)->scratch)];
    FrameGuard guard;
    MusashiInputDevice device;
    InputFixture fixture;
    int32_t result = 0x2468ace0;
    MusashiResetGraphPrefixStop stop = {0, 0};

    fresh_memory(&memory);
    fixture_init(&fixture, &memory, &guard, &device);
    setup_verifier(&memory);
    write_word(&memory, OPTIONAL_VERIFIER, VERIFIER_CALLBACK);
    fixture.mask = fixture.stat = 1;
    memcpy(scratch_before, guard.frame.scratch, sizeof(scratch_before));
    before = memory;
    assert(!run_callback(&fixture, VERIFIER, &result, &stop));
    assert(result == 0x2468ace0);
    assert(guard.frame.busy == 0 && guard.frame.faulted == 1);
    assert(stop.call_address == VERIFIER_CALL &&
           stop.target_address == VERIFIER_CALLBACK);
    assert(memcmp(guard.frame.scratch, scratch_before, sizeof(scratch_before)) != 0);
    assert_memory_same_except(&before, &memory, NULL, 0);
    assert_guard(&guard);
}

static void setup_handler(MusashiBootMemory *memory, uint32_t enabled,
                          uint32_t active, uint32_t counter) {
    write_word(memory, HANDLER_ENABLED, enabled);
    write_word(memory, HANDLER_ACTIVE, active);
    write_word(memory, HANDLER_COUNTER, counter);
    write_word(memory, HANDLER_RESULT, 0);
    write_word(memory, HANDLER_RECORD_BASE, 0x80078a48u);
    write_word(memory, HANDLER_RECORD_INDEX, 9);
    write_word(memory, HANDLER_RECORD_OFFSET, 8);
}

static void test_handler_disabled_returns_guest_zero(void) {
    MusashiBootMemory memory;
    MusashiBootMemory before;
    FrameGuard guard;
    MusashiInputDevice device;
    InputFixture fixture;
    int32_t result = 0x2468ace0;
    MusashiResetGraphPrefixStop stop = {0, 0};
    static const uint32_t allowed[] = {HANDLER_RESULT};

    fresh_memory(&memory);
    fixture_init(&fixture, &memory, &guard, &device);
    setup_handler(&memory, 0, 0, 1);
    before = memory;
    assert(run_callback(&fixture, HANDLER, &result, &stop));
    assert(result == 0);
    assert(read_word(&memory, HANDLER_RESULT) == 1);
    assert(fixture.write_count == 0);
    assert(guard.frame.busy == 0 && guard.frame.faulted == 0);
    assert_memory_same_except(&before, &memory, allowed, 1);
    assert_guard(&guard);
}

static void test_handler_reaches_unbound_child_with_prior_effects(void) {
    MusashiBootMemory memory;
    MusashiBootMemory before;
    FrameGuard guard;
    MusashiInputDevice device;
    InputFixture fixture;
    int32_t result = 0x2468ace0;
    MusashiResetGraphPrefixStop stop = {0, 0};
    static const uint32_t allowed[] = {
        HANDLER_RESULT, HANDLER_RECORD_INDEX, HANDLER_RECORD_OFFSET
    };

    fresh_memory(&memory);
    fixture_init(&fixture, &memory, &guard, &device);
    setup_handler(&memory, 1, 0, 1);
    before = memory;
    assert(!run_callback(&fixture, HANDLER, &result, &stop));
    assert(result == 0x2468ace0);
    assert(guard.frame.busy == 0 && guard.frame.faulted == 1);
    assert(stop.call_address == HANDLER_CALL && stop.target_address == HANDLER_CHILD);
    assert(read_word(&memory, HANDLER_RESULT) == 1);
    assert(read_word(&memory, HANDLER_RECORD_INDEX) == 0);
    assert(read_word(&memory, HANDLER_RECORD_OFFSET) == 0);
    assert(fixture.write_count == 0);
    assert_memory_same_except(&before, &memory, allowed, 3);
    assert_guard(&guard);

    before = memory;
    result = 0x2468ace0;
    stop.call_address = stop.target_address = 0xcccccccc;
    assert(!run_callback(&fixture, HANDLER, &result, &stop));
    assert(result == 0x2468ace0);
    assert(memcmp(&memory, &before, sizeof(memory)) == 0);
    assert(stop.call_address == 0xcccccccc && stop.target_address == 0xcccccccc);
}

static void test_handler_counter_caps(void) {
    MusashiBootMemory memory;
    MusashiBootMemory before;
    FrameGuard guard;
    MusashiInputDevice device;
    InputFixture fixture;
    int32_t result = 0x2468ace0;
    MusashiResetGraphPrefixStop stop = {0, 0};
    static const uint32_t allowed[] = {
        HANDLER_RESULT, INPUT_COUNTER_A, INPUT_COUNTER_B
    };

    fresh_memory(&memory);
    fixture_init(&fixture, &memory, &guard, &device);
    setup_handler(&memory, 0, 1, 0);
    write_word(&memory, INPUT_COUNTER_A, 149);
    write_word(&memory, INPUT_COUNTER_B, 149);
    before = memory;
    assert(run_callback(&fixture, HANDLER, &result, &stop));
    assert(result == 0);
    assert(read_word(&memory, INPUT_COUNTER_A) == 150);
    assert(read_word(&memory, INPUT_COUNTER_B) == 150);
    assert(fixture.write_count == 0);
    assert_memory_same_except(&before, &memory, allowed, 3);
    setup_handler(&memory, 0, 1, 0);
    write_word(&memory, INPUT_COUNTER_A, 150);
    write_word(&memory, INPUT_COUNTER_B, 150);
    before = memory;
    result = 0x2468ace0;
    assert(run_callback(&fixture, HANDLER, &result, &stop));
    assert(result == 0);
    assert(read_word(&memory, INPUT_COUNTER_A) == 150);
    assert(read_word(&memory, INPUT_COUNTER_B) == 150);
    assert(fixture.write_count == 0);
    assert_memory_same_except(&before, &memory, allowed, 3);
}

static void test_target_and_argument_refusals(void) {
    MusashiBootMemory memory;
    MusashiBootMemory before;
    FrameGuard guard;
    MusashiInputDevice device;
    InputFixture fixture;
    const uint32_t targets[] = {
        0, VERIFIER - 1, VERIFIER + 1, HANDLER - 1, HANDLER + 1,
        0x8005d8a0u, 0xffffffffu
    };
    unsigned index;

    fresh_memory(&memory);
    fixture_init(&fixture, &memory, &guard, &device);
    setup_verifier(&memory);
    fixture.mask = fixture.stat = 1;
    for (index = 0; index < sizeof(targets) / sizeof(targets[0]); ++index) {
        int32_t result = 0x2468ace0;
        MusashiResetGraphPrefixStop stop = {0xaaaaaaaa, 0xbbbbbbbb};
        before = memory;
        musashi_boot_bios_callback_frame_init(&guard.frame);
        assert(!run_callback(&fixture, targets[index], &result, &stop));
        assert(result == 0x2468ace0);
        assert(guard.frame.busy == 0 && guard.frame.faulted == 0);
        assert(memcmp(&memory, &before, sizeof(memory)) == 0);
        assert_guard(&guard);
    }
    {
        int32_t result = 0x2468ace0;
        MusashiResetGraphPrefixStop stop = {0xaaaaaaaa, 0xbbbbbbbb};
        assert(!musashi_boot_execute_input_bios_callback(
            NULL, &device, &guard.frame, VERIFIER, 0, &result, &stop));
        assert(!musashi_boot_execute_input_bios_callback(
            &memory, NULL, &guard.frame, VERIFIER, 0, &result, &stop));
        assert(!musashi_boot_execute_input_bios_callback(
            &memory, &device, NULL, VERIFIER, 0, &result, &stop));
        assert(!musashi_boot_execute_input_bios_callback(
            &memory, &device, &guard.frame, VERIFIER, 0, NULL, &stop));
        assert(!musashi_boot_execute_input_bios_callback(
            &memory, &device, &guard.frame, VERIFIER, 0, &result, NULL));
        assert(result == 0x2468ace0);
    }
}

typedef struct ClockFixture {
    uint64_t cycles;
    unsigned calls;
    unsigned refuse_at;
} ClockFixture;

static int clock_advance(void *userdata, uint32_t cost) {
    ClockFixture *clock = userdata;
    ++clock->calls;
    if (clock->refuse_at && clock->calls == clock->refuse_at) return 0;
    assert(cost == 2 || cost == 3);
    clock->cycles += cost;
    return 1;
}

static void test_shared_source_clock_and_refusal(void) {
    MusashiBootMemory memory, before;
    FrameGuard guard;
    MusashiInputDevice device;
    InputFixture fixture;
    ClockFixture time = {0};
    MusashiExecutionClock clock = {&time, clock_advance};
    MusashiResetGraphPrefixStop stop = {0};
    int32_t result = -99;
    uint8_t scratch[sizeof(guard.frame.scratch)];
    fresh_memory(&memory);
    fixture_init(&fixture, &memory, &guard, &device);
    setup_verifier(&memory);
    device.clock = &clock;
    /* Mask-zero source path: 13 instructions, four data accesses, including
     * source branch/return delay slots and dedicated BIOS scratch accesses. */
    assert(run_callback(&fixture, VERIFIER, &result, &stop));
    assert(result == 0 && time.cycles == 30 && time.calls == 13);
    fixture.mask = fixture.stat = 1;
    /* Mask/stat-one, absent optional callback: 24 instructions + six data
     * accesses. The same clock survives this independent BIOS CPU frame. */
    assert(run_callback(&fixture, VERIFIER, &result, &stop));
    assert(result == 1 && time.cycles == 84 && time.calls == 37);
    fixture_init(&fixture, &memory, &guard, &device);
    device.clock = &clock;
    time.calls = 0; time.cycles = 0; time.refuse_at = 4;
    before = memory;
    memcpy(scratch, guard.frame.scratch, sizeof(scratch));
    result = -99;
    /* Refuse the first SW before it touches the BIOS stack. */
    assert(!run_callback(&fixture, VERIFIER, &result, &stop));
    assert(time.cycles == 7 && time.calls == 4 && result == -99);
    assert(stop.call_address == 0x8005d6d8u);
    assert(guard.frame.faulted && !guard.frame.busy);
    assert(memcmp(scratch, guard.frame.scratch, sizeof(scratch)) == 0);
    assert(memcmp(&before, &memory, sizeof(memory)) == 0);
    assert_guard(&guard);
    assert(!run_callback(&fixture, VERIFIER, &result, &stop));
    assert(time.calls == 4);
}

static void test_serial_children_and_timer_boundary(void) {
    int available;
    for (available = 0; available <= 1; ++available) {
        MusashiBootMemory memory, before;
        FrameGuard guard;
        MusashiInputDevice device;
        InputFixture fixture;
        MusashiResetGraphPrefixStop stop = {0};
        int32_t result = -99;
        const uint32_t effects[] = {HANDLER_RESULT, HANDLER_RECORD_INDEX,
            HANDLER_RECORD_OFFSET, 0x800c5320u, 0x80078f24u, 0x800729b4u};
        fresh_memory(&memory);
        fixture_init(&fixture, &memory, &guard, &device);
        setup_handler(&memory, 1, 0, 1);
        write_word(&memory, 0x800729c0u, 0x1f801040u);
        write_word(&memory, 0x800729b4u, 0);
        write_word(&memory, 0x8007297cu, 0x8005f830u);
        fixture.timer_available = available;
        device.read16 = serial_read16;
        device.write16 = serial_write16;
        before = memory;
        assert(!run_callback(&fixture, HANDLER, &result, &stop));
        assert(result == -99 && guard.frame.faulted && !guard.frame.busy);
        assert(fixture.timer_reads == 1 && fixture.serial_writes == (available ? 5u : 4u));
        if (available) {
            assert(stop.call_address == 0x8005db04u && stop.target_address == 0x8005f830u);
            assert(read_word(&memory, 0x800c5320u) == 0x91);
            assert(read_word(&memory, 0x80078f24u) == 0x4567);
            assert(read_word(&memory, 0x800729b4u) == UINT32_MAX); /* JALR delay slot. */
        } else {
            assert(stop.call_address == 0x8005fbb0u && stop.target_address == 0x8005fbb0u);
            assert(read_word(&memory, 0x800c5320u) == 0xa5a5a5a5u);
            assert(read_word(&memory, 0x80078f24u) == 0xa5a5a5a5u);
        }
        /* Both nested return addresses live only in dedicated BIOS storage. */
        assert(guard.frame.scratch[0x85d0u - MUSASHI_BIOS_CALLBACK_STACK_BASE] == 0xff &&
               guard.frame.scratch[0x85d3u - MUSASHI_BIOS_CALLBACK_STACK_BASE] == 0xff);
        assert(guard.frame.scratch[0x85b8u - MUSASHI_BIOS_CALLBACK_STACK_BASE] == 0x04 &&
               guard.frame.scratch[0x85b9u - MUSASHI_BIOS_CALLBACK_STACK_BASE] == 0xd8);
        assert_memory_same_except(&before, &memory, effects, sizeof(effects)/sizeof(effects[0]));
        assert_guard(&guard);
    }
}

int main(void) {
    test_serial_children_and_timer_boundary();
    test_shared_source_clock_and_refusal();
    test_verifier_live_interrupt_state();
    test_verifier_refusal_and_reentrant_busy_guard();
    test_optional_verifier_callback_refuses_after_delay_slot();
    test_handler_disabled_returns_guest_zero();
    test_handler_reaches_unbound_child_with_prior_effects();
    test_handler_counter_caps();
    test_target_and_argument_refusals();
    return 0;
}
