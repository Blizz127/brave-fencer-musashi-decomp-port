#include "../pc_port/mips_formatter.c"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    SIO_DATA = 0x1f801040u,
    SIO_STATUS = 0x1f801044u,
    TIMER2_COUNT = 0x1f801120u,
    HANDLER_ENABLED = 0x80072994u,
    HANDLER_ACTIVE = 0x800729acu,
    HANDLER_COUNTER = 0x800729b0u,
    HANDLER_RESULT = 0x800729c4u,
    RECORD_BASE = 0x80072990u,
    RECORD_INDEX = 0x8007299cu,
    RECORD_OFFSET = 0x800729a0u,
    SLOT_F830 = 0x8007297cu,
    SLOT_F384 = 0x80072980u,
    DISPATCH_TABLE = 0x800729e0u,
    SIO_POINTER = 0x800729c0u,
    SERIAL_COUNTER = 0x80078998u,
    SERIAL_COUNTER_B = 0x8007899cu,
    RECORD = 0x80078a48u,
    INPUT_BUFFER = 0x80079000u,
    OUTPUT_BUFFER = 0x80079100u,
};

typedef struct FrameGuard {
    uint8_t before[16];
    MusashiBiosCallbackFrame frame;
    uint8_t after[16];
} FrameGuard;

typedef struct InputFixture {
    MusashiBootMemory *memory;
    unsigned read8_count;
    unsigned write8_count;
    unsigned read16_count;
    unsigned write16_count;
    int refuse_read8;
    int refuse_write8;
    int refuse_read16;
    int refuse_write16;
    uint8_t data_value;
    uint16_t timer_value;
} InputFixture;

static uint8_t *ram_byte(MusashiBootMemory *memory, uint32_t address) {
    uint8_t *span = musashi_boot_ram_span(memory, address, 1);
    assert(span != NULL);
    return span;
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

static int fixture_read8(void *userdata, uint32_t address, uint8_t *value) {
    InputFixture *fixture = userdata;
    if (!fixture || !value || fixture->refuse_read8 || address != SIO_DATA)
        return 0;
    fixture->read8_count++;
    *value = fixture->data_value;
    return 1;
}

static int fixture_write8(void *userdata, uint32_t address, uint8_t value) {
    InputFixture *fixture = userdata;
    if (!fixture || fixture->refuse_write8 || address != SIO_DATA)
        return 0;
    fixture->write8_count++;
    fixture->data_value = value;
    return 1;
}

static int fixture_read16(void *userdata, uint32_t address, uint16_t *value) {
    InputFixture *fixture = userdata;
    if (!fixture || !value || fixture->refuse_read16 ||
        (address != TIMER2_COUNT && address != SIO_STATUS))
        return 0;
    fixture->read16_count++;
    *value = address == TIMER2_COUNT ? fixture->timer_value : 0;
    return 1;
}

static int fixture_write16(void *userdata, uint32_t address, uint16_t value) {
    InputFixture *fixture = userdata;
    static const uint32_t expected_address[] = {
        0x1f80104au, 0x1f80104au, 0x1f801048u,
        0x1f80104eu, 0x1f80104au,
    };
    static const uint16_t expected_value[] = {0x40u, 0, 0x0du, 0x88u, 0x1003u};
    unsigned index;
    if (!fixture || fixture->refuse_write16 || address < 0x1f801040u ||
        address > 0x1f80104eu)
        return 0;
    index = fixture->write16_count++;
    if (index >= sizeof(expected_address) / sizeof(expected_address[0]) ||
        address != expected_address[index] || value != expected_value[index])
        return 0;
    return 1;
}

static void init_fixture(MusashiBootMemory *memory, FrameGuard *guard,
                         InputFixture *fixture, MusashiInputDevice *device,
                         int byte_callbacks) {
    memset(memory, 0xa5, sizeof(*memory));
    memset(guard->before, 0x3c, sizeof(guard->before));
    memset(guard->after, 0xc3, sizeof(guard->after));
    musashi_boot_bios_callback_frame_init(&guard->frame);
    memset(guard->frame.scratch, 0xa5, sizeof(guard->frame.scratch));
    memset(fixture, 0, sizeof(*fixture));
    fixture->memory = memory;
    fixture->data_value = 0x41;
    fixture->timer_value = 0x2345;
    memset(device, 0, sizeof(*device));
    device->userdata = fixture;
    device->read16 = fixture_read16;
    device->write16 = fixture_write16;
    if (byte_callbacks) {
        device->read8 = fixture_read8;
        device->write8 = fixture_write8;
    }
}

static void setup_metadata_record(MusashiBootMemory *memory) {
    write_word(memory, HANDLER_ENABLED, 1);
    write_word(memory, HANDLER_ACTIVE, 0);
    write_word(memory, HANDLER_COUNTER, 0);
    write_word(memory, HANDLER_RESULT, 0);
    write_word(memory, RECORD_BASE, RECORD);
    write_word(memory, RECORD_INDEX, 0);
    write_word(memory, RECORD_OFFSET, 0);
    write_word(memory, SLOT_F830, 0x8005f830u);
    write_word(memory, 0x80072960u, 0x8005f228u);
    write_word(memory, SLOT_F384, 0x8005f384u);
    write_word(memory, DISPATCH_TABLE, 0x8005eae8u);
    write_word(memory, SIO_POINTER, SIO_DATA);
    write_word(memory, SERIAL_COUNTER, 0);
    write_word(memory, SERIAL_COUNTER_B, 0);
    write_word(memory, 0x800729b4u, 0);
    write_word(memory, RECORD + 0x30u, OUTPUT_BUFFER);
    write_word(memory, RECORD + 0x3cu, INPUT_BUFFER);
    write_word(memory, RECORD + 0x0cu, RECORD);
    write_word(memory, RECORD + 0x14u, 0);
    write_word(memory, RECORD + 0x18u, 0);
    *ram_byte(memory, RECORD + 0x44u) = 0;
    *ram_byte(memory, RECORD + 0x36u) = 0;
    *ram_byte(memory, RECORD + 0x37u) = 0;
    *ram_byte(memory, RECORD + 0x46u) = 0;
    *ram_byte(memory, RECORD + 0x49u) = 0;
    *ram_byte(memory, RECORD + 0x4au) = 0;
    *ram_byte(memory, RECORD + 0x50u) = 1;
    *ram_byte(memory, RECORD + 0xe8u) = 0;
    *ram_byte(memory, INPUT_BUFFER + 0u) = 0;
    *ram_byte(memory, INPUT_BUFFER + 1u) = 0;
    *ram_byte(memory, OUTPUT_BUFFER + 0u) = 0xa1;
    *ram_byte(memory, OUTPUT_BUFFER + 1u) = 0xa2;
}

static int run_handler(MusashiBootMemory *memory,
                       const MusashiInputDevice *device, FrameGuard *guard,
                       int32_t *result, MusashiResetGraphPrefixStop *stop) {
    return musashi_boot_execute_input_bios_callback(
        memory, device, &guard->frame, 0x8005d734u, 1, result, stop);
}

static void assert_guard(const FrameGuard *guard) {
    static const uint8_t before[16] = {
        0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c,
        0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c,
    };
    static const uint8_t after[16] = {
        0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
        0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    };
    assert(memcmp(guard->before, before, sizeof(before)) == 0);
    assert(memcmp(guard->after, after, sizeof(after)) == 0);
}

static void test_normal_metadata_stage_and_next_refusal(void) {
    MusashiBootMemory memory;
    FrameGuard guard;
    InputFixture fixture;
    MusashiInputDevice device;
    MusashiResetGraphPrefixStop stop = {0, 0};
    int32_t result = 0x2468ace0;

    init_fixture(&memory, &guard, &fixture, &device, 1);
    setup_metadata_record(&memory);
    assert(!run_handler(&memory, &device, &guard, &result, &stop));
    assert(result == 0x2468ace0);
    assert(guard.frame.faulted && !guard.frame.busy);
    assert(stop.call_address == 0x8005eafcu);
    assert(stop.target_address == 0xa5a5a5a5u);
    assert(fixture.write16_count == 5);
    assert(fixture.read8_count == 0 && fixture.write8_count == 0);
    assert(read_word(&memory, HANDLER_RESULT) == 1);
    assert(*ram_byte(&memory, OUTPUT_BUFFER) == 0);
    assert(*ram_byte(&memory, OUTPUT_BUFFER + 1u) == 0);
    assert(*ram_byte(&memory, RECORD + 0x36u) == 0);
    assert(*ram_byte(&memory, RECORD + 0x37u) == 0);
    assert_guard(&guard);
}

static void test_read16_only_keeps_db04_refusal(void) {
    MusashiBootMemory memory;
    FrameGuard guard;
    InputFixture fixture;
    MusashiInputDevice device;
    MusashiResetGraphPrefixStop stop = {0, 0};
    int32_t result = 0x13579bdf;

    init_fixture(&memory, &guard, &fixture, &device, 0);
    setup_metadata_record(&memory);
    assert(!run_handler(&memory, &device, &guard, &result, &stop));
    assert(result == 0x13579bdf);
    assert(guard.frame.faulted && !guard.frame.busy);
    assert(stop.call_address == 0x8005db04u);
    assert(stop.target_address == 0x8005f830u);
    assert(fixture.write16_count == 5);
    assert(*ram_byte(&memory, INPUT_BUFFER) == 0);
    assert(*ram_byte(&memory, OUTPUT_BUFFER) == 0xa1);
    assert(*ram_byte(&memory, OUTPUT_BUFFER + 1u) == 0xa2);
    assert_guard(&guard);
}

static void test_f228_stateful_path_preserves_source_effects(void) {
    MusashiBootMemory memory;
    FrameGuard guard;
    InputFixture fixture;
    MusashiInputDevice device;
    MusashiResetGraphPrefixStop stop = {0, 0};
    int32_t result = 0x11223344;
    unsigned offset;

    init_fixture(&memory, &guard, &fixture, &device, 1);
    setup_metadata_record(&memory);
    *ram_byte(&memory, INPUT_BUFFER + 1u) = 1;
    *ram_byte(&memory, RECORD + 0x50u) = 0;
    *ram_byte(&memory, RECORD + 0x46u) = 0xff;
    *ram_byte(&memory, RECORD + 0x49u) = 1;
    *ram_byte(&memory, RECORD + 0xe8u) = 1;
    assert(musashi_boot_write16(&memory, RECORD + 0xe6u, 1));
    assert(!run_handler(&memory, &device, &guard, &result, &stop));
    assert(result == 0x11223344);
    assert(stop.call_address == 0x8005eafcu);
    assert(stop.target_address == 0xa5a5a5a5u);
    assert(*ram_byte(&memory, RECORD + 0x49u) == 0);
    assert(*ram_byte(&memory, RECORD + 0x46u) == 0);
    assert(*ram_byte(&memory, RECORD + 0xe8u) == 0);
    assert(read_word(&memory, RECORD + 0x14u) == 0);
    assert(read_word(&memory, RECORD + 0x18u) == 0);
    assert(read_word(&memory, RECORD) == 0);
    assert(read_word(&memory, RECORD + 4u) == 0);
    assert(read_word(&memory, RECORD + 8u) == 0);
    for (offset = 0; offset < 6; ++offset)
        assert(*ram_byte(&memory, RECORD + 0x5du + offset) == 0xff);
    assert_guard(&guard);
}

static void test_e228_maximum_first_stage_stack_path(void) {
    MusashiBootMemory memory;
    FrameGuard guard;
    InputFixture fixture;
    MusashiInputDevice device;
    MusashiResetGraphPrefixStop stop = {0, 0};
    int32_t result = 0x55667788;

    init_fixture(&memory, &guard, &fixture, &device, 1);
    setup_metadata_record(&memory);
    *ram_byte(&memory, INPUT_BUFFER + 1u) = 1;
    *ram_byte(&memory, INPUT_BUFFER + 3u) = 0x12;
    *ram_byte(&memory, INPUT_BUFFER + 4u) = 0x34;
    *ram_byte(&memory, INPUT_BUFFER + 5u) = 0x56;
    *ram_byte(&memory, INPUT_BUFFER + 6u) = 0x78;
    *ram_byte(&memory, RECORD + 0x36u) = 1;
    *ram_byte(&memory, RECORD + 0x46u) = 2;
    assert(!run_handler(&memory, &device, &guard, &result, &stop));
    assert(result == 0x55667788);
    assert(stop.call_address == 0x8005eafcu);
    assert(stop.target_address == 0xa5a5a5a5u);
    assert(*ram_byte(&memory, RECORD + 0x36u) == 0);
    assert(*ram_byte(&memory, RECORD + 0x37u) == 1);
    assert(*ram_byte(&memory, RECORD + 0xe3u) == 0x12);
    assert(*ram_byte(&memory, RECORD + 0xe4u) == 0x34);
    assert(*ram_byte(&memory, RECORD + 0xe9u) == 0x56);
    assert(*ram_byte(&memory, RECORD + 0xeau) == 0x78);
    assert_guard(&guard);
}

static void test_wrong_slots_refuse_after_delay_and_preserve_prior_writes(void) {
    static const uint32_t bad_slots[] = {0, 0x8005fb70u, 0xa005f830u};
    unsigned index;
    for (index = 0; index < sizeof(bad_slots) / sizeof(bad_slots[0]); ++index) {
        MusashiBootMemory memory;
        FrameGuard guard;
        InputFixture fixture;
        MusashiInputDevice device;
        MusashiResetGraphPrefixStop stop = {0, 0};
        int32_t result = 0x66778899;

        init_fixture(&memory, &guard, &fixture, &device, 1);
        setup_metadata_record(&memory);
        write_word(&memory, SLOT_F830, bad_slots[index]);
        assert(!run_handler(&memory, &device, &guard, &result, &stop));
        assert(result == 0x66778899);
        assert(guard.frame.faulted && !guard.frame.busy);
        assert(stop.call_address == 0x8005db04u);
        assert(stop.target_address == bad_slots[index]);
        assert(fixture.write16_count == 5);
        assert(*ram_byte(&memory, INPUT_BUFFER) == 0);
        assert(*ram_byte(&memory, OUTPUT_BUFFER) == 0xa1);
        assert(*ram_byte(&memory, OUTPUT_BUFFER + 1u) == 0xa2);
        assert_guard(&guard);
    }
}

static void test_scratch_widths_aliases_edges_and_mmio_outputs(void) {
    MusashiBootMemory memory;
    FrameGuard guard;
    InputFixture fixture;
    MusashiInputDevice device;
    FormatterCpu cpu;
    uint8_t byte;
    uint16_t half;
    uint32_t word;
    uint8_t memory_before[0x10000];

    init_fixture(&memory, &guard, &fixture, &device, 1);
    memset(&cpu, 0, sizeof(cpu));
    cpu.input_device = &device;
    cpu.bios_frame = &guard.frame;
    memcpy(memory_before, memory.bytes, sizeof(memory_before));

    byte = 0;
    assert(cpu_read8(&memory, &cpu, 0x000084d8u, &byte) && byte == 0xa5);
    assert(cpu_write8(&memory, &cpu, 0x800085d7u, 0x19));
    assert(cpu_read8(&memory, &cpu, 0xa00085d7u, &byte) && byte == 0x19);
    assert(!cpu_read8(&memory, &cpu, 0x000085d8u, &byte));
    assert(!cpu_read8(&memory, &cpu, 0x000084d7u, &byte));
    assert(!cpu_read8(&memory, &cpu, 0x400084d8u, &byte));

    half = 0;
    assert(cpu_write16(&memory, &cpu, 0x800084d8u, 0x80f1u));
    assert(cpu_read16(&memory, &cpu, 0xa00085d6u, &half) && half == 0x19a5u);
    assert(!cpu_read16(&memory, &cpu, 0x800084d9u, &half));
    assert(!cpu_write16(&memory, &cpu, 0x800085d7u, 0x1234u));

    word = 0;
    assert(cpu_write32(&memory, &cpu, 0x000085d4u, 0xdeadbeefu));
    assert(cpu_read32(&memory, &cpu, 0x800085d4u, &word) && word == 0xdeadbeefu);
    assert(!cpu_read32(&memory, &cpu, 0xa00085d5u, &word));
    assert(!cpu_write32(&memory, &cpu, 0x800085d5u, 0x12345678u));

    byte = 0x7b;
    fixture.refuse_read8 = 1;
    assert(!cpu_read8(&memory, &cpu, SIO_DATA, &byte) && byte == 0x7b);
    fixture.refuse_read8 = 0;
    assert(cpu_read8(&memory, &cpu, SIO_DATA, &byte) && byte == 0x41);
    assert(fixture.read8_count == 1);
    assert(!cpu_read8(&memory, &cpu, SIO_DATA + 1u, &byte));
    fixture.refuse_write8 = 1;
    assert(!cpu_write8(&memory, &cpu, SIO_DATA, 0x55));
    fixture.refuse_write8 = 0;
    assert(cpu_write8(&memory, &cpu, SIO_DATA, 0x55));
    assert(fixture.write8_count == 1 && fixture.data_value == 0x55);

    half = 0x7c7c;
    fixture.refuse_read16 = 1;
    assert(!cpu_read16(&memory, &cpu, TIMER2_COUNT, &half) && half == 0x7c7c);
    fixture.refuse_read16 = 0;
    assert(cpu_read16(&memory, &cpu, TIMER2_COUNT, &half) && half == 0x2345);
    fixture.refuse_write16 = 1;
    assert(!cpu_write16(&memory, &cpu, 0x1f801048u, 0x1234u));
    assert(memcmp(memory.bytes, memory_before, sizeof(memory_before)) == 0);
    assert_guard(&guard);
    assert(guard.frame.scratch[0] == 0xf1);
    assert(guard.frame.scratch[0x85d4u - MUSASHI_BIOS_CALLBACK_STACK_BASE] == 0xef);
}

typedef struct CorruptReturnFixture {
    FrameGuard *guard;
    uint32_t address, expected, replacement;
    int injected;
} CorruptReturnFixture;

/* Deliberate test-owned stack corruption after the real source save. No
 * production entry point accepts an injected CPU or captured register frame. */
static int corrupt_saved_return(void *userdata, uint32_t cycles) {
    CorruptReturnFixture *fault = userdata;
    uint8_t *p = fault->guard->frame.scratch + fault->address - MUSASHI_BIOS_CALLBACK_STACK_BASE;
    uint32_t saved = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    assert(cycles == 2 || cycles == 3);
    if (!fault->injected && saved == fault->expected) {
        p[0] = (uint8_t)fault->replacement;
        p[1] = (uint8_t)(fault->replacement >> 8);
        p[2] = (uint8_t)(fault->replacement >> 16);
        p[3] = (uint8_t)(fault->replacement >> 24);
        fault->injected = 1;
    }
    return 1;
}

static void test_corrupted_returns_cannot_enter_other_mapped_bodies(void) {
    unsigned nested;
    for (nested = 0; nested < 2; ++nested) {
        MusashiBootMemory memory;
        FrameGuard guard;
        InputFixture fixture;
        MusashiInputDevice device;
        MusashiResetGraphPrefixStop stop = {0};
        int32_t result = 0x12345678;
        CorruptReturnFixture fault = {&guard, nested ? 0x8598u : 0x85d0u,
            nested ? 0x8005db0cu : UINT32_MAX,
            nested ? 0x8005f384u : 0x8005d6ccu, 0};
        MusashiExecutionClock clock = {&fault, corrupt_saved_return};
        init_fixture(&memory, &guard, &fixture, &device, 1);
        setup_metadata_record(&memory);
        if (!nested) write_word(&memory, HANDLER_ENABLED, 0);
        device.clock = &clock;
        assert(!run_handler(&memory, &device, &guard, &result, &stop));
        assert(fault.injected && result == 0x12345678 && guard.frame.faulted);
        assert(stop.call_address == (nested ? 0x8005fa8cu : 0x8005d898u));
        assert(stop.target_address == fault.replacement);
        assert(!guard.frame.busy);
        assert_guard(&guard);
    }
}

int main(void) {
    test_corrupted_returns_cannot_enter_other_mapped_bodies();
    test_normal_metadata_stage_and_next_refusal();
    test_read16_only_keeps_db04_refusal();
    test_f228_stateful_path_preserves_source_effects();
    test_e228_maximum_first_stage_stack_path();
    test_wrong_slots_refuse_after_delay_and_preserve_prior_writes();
    test_scratch_widths_aliases_edges_and_mmio_outputs();
    puts("input controller word probe: PASS");
    return 0;
}
