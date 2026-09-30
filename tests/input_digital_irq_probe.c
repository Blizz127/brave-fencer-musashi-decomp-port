#include "../pc_port/mips_formatter.c"
#include "../pc_port/include/musashi_sio_controller.h"
#include "../pc_port/include/musashi_timer2.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
    uint64_t epoch, last_due;
    uint8_t tx[128], rx[128];
    int tx_port[128];
    uint64_t tx_cycle[128], tx_due[128];
    int tx_ack[128];
    uint16_t buttons;
    unsigned steps, fail_step;
    int freeze_timer;
    MusashiBiosCallbackFrame *frame;
    uint32_t corrupt_address, corrupt_expected, corrupt_replacement;
    int corrupted;
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
    assert(fixture->sio.cycle >= fixture->last_due);
    fixture->irq_count++;
    fixture->irq_status |= mask;
    return 1;
}

static int advance_clock(void *userdata, uint32_t cycles) {
    SerialFixture *fixture = userdata;
    if (!fixture || fixture->epoch > UINT64_MAX - cycles) return 0;
    ++fixture->steps;
    if (fixture->fail_step && fixture->steps == fixture->fail_step) return 0;
    if (fixture->corrupt_address && !fixture->corrupted) {
        uint8_t *p = fixture->frame->scratch + fixture->corrupt_address -
                     MUSASHI_BIOS_CALLBACK_STACK_BASE;
        uint32_t word = p[0] | ((uint32_t)p[1] << 8) |
                        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        if (word == fixture->corrupt_expected) {
            for (unsigned i = 0; i < 4; ++i)
                p[i] = (uint8_t)(fixture->corrupt_replacement >> (8u*i));
            fixture->corrupted = 1;
        }
    }
    fixture->epoch += cycles;
    if ((!fixture->freeze_timer &&
         !musashi_timer2_advance(&fixture->timer, fixture->epoch)) ||
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
    if (!musashi_sio_controller_read8(&fixture->sio, address, value)) return 0;
    assert(fixture->read8_count < 128);
    fixture->rx[fixture->read8_count++] = *value;
    return 1;
}

static int fixture_write8(void *userdata, uint32_t address, uint8_t value) {
    SerialFixture *fixture = userdata;
    if (!fixture) return 0;
    if (fixture->refuse_write8) return 0;
    unsigned i = fixture->write8_count;
    assert(i < 128);
    fixture->tx[i] = value;
    fixture->tx_port[i] = fixture->sio.selected_port;
    fixture->tx_cycle[i] = fixture->sio.cycle;
    if (!musashi_sio_controller_write8(&fixture->sio, address, value)) return 0;
    fixture->tx_ack[i] = fixture->sio.ack_pending;
    fixture->tx_due[i] = fixture->sio.ack_due;
    if (fixture->sio.ack_pending) {
        assert(fixture->sio.ack_due == fixture->sio.cycle + 1088u);
        fixture->last_due = fixture->sio.ack_due;
    }
    ++fixture->write8_count;
    return 1;
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

/* Synthetic hardware fixture, mapped pinned EXE data and exact F0C8 source
 * initialization. No serial priming, captured RAM, or completion writes. */
static void setup_cold(MusashiBootMemory *memory, FrameGuard *guard,
                       SerialFixture *fixture, const char *exe_path) {
    FILE *file = fopen(exe_path, "rb");
    long length;
    uint8_t *bytes;
    MusashiResetGraphPrefixStop stop = {0};
    assert(file && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    memset(memory, 0, sizeof(*memory));
    assert(musashi_boot_map_exe(memory, bytes, (size_t)length));
    free(bytes);
    assert(musashi_boot_run_8005f0c8_prefix(memory, &stop) ==
           MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
    memset(guard, 0xa5, sizeof(*guard));
    musashi_boot_bios_callback_frame_init(&guard->frame);
    memset(memory->bytes, 0x5a, 0x10000);
    init_fixture(fixture);
    fixture->frame = &guard->frame;
}

static void assert_isolated(const MusashiBootMemory *memory,
                            const FrameGuard *guard) {
    for (unsigned i = 0; i < 0x10000; ++i) assert(memory->bytes[i] == 0x5a);
    for (unsigned i = 0; i < 16; ++i) {
        assert(guard->before[i] == 0xa5 && guard->after[i] == 0xa5);
    }
}

static int invoke(MusashiBootMemory *memory, FrameGuard *guard,
                  SerialFixture *fixture, int32_t *result,
                  MusashiResetGraphPrefixStop *stop) {
    return musashi_boot_execute_input_bios_callback(memory, &fixture->device,
        &guard->frame, 0x8005d734u, 1, result, stop);
}

static void test_packets_and_source_retry(const char *path, uint64_t epoch) {
    static MusashiBootMemory memory;
    FrameGuard guard;
    SerialFixture fixture;
    MusashiResetGraphPrefixStop stop = {0};
    uint8_t previous_packet[4] = {0xff, 0, 0, 0};
    static uint8_t before[MUSASHI_RAM_SIZE];
    setup_cold(&memory, &guard, &fixture, path);
    memcpy(before, memory.bytes, sizeof(before));
    /* Alternate absolute clock phases exercise the real FFFF wrap path.
     * This advances devices, never seeds Timer2 count or guest state. */
    fixture.epoch = epoch;
    assert(musashi_timer2_advance(&fixture.timer, epoch));
    assert(musashi_sio_controller_advance(&fixture.sio, epoch));
    for (unsigned round = 0; round < 7; ++round) {
        int32_t result = 0x2468ace0;
        int digital = round == 0 || round >= 4;
        unsigned tx_start = fixture.write8_count, rx_start = fixture.read8_count;
        unsigned irq_start = fixture.irq_count, step_start = fixture.steps;
        uint64_t cycle_start = fixture.epoch;
        fixture.buttons = (uint16_t)(0x1230u ^ (round * 0x1101u));
        uint16_t sampled = fixture.buttons | 6u;
        assert(invoke(&memory, &guard, &fixture, &result, &stop));
        assert(result == 0 && !guard.frame.faulted && !guard.frame.busy);
        assert(stop.call_address == UINT_MAX && stop.target_address == UINT_MAX);
        assert(read_word(&memory, RECORD_INDEX) == 2);
        assert(read_word(&memory, RECORD_OFFSET) == 0);
        assert(read_word(&memory, HANDLER_RESULT) == 1);
        assert(read_word(&memory, 0x800729b4u) == (digital ? 0u : UINT_MAX));
        assert(read_word(&memory, 0x800729b8u) == 0xffffffffu);
        assert(fixture.pad_samples == round + 1u);
        assert(fixture.irq_count - irq_start == (digital ? 4u : 1u));
        assert(fixture.irq_status == 0 && !fixture.sio.ack_pending);
        assert(fixture.sio.control == 0 && fixture.sio.baud == 0x88);
        assert(fixture.sio.selected_port == -1 && !fixture.sio.faulted);
        assert(fixture.timer.last_cycles == fixture.epoch &&
               fixture.sio.cycle == fixture.epoch);
        assert(fixture.steps - step_start < 16384);
        assert(fixture.write8_count - tx_start == (digital ? 6u : 3u));
        assert(fixture.tx[tx_start] == 1 && fixture.tx_port[tx_start] == 0);
        assert(fixture.tx[tx_start + 1] == (digital ? 0x42 : 0x43));
        unsigned last = fixture.write8_count - 1u;
        assert(fixture.tx[last] == 1 && fixture.tx_port[last] == 1);
        assert(!fixture.tx_ack[last]); /* Absent port never schedules ACK. */
        assert(fixture.epoch - fixture.tx_cycle[last] >= 3200u);
        for (unsigned i = tx_start; i < last; ++i) {
            assert(fixture.tx_port[i] == 0);
            if (i > tx_start + 1u) assert(fixture.tx[i] == 0);
            assert(fixture.tx_ack[i] == (i < last - 1u));
            if (fixture.tx_ack[i])
                assert(fixture.tx_due[i] - fixture.tx_cycle[i] == 1088u);
        }
        /* The final digital byte and rejected43 both create no new ACK. */
        assert(!fixture.tx_ack[last - 1u]);
        assert(fixture.read8_count - rx_start == (digital ? 8u : 5u));
        assert(fixture.rx[rx_start] == 0xff && fixture.rx[rx_start + 1] == 0xff);
        assert(memcmp(memory.bytes + 0x78da0, previous_packet, 4) == 0);
        assert(memory.bytes[0x78dec] == 0xff && memory.bytes[0x78ded] == 0);
        if (digital) {
            const uint8_t response[4] = {0x41, 0x5a, (uint8_t)sampled,
                                        (uint8_t)(sampled >> 8)};
            assert(memcmp(fixture.rx + rx_start + 2, response, 4) == 0);
            assert(memcmp(memory.bytes + 0x789b8, response, 4) == 0);
            previous_packet[0] = 0; previous_packet[1] = 0x41;
            previous_packet[2] = (uint8_t)sampled;
            previous_packet[3] = (uint8_t)(sampled >> 8);
        } else {
            for (unsigned i = rx_start; i < fixture.read8_count; ++i)
                assert(fixture.rx[i] == 0xff);
        }
        assert(memory.bytes[0x78a48 + 0x46] ==
               (round == 0 ? 0 : round < 3 ? 1 : 0xff));
        assert_isolated(&memory, &guard);
        for (unsigned i = 0; i < MUSASHI_RAM_SIZE; ++i) {
            int source_writable = (i >= 0x72990 && i < 0x729e0) ||
                (i >= 0x78998 && i < 0x78c28) ||
                (i >= 0x78da0 && i < 0x78da4) ||
                (i >= 0x78dec && i < 0x78df0) ||
                (i >= 0x78f24 && i < 0x78f28) || /* FBA8 saved Timer2 sample */
                (i >= 0xc5320 && i < 0xc5324);  /* FBA8 timeout threshold */
            if (!source_writable) {
                if (memory.bytes[i] != before[i])
                    fprintf(stderr, "unexpected source write at %08x\n", 0x80000000u+i);
                assert(memory.bytes[i] == before[i]);
            }
        }
        fprintf(stderr, "DIGITAL_FIXTURE round=%u command=%02x tx=%u irq=%u "
                "instructions=%u cycles=%llu return=0 port=2\n", round,
                digital ? 0x42 : 0x43, fixture.write8_count - tx_start,
                fixture.irq_count - irq_start, fixture.steps - step_start,
                (unsigned long long)(fixture.epoch - cycle_start));
    }
}

static void test_clock_budget_and_saved_return_refusals(const char *path) {
    static MusashiBootMemory memory;
    FrameGuard guard;
    SerialFixture fixture;
    MusashiResetGraphPrefixStop stop = {0};
    int32_t result;
    for (unsigned which = 0; which < 5; ++which) {
        setup_cold(&memory, &guard, &fixture, path);
        result = 0x13579bdf;
        if (which < 2) fixture.fail_step = which ? 100 : 1;
        if (which == 2) fixture.freeze_timer = 1; /* Deliberately broken clock. */
        if (which == 4) {
            /* Cold records skip F830 until the first source packet completes.
             * Warm up through that genuine return before corrupting its RA. */
            assert(invoke(&memory, &guard, &fixture, &result, &stop));
            assert(result == 0);
            result = 0x13579bdf;
        }
        if (which >= 3) {
            fixture.corrupt_address = which == 3 ? 0x85d0u : 0x8598u;
            fixture.corrupt_expected = which == 3 ? UINT_MAX : 0x8005db0cu;
            fixture.corrupt_replacement = which == 3 ? 0x8005d6ccu : 0x8005f384u;
        }
        assert(!invoke(&memory, &guard, &fixture, &result, &stop));
        assert(result == 0x13579bdf && guard.frame.faulted && !guard.frame.busy);
        if (which < 2) assert(fixture.steps == fixture.fail_step);
        if (which == 0) assert(!fixture.sio.transmitted_bytes && !fixture.epoch);
        if (which == 2) {
            assert(fixture.steps == 16384);
            /* Frozen Timer2 stalls the source pre-transmit timing loop,
             * before the first byte; budget refusal must not complete it. */
            assert(fixture.sio.transmitted_bytes == 0 && fixture.timer.count == 0);
            assert(read_word(&memory, RECORD_INDEX) == 0);
        }
        if (which >= 3) {
            assert(fixture.corrupted);
            assert(stop.call_address == (which == 3 ? 0x8005d898u : 0x8005fa8cu));
            assert(stop.target_address == fixture.corrupt_replacement);
        }
        unsigned before = fixture.steps;
        assert(!invoke(&memory, &guard, &fixture, &result, &stop));
        assert(fixture.steps == before); /* Sticky refusal does no more work. */
        assert_isolated(&memory, &guard);
    }
}

static void test_indirect_identity_guards(const char *path) {
    static MusashiBootMemory memory;
    FrameGuard guard;
    SerialFixture fixture;
    FormatterCpu cpu = {0};
    static const uint32_t table[] = {0x8005eae8u,0x8005eb28u,0x8005ec00u,
                                     0x8005ecc0u,0x8005ed4cu};
    setup_cold(&memory, &guard, &fixture, path);
    cpu.input_device = &fixture.device;
    cpu.pc = 0x8005dc04u;
    for (unsigned index = 0; index < 5; ++index) {
        assert(musashi_boot_write32(&memory, RECORD_OFFSET, index));
        cpu.r[2] = table[index];
        assert(input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[2]));
        assert(!input_callback_call(&memory, &cpu, 0x0040f809u, table[(index+1)%5]));
        assert(musashi_boot_write32(&memory, DISPATCH_TABLE + index*4u, 0x8005f830u));
        assert(!input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[2]));
        assert(musashi_boot_write32(&memory, DISPATCH_TABLE + index*4u, table[index]));
    }
    assert(musashi_boot_write32(&memory, RECORD_OFFSET, 5));
    assert(!input_callback_call(&memory, &cpu, 0x0040f809u, 0x1f801040u));
    cpu.pc = 0x8005daa0u; cpu.r[3] = 0x8005f830u;
    assert(input_callback_call(&memory, &cpu, 0x0060f809u, cpu.r[3]));
    assert(!input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[3]));
    const uint32_t refused[] = {0x8005d718u,0x8005eb6cu,0x8005eb80u,
                                0x8005f808u,0x8005fa58u};
    for (unsigned i = 0; i < sizeof(refused)/sizeof(refused[0]); ++i) {
        cpu.pc = refused[i]; cpu.r[2] = 0x8005f228u;
        assert(!input_callback_call(&memory, &cpu, 0x0040f809u, cpu.r[2]));
    }
    /* An actual wrong live table word retains the JALR delay's index store. */
    setup_cold(&memory, &guard, &fixture, path);
    assert(musashi_boot_write32(&memory, DISPATCH_TABLE + 4u, 0x8005f830u));
    MusashiResetGraphPrefixStop stop = {0};
    int32_t result = 0x13579bdf;
    assert(!invoke(&memory, &guard, &fixture, &result, &stop));
    assert(stop.call_address == 0x8005dc04u && stop.target_address == 0x8005f830u);
    assert(result == 0x13579bdf && read_word(&memory, RECORD_OFFSET) == 2);
    assert(fixture.sio.transmitted_bytes == 1);
    assert_isolated(&memory, &guard);
    /* Deliberately unsupported configuration record: a mapped leaf address
     * in its callback field still cannot bypass the F808 ownership boundary. */
    setup_cold(&memory, &guard, &fixture, path);
    memory.bytes[0x78a48 + 0x46] = 2;
    assert(musashi_boot_write32(&memory, RECORD + 0x14u, 0x8005f228u));
    result = 0x13579bdf;
    assert(!invoke(&memory, &guard, &fixture, &result, &stop));
    assert(stop.call_address == 0x8005f808u && stop.target_address == 0x8005f228u);
    assert(result == 0x13579bdf && fixture.sio.transmitted_bytes == 0);
    assert(read_word(&memory, RECORD + 0x14u) == 0x8005f228u);
    assert_isolated(&memory, &guard);
}

static void test_audited_edge_matrix_and_instruction_budget(const char *path) {
    static MusashiBootMemory memory;
    FrameGuard guard;
    SerialFixture fixture;
    FormatterCpu cpu = {0};
    static const uint32_t edges[][4] = {
        {0x8005d7fcu,0x0c017671u,0x8005d9c4u,0},
        {0x8005d818u,0x0040f809u,0x8005f290u,0x8007295cu},
        {0x8005d858u,0x0c0176f6u,0x8005dbd8u,0},
        {0x8005da10u,0x0c017eeau,0x8005fba8u,0},
        {0x8005daa0u,0x0060f809u,0x8005f830u,0x8007297cu},
        {0x8005db04u,0x0040f809u,0x8005f830u,0x8007297cu},
        {0x8005db18u,0x0040f809u,0x8005f384u,0x80072980u},
        {0x8005db68u,0x0c01784fu,0x8005e13cu,0},
        {0x8005dc2cu,0x0c017eeau,0x8005fba8u,0},
        {0x8005dc34u,0x0c01782bu,0x8005e0acu,0},
        {0x8005dc50u,0x0040f809u,0x8005f290u,0x8007295cu},
        {0x8005dc88u,0x0040f809u,0x8005f290u,0x8007295cu},
        {0x8005dd1cu,0x0c017ef2u,0x8005fbc8u,0},
        {0x8005ddf0u,0x0c017ef2u,0x8005fbc8u,0},
        {0x8005deecu,0x0c017eeau,0x8005fba8u,0},
        {0x8005e028u,0x0c017eeau,0x8005fba8u,0},
        {0x8005e030u,0x0c017ef2u,0x8005fbc8u,0},
        {0x8005e0e0u,0x0c017ef2u,0x8005fbc8u,0},
        {0x8005e1e8u,0x0c017a95u,0x8005ea54u,0},
        {0x8005e1fcu,0x0c017a9au,0x8005ea68u,0},
        {0x8005e210u,0x0c017aaau,0x8005eaa8u,0},
        {0x8005eafcu,0x0040f809u,0x8005f75cu,0x80072974u},
        {0x8005eb10u,0x0c017728u,0x8005dca0u,0},
        {0x8005ebacu,0x0040f809u,0x8005f75cu,0x80072974u},
        {0x8005ebc4u,0x0040f809u,0x8005f75cu,0x80072974u},
        {0x8005ebe8u,0x0c01779eu,0x8005de78u,0},
        {0x8005ec2cu,0x0040f809u,0x8005f75cu,0x80072974u},
        {0x8005ec44u,0x0040f809u,0x8005f75cu,0x80072974u},
        {0x8005ec64u,0x0c01779eu,0x8005de78u,0},
        {0x8005ed04u,0x0040f809u,0x8005f394u,0x80072964u},
        {0x8005ed10u,0x0c01779eu,0x8005de78u,0},
        {0x8005ed74u,0x0040f809u,0x8005f450u,0x80072968u},
        {0x8005edfcu,0x0040f809u,0x8005f450u,0x80072968u},
        {0x8005ee14u,0x0040f809u,0x8005f394u,0x80072964u},
        {0x8005ee20u,0x0c01779eu,0x8005de78u,0},
        {0x8005ee30u,0x0c017eeau,0x8005fba8u,0},
        {0x8005ee38u,0x0c01782bu,0x8005e0acu,0},
        {0x8005eed8u,0x0040f809u,0x8005f830u,0x8007297cu},
        {0x8005ef34u,0x0040f809u,0x8005f830u,0x8007297cu},
        {0x8005ef60u,0x0040f809u,0x8005f830u,0x8007297cu},
        {0x8005ef74u,0x0040f809u,0x8005f384u,0x80072980u},
        {0x8005ef94u,0x0040f809u,0x8005f394u,0x80072964u},
        {0x8005efa0u,0x0c017728u,0x8005dca0u,0},
        {0x8005efb0u,0x0c017eeau,0x8005fba8u,0},
        {0x8005efb8u,0x0c01782bu,0x8005e0acu,0},
        {0x8005f00cu,0x0040f809u,0x8005f394u,0x80072964u},
        {0x8005f018u,0x0c017728u,0x8005dca0u,0},
        {0x8005f028u,0x0c017eeau,0x8005fba8u,0},
        {0x8005f030u,0x0c01782bu,0x8005e0acu,0},
        {0x8005f05cu,0x0c01784fu,0x8005e13cu,0},
        {0x8005f098u,0x0040f809u,0x8005f290u,0x8007295cu},
        {0x8005f2f4u,0x0c017ea5u,0x8005fa94u,0},
        {0x8005f2fcu,0x0c017ce1u,0x8005f384u,0},
        {0x8005f34cu,0x0c017671u,0x8005d9c4u,0},
        {0x8005f464u,0x0c0170b2u,0x8005c2c8u,0},
        {0x8005f7d8u,0x0c017a8du,0x8005ea34u,0},
        {0x8005f7e8u,0x0c017a8du,0x8005ea34u,0},
        {0x8005f818u,0x0c017869u,0x8005e1a4u,0},
        {0x8005f910u,0x0c017edcu,0x8005fb70u,0},
        {0x8005f95cu,0x0040f809u,0x8005f228u,0x80072960u},
        {0x8005f9a4u,0x0040f809u,0x8005f228u,0x80072960u},
        {0x8005fa68u,0x0c01788au,0x8005e228u,0},
        {0x8005fb24u,0x0040f809u,0x8005f228u,0x80072960u},
    };
    setup_cold(&memory, &guard, &fixture, path);
    cpu.input_device = &fixture.device;
    for (unsigned i = 0; i < sizeof(edges)/sizeof(edges[0]); ++i) {
        uint32_t word;
        cpu.pc = edges[i][0];
        cpu.r[(edges[i][1] >> 21) & 31u] = edges[i][2];
        assert(input_callback_word(&memory, &cpu, cpu.pc, &word) && word == edges[i][1]);
        assert(input_callback_call(&memory, &cpu, word, edges[i][2]));
        assert(!input_callback_call(&memory, &cpu, word ^ 1u, edges[i][2]));
        uint32_t wrong = edges[i][2] == 0x8005f830u ? 0x8005f384u : 0x8005f830u;
        assert(!input_callback_call(&memory, &cpu, word, wrong));
        if (edges[i][3]) {
            assert(musashi_boot_write32(&memory, edges[i][3], wrong));
            assert(!input_callback_call(&memory, &cpu, word, edges[i][2]));
            assert(musashi_boot_write32(&memory, edges[i][3], edges[i][2]));
        }
    }
    /* One budget unit cannot silently execute a call AND its delay. */
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x8005eb10u; cpu.npc = cpu.pc + 4u;
    unsigned remaining = 1;
    MusashiResetGraphPrefixStop stop = {0};
    assert(input_callback_step(&memory, &cpu, &remaining, &stop));
    assert(!remaining && cpu.pc == 0x8005eb14u && cpu.r[31] == 0x8005eb18u);
    assert(!input_callback_step(&memory, &cpu, &remaining, &stop));
    assert(stop.call_address == 0x8005eb14u && stop.target_address == 0x8005eb14u);
    assert(cpu.pc == 0x8005eb14u);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    test_packets_and_source_retry(argv[1], 0);
    test_packets_and_source_retry(argv[1], 0xff00);
    test_clock_budget_and_saved_return_refusals(argv[1]);
    test_indirect_identity_guards(argv[1]);
    test_audited_edge_matrix_and_instruction_budget(argv[1]);
    return 0;
}
