#include "../pc_port/mips_formatter.c"
#include "../pc_port/include/musashi_sio_controller.h"
#include "../pc_port/include/musashi_timer2.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    SIO_DATA = 0x1f801040u,
    SIO_STATUS = 0x1f801044u,
    SIO_MODE = 0x1f801048u,
    SIO_CONTROL = 0x1f80104au,
    SIO_BAUD = 0x1f80104eu,
    IRQ_STATUS = 0x1f801070u,
    TIMER2_COUNT = 0x1f801120u,
    HANDLER_ENABLED = 0x80072994u,
    HANDLER_ACTIVE = 0x800729acu,
    HANDLER_COUNTER = 0x800729b0u,
    HANDLER_RESULT = 0x800729c4u,
    RECORD_BASE = 0x80072990u,
    RECORD_INDEX = 0x8007299cu,
    RECORD_OFFSET = 0x800729a0u,
    SLOT_F75C = 0x80072974u,
    DISPATCH_TABLE = 0x800729e0u,
    SIO_POINTER = 0x800729c0u,
    IRQ_POINTER = 0x800729bcu,
    SERIAL_COUNTER = 0x80078998u,
    SERIAL_COUNTER_B = 0x8007899cu,
    RECORD = 0x80078a48u,
    INPUT_BUFFER = 0x80079000u,
    OUTPUT_BUFFER = 0x80079100u,
};

typedef struct SerialFixture {
    MusashiSioController sio;
    MusashiTimer2 timer;
    MusashiExecutionClock clock;
    MusashiInputDevice device;
    MusashiSioPadDevice pad;
    uint64_t epoch, first_write_cycle;
    uint16_t buttons;
    uint32_t irq_status;
    unsigned irq_count, pad_samples;
    unsigned read8_count;
    unsigned write8_count;
    unsigned write16_count;
    uint32_t write16_addresses[8];
    uint16_t write16_values[8];
    unsigned read32_count;
    unsigned write32_count;
    int refuse_read8;
    int refuse_write8;
} SerialFixture;

typedef struct FrameGuard {
    uint8_t before[16];
    MusashiBiosCallbackFrame frame;
    uint8_t after[16];
} FrameGuard;

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

static uint64_t pad_owner(void *userdata) {
    (void)userdata;
    return 1;
}

static int pad_sample(void *userdata, uint16_t *buttons) {
    SerialFixture *fixture = userdata;
    if (!fixture || !buttons) return 0;
    ++fixture->pad_samples;
    *buttons = fixture->buttons;
    return 1;
}

static int pad_raise_irq(void *userdata, uint16_t mask) {
    SerialFixture *fixture = userdata;
    if (!fixture || mask != 0x0080u) return 0;
    assert(fixture->sio.transmitted_bytes == 1);
    assert(fixture->sio.cycle >= fixture->first_write_cycle + 1088u);
    fixture->irq_count++;
    fixture->irq_status |= mask;
    return 1;
}

static int advance_clock(void *userdata, uint32_t cycles) {
    SerialFixture *fixture = userdata;
    if (!fixture || fixture->epoch > UINT64_MAX - cycles) return 0;
    fixture->epoch += cycles;
    if (!musashi_timer2_advance(&fixture->timer, fixture->epoch) ||
        !musashi_sio_controller_advance(&fixture->sio, fixture->epoch)) {
        return 0;
    }
    return 1;
}

static int fixture_read16(void *userdata, uint32_t address, uint16_t *value) {
    SerialFixture *fixture = userdata;
    if (!fixture || !value) return 0;
    if (address == TIMER2_COUNT || address == 0x1f801124u ||
        address == 0x1f801128u)
        return musashi_timer2_read16(&fixture->timer, address, value);
    return musashi_sio_controller_read16(&fixture->sio, address, value);
}

static int fixture_write16(void *userdata, uint32_t address, uint16_t value) {
    SerialFixture *fixture = userdata;
    if (!fixture) return 0;
    fixture->write16_count++;
    if (fixture->write16_count <= 8u) {
        fixture->write16_addresses[fixture->write16_count - 1u] = address;
        fixture->write16_values[fixture->write16_count - 1u] = value;
    }
    return musashi_sio_controller_write16(&fixture->sio, address, value);
}

static int fixture_read8(void *userdata, uint32_t address, uint8_t *value) {
    SerialFixture *fixture = userdata;
    if (!fixture || fixture->refuse_read8) return 0;
    fixture->read8_count++;
    return musashi_sio_controller_read8(&fixture->sio, address, value);
}

static int fixture_write8(void *userdata, uint32_t address, uint8_t value) {
    SerialFixture *fixture = userdata;
    if (!fixture) return 0;
    fixture->write8_count++;
    if (fixture->refuse_write8) return 0;
    assert(value == 1 && !fixture->sio.transmitted_bytes);
    fixture->first_write_cycle = fixture->sio.cycle;
    return musashi_sio_controller_write8(&fixture->sio, address, value);
}

static int fixture_read32(void *userdata, uint32_t address, uint32_t *value) {
    SerialFixture *fixture = userdata;
    if (!fixture || !value || address != IRQ_STATUS) return 0;
    fixture->read32_count++;
    *value = fixture->irq_status;
    return 1;
}

static int fixture_write32(void *userdata, uint32_t address, uint32_t value) {
    SerialFixture *fixture = userdata;
    if (!fixture || address != IRQ_STATUS) return 0;
    fixture->write32_count++;
    fixture->irq_status &= value;
    return 1;
}

static void init_fixture(SerialFixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->buttons = 0x1230u;
    musashi_sio_controller_init_disconnected(&fixture->sio);
    musashi_timer2_init(&fixture->timer, 0);
    fixture->pad.userdata = fixture;
    fixture->pad.owner_thread = pad_owner;
    fixture->pad.sample_buttons = pad_sample;
    fixture->pad.raise_irq = pad_raise_irq;
    assert(musashi_sio_controller_bind_digital_pad(&fixture->sio, &fixture->pad));
    fixture->clock.userdata = fixture;
    fixture->clock.advance = advance_clock;
    memset(&fixture->device, 0, sizeof(fixture->device));
    fixture->device.userdata = fixture;
    fixture->device.read16 = fixture_read16;
    fixture->device.write16 = fixture_write16;
    fixture->device.read8 = fixture_read8;
    fixture->device.write8 = fixture_write8;
    fixture->device.read32 = fixture_read32;
    fixture->device.write32 = fixture_write32;
    fixture->device.clock = &fixture->clock;
}

static void setup_record(MusashiBootMemory *memory) {
    write_word(memory, HANDLER_ENABLED, 1);
    write_word(memory, HANDLER_ACTIVE, 0);
    write_word(memory, HANDLER_COUNTER, 0);
    write_word(memory, HANDLER_RESULT, 0);
    write_word(memory, RECORD_BASE, RECORD);
    write_word(memory, RECORD_INDEX, 0);
    write_word(memory, RECORD_OFFSET, 0);
    write_word(memory, SLOT_F75C, 0x8005f75cu);
    write_word(memory, 0x8007297cu, 0x8005f830u);
    write_word(memory, 0x80072960u, 0x8005f228u);
    write_word(memory, 0x80072980u, 0x8005f384u);
    write_word(memory, DISPATCH_TABLE, 0x8005eae8u);
    write_word(memory, DISPATCH_TABLE + 4u, 0x8005eb28u);
    write_word(memory, SIO_POINTER, SIO_DATA);
    write_word(memory, IRQ_POINTER, IRQ_STATUS);
    write_word(memory, SERIAL_COUNTER, 0);
    write_word(memory, SERIAL_COUNTER_B, 0);
    write_word(memory, 0x800729b4u, 0);
    write_word(memory, RECORD + 0x30u, OUTPUT_BUFFER);
    write_word(memory, RECORD + 0x3cu, INPUT_BUFFER);
    write_word(memory, RECORD + 0x40u, OUTPUT_BUFFER + 0x10u);
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

static int run_case(MusashiBootMemory *memory, FrameGuard *guard,
                    SerialFixture *fixture, int refuse_read8,
                    int refuse_write8, int32_t *result,
                    MusashiResetGraphPrefixStop *stop) {
    memset(memory, 0xa5, sizeof(*memory));
    memset(guard->before, 0x3c, sizeof(guard->before));
    memset(guard->after, 0xc3, sizeof(guard->after));
    musashi_boot_bios_callback_frame_init(&guard->frame);
    memset(guard->frame.scratch, 0xa5, sizeof(guard->frame.scratch));
    init_fixture(fixture);
    fixture->refuse_read8 = refuse_read8;
    fixture->refuse_write8 = refuse_write8;
    setup_record(memory);
    write_word(memory, DISPATCH_TABLE + 4u, 0x8005eb28u);
    return musashi_boot_execute_input_bios_callback(
        memory, &fixture->device, &guard->frame, 0x8005d734u, 1,
        result, stop);
}

static void assert_guards(const FrameGuard *guard) {
    unsigned i;
    for (i = 0; i < sizeof(guard->before); ++i) {
        assert(guard->before[i] == 0x3c);
        assert(guard->after[i] == 0xc3);
    }
}

static void test_first_data_stage_and_next_refusal(void) {
    MusashiBootMemory memory;
    FrameGuard guard;
    SerialFixture fixture;
    MusashiResetGraphPrefixStop stop = {0, 0};
    int32_t result = 0x2468ace0;
    static const uint32_t expected_addresses[] = {
        SIO_CONTROL, SIO_CONTROL, SIO_MODE, SIO_BAUD, SIO_CONTROL, SIO_CONTROL
    };
    static const uint16_t expected_values[] = {0x40, 0, 0x0d, 0x88, 0x1003, 0x1013};
    unsigned i;

    assert(!run_case(&memory, &guard, &fixture, 0, 0, &result, &stop));
    assert(result == 0x2468ace0);
    assert(guard.frame.faulted && !guard.frame.busy);
    /* This partial fixture deliberately leaves the optional global hook
     * unowned (A5 fill). Index1 executes up to its actual indirect hook. */
    assert(stop.call_address == 0x8005eb6cu && stop.target_address == 0xa5a5a5a5u);
    assert(read_word(&memory, RECORD_OFFSET) == 2);
    assert(read_word(&memory, HANDLER_RESULT) == 1);
    assert(fixture.read8_count == 1 && fixture.write8_count == 1);
    assert(fixture.sio.transmitted_bytes == 1);
    assert(fixture.write16_count == 6);
    for (i = 0; i < 6; ++i) {
        assert(fixture.write16_addresses[i] == expected_addresses[i]);
        assert(fixture.write16_values[i] == expected_values[i]);
    }
    assert(fixture.epoch > 0 && fixture.timer.last_cycles == fixture.epoch);
    assert(fixture.sio.ack_pending);
    assert(musashi_sio_controller_advance(&fixture.sio, fixture.sio.ack_due));
    assert(fixture.irq_count == 1 && (fixture.sio.status & 0x0200u));
    assert(musashi_sio_controller_write16(&fixture.sio, SIO_CONTROL, 0x1013));
    assert(fixture.sio.status == 7 && fixture.sio.control == 0x1003);
    assert(*ram_byte(&memory, OUTPUT_BUFFER) == 0);
    assert(*ram_byte(&memory, OUTPUT_BUFFER + 1u) == 0);
    assert(*ram_byte(&memory, OUTPUT_BUFFER + 0x10u) == 1);
    assert_guards(&guard);
}

static void test_missing_data_refuses_without_late_writes(void) {
    MusashiBootMemory memory;
    FrameGuard guard;
    SerialFixture fixture;
    MusashiResetGraphPrefixStop stop = {0, 0};
    int32_t result = 0x13579bdf;

    assert(!run_case(&memory, &guard, &fixture, 1, 0, &result, &stop));
    assert(result == 0x13579bdf && guard.frame.faulted && !guard.frame.busy);
    assert(stop.call_address == 0x8005dccc);
    assert(fixture.write8_count == 0);
    assert(read_word(&memory, RECORD_OFFSET) == 1);
    assert_guards(&guard);
}

static void test_data_write_refusal_preserves_device(void) {
    MusashiBootMemory memory;
    FrameGuard guard;
    SerialFixture fixture;
    MusashiResetGraphPrefixStop stop = {0, 0};
    int32_t result = 0x11223344;
    uint16_t old_status;
    uint16_t old_control;

    assert(!run_case(&memory, &guard, &fixture, 0, 1, &result, &stop));
    assert(result == 0x11223344 && guard.frame.faulted && !guard.frame.busy);
    assert(stop.call_address == 0x8005dd38u);
    old_status = fixture.sio.status;
    old_control = fixture.sio.control;
    assert(fixture.write8_count == 1 && old_status == 5 && old_control == 0x1003);
    assert(read_word(&memory, RECORD_OFFSET) == 1);
    assert_guards(&guard);
}

static void test_exact_sra_sign_behavior(void) {
    MusashiBootMemory memory;
    SerialFixture fixture;
    FormatterCpu cpu = {0};

    assert(formatter_sra(0x80000000u, 0) == 0x80000000u);
    assert(formatter_sra(0x80000000u, 1) == 0xc0000000u);
    assert(formatter_sra(0x80000000u, 31) == 0xffffffffu);
    assert(formatter_sra(0x7fffffffu, 31) == 0);
    assert(formatter_sra(0xffffffffu, 31) == 0xffffffffu);
    memset(&memory, 0, sizeof(memory));
    init_fixture(&fixture);
    cpu.input_device = &fixture.device;
    cpu.clock = NULL;
    cpu.pc = 0x8005dd54u;
    cpu.npc = cpu.pc + 4u;
    cpu.r[2] = 0x80000000u;
    assert(formatter_step(&memory, &cpu));
    assert(cpu.r[2] == 0xf8000000u);
    cpu.r[2] = 0x7fffffffu;
    cpu.pc = 0x8005dd54u;
    cpu.npc = cpu.pc + 4u;
    assert(formatter_step(&memory, &cpu));
    assert(cpu.r[2] == 0x07ffffffu);
}

static void test_serial_callsite_and_live_slot_guards(void) {
    MusashiBootMemory memory = {0};
    SerialFixture fixture;
    FormatterCpu cpu = {0};
    static const uint32_t direct[][3] = {
        {0x8005eb10u, 0x0c017728u, 0x8005dca0u},
        {0x8005dd1cu, 0x0c017ef2u, 0x8005fbc8u},
        {0x8005dc34u, 0x0c01782bu, 0x8005e0acu},
        {0x8005e0e0u, 0x0c017ef2u, 0x8005fbc8u},
    };
    unsigned i;
    init_fixture(&fixture);
    setup_record(&memory);
    cpu.input_device = &fixture.device;
    cpu.pc = 0x8005dc04u;
    cpu.r[2] = 0x8005eae8u;
    assert(input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[2]));
    for (i = 1; i <= 5; ++i) {
        write_word(&memory, RECORD_OFFSET, i);
        assert(!input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[2]));
    }
    write_word(&memory, RECORD_OFFSET, 0);
    write_word(&memory, DISPATCH_TABLE, 0x8005f830u);
    assert(!input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[2]));
    write_word(&memory, DISPATCH_TABLE, 0x8005eae8u);
    assert(!input_callback_call(&memory, &cpu, 0x0040f809u, 0x8005f830u));
    cpu.pc = 0x8005eafcu;
    cpu.r[2] = 0x8005f75cu;
    assert(input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[2]));
    write_word(&memory, SLOT_F75C, 0x8005f830u);
    assert(!input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[2]));
    for (i = 0; i < sizeof(direct) / sizeof(direct[0]); ++i) {
        cpu.pc = direct[i][0];
        assert(input_callback_call(&memory, &cpu, direct[i][1], direct[i][2]));
        assert(!input_callback_call(&memory, &cpu, direct[i][1] ^ 1u, direct[i][2]));
        assert(!input_callback_call(&memory, &cpu, direct[i][1], 0x8005f830u));
    }
}

int main(void) {
    test_first_data_stage_and_next_refusal();
    test_missing_data_refuses_without_late_writes();
    test_data_write_refusal_preserves_device();
    test_exact_sra_sign_behavior();
    test_serial_callsite_and_live_slot_guards();
    return 0;
}
