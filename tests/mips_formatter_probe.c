#include "musashi_boot_memory.h"
#include "musashi_cd_controller.h"
#include "musashi_callback_registration.h"
#include "musashi_vblank.h"
#ifdef MUSASHI_WITH_PSYCROSS
#include <SDL.h>
#include "musashi_irq_scheduler.h"
#include "irq_cpu_fixture.h"
extern SDL_mutex *g_intrMutex;
#endif

#include <assert.h>
#include <string.h>

typedef struct Output {
    unsigned count;
    uint8_t bytes[64];
} Output;

typedef struct CallbackDeviceLog {
    unsigned read16_count;
    unsigned write16_count;
    unsigned write32_count;
    uint32_t write16_address[8];
    uint16_t write16_value[8];
    uint32_t write32_address[3];
    uint32_t write32_value[3];
    unsigned hook_count;
    uint32_t hook_context;
    uint16_t mask;
    unsigned registration_b0_count, registration_c0_count;
    MusashiCallbackRegistrationDevice registration;
    unsigned a0_72_count;
    uint32_t a0_72_table;
    unsigned exit_critical_count;
    unsigned gpu_cw_count;
    uint32_t gpu_cw_command;
} CallbackDeviceLog;

typedef struct ResetGpuLog {
    uint16_t mask;
} ResetGpuLog;

typedef struct CdDeviceLog {
    unsigned enter_count;
    unsigned exit_count;
    unsigned b0_5b_count;
    unsigned b0_4a_count;
    unsigned b0_4b_count;
    unsigned a0_70_count;
} CdDeviceLog;

typedef struct EventDeviceLog {
    unsigned enter_count;
    unsigned open_count;
    unsigned enable_count;
    unsigned b0_0b_count;
    unsigned exit_count;
} EventDeviceLog;

typedef struct InputDeviceLog {
    unsigned enter_count;
    unsigned c0_3_count;
    unsigned c0_2_count;
    unsigned c0_0a_count;
    unsigned exit_count;
} InputDeviceLog;

typedef struct RetryDeviceLog { int32_t result; unsigned count; } RetryDeviceLog;
typedef struct TickDeviceLog { int32_t tick; unsigned count; } TickDeviceLog;
typedef struct HookLog { unsigned count; uint32_t context; } HookLog;

#ifdef MUSASHI_WITH_PSYCROSS
typedef struct SchedulerHardware {
    uint16_t status;
    uint16_t mask;
    unsigned raised;
} SchedulerHardware;

static int scheduler_read16(void *userdata, uint32_t address, uint16_t *value) {
    SchedulerHardware *hardware = userdata;
    if (address == 0x1f801070u)
        *value = hardware->status;
    else if (address == 0x1f801074u)
        *value = hardware->mask;
    else
        return 0;
    return 1;
}

static int scheduler_write16(void *userdata, uint32_t address, uint16_t value) {
    SchedulerHardware *hardware = userdata;
    if (address != 0x1f801070u)
        return 0;
    hardware->status &= value;
    return 1;
}

static int scheduler_execute(void *userdata, MusashiBootMemory *memory,
                             uint32_t target) {
    (void)userdata;
    if (target != 0x80042ce8u)
        return 0;
    return musashi_boot_call_80042ce8(memory, NULL, NULL);
}

static int scheduler_raise_vblank(void *userdata) {
    SchedulerHardware *hardware = userdata;
    hardware->raised++;
    hardware->status |= 1u;
    return 1;
}
#endif

static void sink(void *userdata, const uint8_t *bytes, int32_t length) {
    Output *output = userdata;
    assert(length == 1);
    assert(output->count < sizeof(output->bytes));
    output->bytes[output->count++] = bytes[0];
}

static uint16_t callback_read16(void *userdata, uint32_t address) {
    CallbackDeviceLog *log = userdata;
    assert(address == 0x1f801074u);
    log->read16_count++;
    return log->mask;
}

static int refresh_tick(void *userdata, int32_t *tick) {
    TickDeviceLog *log = userdata;
    log->count++;
    *tick = log->tick;
    return 1;
}

static void callback_write16(void *userdata, uint32_t address, uint16_t value) {
    CallbackDeviceLog *log = userdata;
    assert(log->write16_count < 8);
    log->write16_address[log->write16_count] = address;
    log->write16_value[log->write16_count++] = value;
    if (address == 0x1f801074u) log->mask = value;
}

static void callback_write32(void *userdata, uint32_t address, uint32_t value) {
    CallbackDeviceLog *log = userdata;
    assert(log->write32_count < 3);
    log->write32_address[log->write32_count] = address;
    log->write32_value[log->write32_count++] = value;
}

static int callback_hook_entry_int(void *userdata, uint32_t context_address) {
    CallbackDeviceLog *log = userdata;
    log->hook_count++;
    log->hook_context = context_address;
    return 1;
}

static int isolated_hook_entry_int(void *userdata, uint32_t context_address) {
    HookLog *log = userdata;
    log->count++;
    log->context = context_address;
    return 1;
}

static int registration_read16(void *userdata, uint32_t address, uint16_t *value) {
    if (address != 0x1f801074u) return 0;
    *value = callback_read16(userdata, address);
    return 1;
}

static int registration_write16(void *userdata, uint32_t address, uint16_t value) {
    if (address != 0x1f801074u) return 0;
    callback_write16(userdata, address, value);
    return 1;
}

static int registration_b0(void *userdata, int32_t disabled) {
    CallbackDeviceLog *log = userdata;
    assert(disabled == 0 && log->mask == 0);
    log->registration_b0_count++;
    return 1;
}

static int registration_c0(void *userdata, int32_t channel, int32_t disabled) {
    CallbackDeviceLog *log = userdata;
    assert(channel == 3 && disabled == 0 && log->registration_b0_count == 1);
    log->registration_c0_count++;
    return 1;
}

static void init_registration(MusashiCallbackDevice *device, CallbackDeviceLog *log) {
    log->registration = (MusashiCallbackRegistrationDevice){
        log, registration_read16, registration_write16, registration_b0, registration_c0
    };
    device->registration_device = &log->registration;
}

static void check_registered_state(MusashiBootMemory *memory,
                                    const CallbackDeviceLog *log, int with_cd) {
    uint32_t value;
    assert(musashi_boot_read32(memory, 0x8006bb00u, &value) && value == 0x80042ce8u);
    assert(musashi_boot_read32(memory, 0x8006bb0cu, &value) && value == 0x80042e08u);
    if (with_cd)
        assert(musashi_boot_read32(memory, 0x8006bb08u, &value) && value == 0x80045290u);
    assert(musashi_boot_read32(memory, 0x8006bb2cu, &value));
    assert((value & 0xffffu) == (with_cd ? 13u : 9u));
    assert(log->mask == (with_cd ? 13u : 9u));
    assert(log->read16_count == (with_cd ? 4u : 3u));
    assert(log->write16_count == (with_cd ? 8u : 6u));
    assert(log->registration_b0_count == 1 && log->registration_c0_count == 1);
}

static int callback_a0_72(void *userdata, uint32_t table_address) {
    CallbackDeviceLog *log = userdata;
    log->a0_72_count++;
    log->a0_72_table = table_address;
    return 1;
}

static int callback_exit_critical(void *userdata) {
    CallbackDeviceLog *log = userdata;
    log->exit_critical_count++;
    return 1;
}

static int input_enter_critical(void *userdata) {
    ((InputDeviceLog *)userdata)->enter_count++;
    return 1;
}

static int input_c0_3(void *userdata, int32_t argument, uint32_t buffer_address) {
    InputDeviceLog *log = userdata;
    assert(argument == 2 && buffer_address == 0x80078988u);
    log->c0_3_count++;
    return 1;
}

static int input_c0_2(void *userdata, int32_t argument, uint32_t buffer_address) {
    InputDeviceLog *log = userdata;
    assert(argument == 2 && buffer_address == 0x80078988u);
    log->c0_2_count++;
    return 1;
}

static int input_c0_0a(void *userdata, int32_t argument, int32_t value) {
    InputDeviceLog *log = userdata;
    assert(argument == 3 && value == 0);
    log->c0_0a_count++;
    return 1;
}

static int input_exit_critical(void *userdata) {
    ((InputDeviceLog *)userdata)->exit_count++;
    return 1;
}

static int retry_probe(void *userdata, int32_t *result) {
    RetryDeviceLog *log = userdata;
    log->count++;
    *result = log->result;
    return 1;
}

typedef struct CdRegisterLog {
    unsigned commands, ticks;
    int accept;
    unsigned respond, pending, consumed, acknowledgements;
} CdRegisterLog;
static int cd_register_read(void *userdata, uint32_t address, uint8_t *value) {
    CdRegisterLog *log = userdata;
    *value = 0;
    if (address == 0x1f801800u)
        *value = log->pending && !log->consumed ? 0x20 : 0;
    else if (address == 0x1f801803u)
        *value = log->pending ? 3 : 0;
    else {
        assert(address == 0x1f801801u && log->pending && !log->consumed);
        *value = 2;
        ++log->consumed;
    }
    return 1;
}
static int cd_register_write(void *userdata, uint32_t address, uint8_t value) {
    CdRegisterLog *log = userdata;
    if (address == 0x1f801801u) {
        assert(value == 1);
        ++log->commands;
        if (log->respond && log->accept) log->pending = 1;
        return log->accept;
    }
    if (address == 0x1f801803u) {
        assert(value == 7 && log->consumed == 1);
        log->pending = 0;
        ++log->acknowledgements;
    } else if (address == 0x1f801802u) {
        assert(value == 7);
    } else assert(address == 0x1f801800u);
    return 1;
}
static int cd_register_tick(void *userdata, int32_t *tick) {
    CdRegisterLog *log = userdata;
    *tick = 0x100;
    /* Stop after submission; this fixture does not manufacture completion. */
    return ++log->ticks == 1 || log->respond;
}

static int controller_test_tick(void *userdata, int32_t *tick) {
    MusashiCdController *cd = userdata;
    /* Deterministic scheduler quantum for integration, not runtime timing proof. */
    if (!musashi_cd_controller_advance(cd, cd->cycle + 8192)) return 0;
    *tick = 0x100 + (int32_t)(cd->cycle / 564480);
    return 1;
}

typedef struct CdSpuLog { unsigned reads, writes; int reject; } CdSpuLog;
static int cd_spu_read(void *userdata, uint32_t address, uint16_t *value) {
    CdSpuLog *log = userdata;
    assert(address == 0x1f801db8u || address == 0x1f801dbau);
    ++log->reads;
    *value = 0;
    return 1;
}
static int cd_spu_write(void *userdata, uint32_t address, uint16_t value) {
    CdSpuLog *log = userdata;
    assert(value == (address == 0x1f801daau ? 0xc001 : 0x3fff));
    ++log->writes;
    return !log->reject;
}

static int callback_gpu_cw(void *userdata, uint32_t command, int32_t *result) {
    CallbackDeviceLog *log = userdata;
    log->gpu_cw_count++;
    log->gpu_cw_command = command;
    *result = 0;
    return 1;
}

static uint16_t reset_gpu_read16(void *userdata, uint32_t address) {
    ResetGpuLog *log = userdata;
    assert(address == 0x1f801074u);
    return log->mask;
}

static void reset_gpu_write16(void *userdata, uint32_t address, uint16_t value) {
    ResetGpuLog *log = userdata;
    assert(address == 0x1f801074u);
    log->mask = value;
}

static uint32_t reset_gpu_read32(void *userdata, uint32_t address) {
    (void)userdata;
    if (address == 0x1f801810u)
        return 2;
    assert(address == 0x1f8010f0u || address == 0x1f801814u);
    return 0;
}

static void reset_gpu_write32(void *userdata, uint32_t address, uint32_t value) {
    (void)userdata;
    (void)value;
    assert(address == 0x1f8010a8u || address == 0x1f8010f0u ||
           address == 0x1f801814u || address == 0x1f801810u);
}

static int cd_enter(void *userdata) {
    ((CdDeviceLog *)userdata)->enter_count++;
    return 1;
}

static int cd_exit(void *userdata) {
    ((CdDeviceLog *)userdata)->exit_count++;
    return 1;
}

static int cd_b0_5b(void *userdata, int32_t argument, int32_t *result) {
    assert(argument == 0);
    ((CdDeviceLog *)userdata)->b0_5b_count++;
    *result = 0;
    return 1;
}

static int cd_b0_4a(void *userdata, int32_t argument, int32_t *result) {
    assert(argument == 0);
    ((CdDeviceLog *)userdata)->b0_4a_count++;
    *result = 0;
    return 1;
}

static int cd_b0_4b(void *userdata, int32_t *result) {
    ((CdDeviceLog *)userdata)->b0_4b_count++;
    *result = 0;
    return 1;
}

static int cd_a0_70(void *userdata, int32_t *result) {
    ((CdDeviceLog *)userdata)->a0_70_count++;
    *result = 0;
    return 1;
}

static int event_enter(void *userdata, int32_t *result) {
    ((EventDeviceLog *)userdata)->enter_count++;
    *result = 1;
    return 1;
}

static int event_open(void *userdata, uint32_t class_word, uint32_t spec,
                      uint32_t mode, uint32_t callback, int32_t *handle) {
    static const uint32_t classes[] = {
        0xf4000001u, 0xf4000001u, 0xf4000001u, 0xf4000001u,
        0xf0000011u, 0xf0000011u, 0xf0000011u, 0xf0000011u,
    };
    static const uint32_t specs[] = {4, 0x8000, 0x100, 0x2000, 4, 0x8000, 0x100, 0x2000};
    static const uint32_t callbacks[] = {
        0x800617ccu, 0x800617e0u, 0x800617f4u, 0x80061808u,
        0x8006181cu, 0x80061830u, 0x80061844u, 0x80061858u,
    };
    EventDeviceLog *log = userdata;
    assert(log->open_count < 8 && class_word == classes[log->open_count] &&
           spec == specs[log->open_count] && mode == 0x1000u &&
           callback == callbacks[log->open_count]);
    *handle = 0x100 + (int32_t)log->open_count++;
    return 1;
}

static int event_enable(void *userdata, int32_t handle, int32_t *result) {
    EventDeviceLog *log = userdata;
    assert(handle == 0x100 + (int32_t)log->enable_count++);
    *result = 0;
    return 1;
}

static int event_b0_0b(void *userdata, int32_t handle, int32_t *result) {
    EventDeviceLog *log = userdata;
    assert(handle == 0x100 + (int32_t)log->b0_0b_count++);
    *result = 0;
    return 1;
}

static int event_exit(void *userdata, int32_t *result) {
    ((EventDeviceLog *)userdata)->exit_count++;
    *result = 0;
    return 1;
}

int main(void) {
    {
        MusashiCdController cd;
        uint8_t response = 0xff;
        musashi_cd_controller_init(&cd, 2);
        assert(musashi_cd_controller_write8(&cd, 0x1f801800u, 1));
        assert(musashi_cd_controller_write8(&cd, 0x1f801802u, 7));
        assert(musashi_cd_controller_write8(&cd, 0x1f801800u, 0));
        assert(!musashi_cd_controller_write8(&cd, 0x1f801801u, 0xff));
        assert(musashi_cd_controller_write8(&cd, 0x1f801801u, 0x0a));
        assert(!musashi_cd_controller_write8(&cd, 0x1f801801u, 1));
        assert(musashi_cd_controller_advance(&cd, 0x7ff));
        assert(!musashi_cd_controller_irq(&cd));
        assert(musashi_cd_controller_advance(&cd, 0x800));
        assert(musashi_cd_controller_irq(&cd) && cd.interrupt == 3);
        assert(musashi_cd_controller_read8(&cd, 0x1f801801u, &response));
        assert(response == 2 && cd.mode == 0x20);
        assert(!musashi_cd_controller_read8(&cd, 0x1f801801u, &response));
        assert(musashi_cd_controller_advance(&cd, 0x800 + 4100000));
        assert(cd.interrupt == 3); /* Completion waits for acknowledgement. */
        assert(musashi_cd_controller_write8(&cd, 0x1f801800u, 1));
        assert(musashi_cd_controller_write8(&cd, 0x1f801803u, 7));
        assert(!musashi_cd_controller_irq(&cd));
        assert(musashi_cd_controller_advance(&cd, 0x800 + 4100000));
        assert(cd.interrupt == 2 && musashi_cd_controller_irq(&cd));
        assert(!musashi_cd_controller_advance(&cd, 0));
        assert(musashi_cd_controller_read8(&cd, 0x1f801801u, &response));
        assert(response == 2);
        assert(musashi_cd_controller_write8(&cd, 0x1f801803u, 7));
        assert(musashi_cd_controller_write8(&cd, 0x1f801800u, 0));
        cd.muted = 1;
        assert(musashi_cd_controller_write8(&cd, 0x1f801801u, 0x0c));
        assert(musashi_cd_controller_advance(&cd, 0x1000 + 4100000));
        assert(!cd.muted && cd.interrupt == 3);
        assert(!musashi_cd_controller_read8(&cd, 0x1f801804u, &response));
        assert(musashi_cd_controller_write8(&cd, 0x1f801800u, 2));
        assert(musashi_cd_controller_write8(&cd, 0x1f801802u, 0x80));
        assert(musashi_cd_controller_write8(&cd, 0x1f801803u, 0x12));
        assert(musashi_cd_controller_write8(&cd, 0x1f801800u, 3));
        assert(musashi_cd_controller_write8(&cd, 0x1f801801u, 0x70));
        assert(musashi_cd_controller_write8(&cd, 0x1f801802u, 0x34));
        assert(musashi_cd_controller_write8(&cd, 0x1f801803u, 0));
        assert(cd.volume[0] == 0 && cd.volume[1] == 0 &&
               cd.volume[2] == 0 && cd.volume[3] == 0);
        assert(musashi_cd_controller_write8(&cd, 0x1f801803u, 0x20));
        assert(cd.volume[0] == 0x80 && cd.volume[1] == 0x12 &&
               cd.volume[2] == 0x34 && cd.volume[3] == 0x70);
        assert(cd.command == 0x0c && cd.interrupt == 3);
    }
    MusashiBootMemory memory;
    Output output;
    int32_t result = -1;
    uint16_t half;
    uint32_t word;
    memset(&memory, 0, sizeof(memory));
    memset(&output, 0, sizeof(output));
    memcpy(memory.bytes + 0x10000, "OK\n", 4);
    assert(musashi_boot_format_8005c640(&memory, 0x80010000u, 0x80010100u,
                                         sink, &output, &result));
    /* Retail only increments the return count through conversion paths; plain
     * literal bytes are emitted but return zero. */
    assert(result == 0);
    assert(output.count == 4);
    assert(!memcmp(output.bytes, "OK\r\n", 4));
    assert(memory.bytes[0x78978] == 0);

    /* The exact retail table is normally supplied by musashi_boot_load_entry.
     * This focused fixture supplies only its observed %d cell, then exercises
     * guest argument words, jump-table dispatch, unsigned division, and the
     * decomp-owned output sink through the MIPS stream. */
    memset(&memory, 0, sizeof(memory));
    memset(&output, 0, sizeof(output));
    memcpy(memory.bytes + 0x10000, "%d", 3);
    memcpy(memory.bytes + 0x74284, "0123456789abcdef", 17);
    assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'d' * 4u,
                                 0x8005c860u));
    assert(musashi_boot_write32(&memory, 0x80010100u, 42));
    assert(musashi_boot_format_8005c640(&memory, 0x80010000u, 0x80010100u,
                                         sink, &output, &result));
    assert(result == 2);
    assert(output.count == 2 && !memcmp(output.bytes, "42", 2));

    /* The caller-facing wrapper stores raw a1..a3 words in its PS1 home area
     * before jumping to the formatter; no host varargs are involved. */
    memset(&memory, 0, sizeof(memory));
    memset(&output, 0, sizeof(output));
    memcpy(memory.bytes + 0x10000, "%d", 3);
    memcpy(memory.bytes + 0x74284, "0123456789abcdef", 17);
    assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'d' * 4u,
                                 0x8005c860u));
    assert(musashi_boot_format_8005c604(&memory, 0x80010000u, 42, 0, 0,
                                         sink, &output, &result));
    assert(result == 2);
    assert(output.count == 2 && !memcmp(output.bytes, "42", 2));

    /* %s crosses the exact 8005CD18 strlen call boundary and then emits bytes
     * through the same MIPS-controlled precision/padding path. */
    memset(&memory, 0, sizeof(memory));
    memset(&output, 0, sizeof(output));
    memcpy(memory.bytes + 0x10000, "%s", 3);
    memcpy(memory.bytes + 0x10200, "HI", 3);
    assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'s' * 4u,
                                 0x8005c928u));
    assert(musashi_boot_write32(&memory, 0x80010100u, 0x80010200u));
    assert(musashi_boot_format_8005c640(&memory, 0x80010000u, 0x80010100u,
                                         sink, &output, &result));
    assert(result == 2);
    assert(output.count == 2 && !memcmp(output.bytes, "HI", 2));

    /* Mode 5 reaches the first callback after retail's checked environment
     * clear. The executor must stop at that callback, never synthesize it. */
    {
        MusashiResetGraphPrefixStop stop = {0, 0};
        unsigned index;
        memset(&memory, 0xa5, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cb84u, 0x8006cb64u));
        assert(musashi_boot_write32(&memory, 0x8006cb70u, 0x80042718u));
        result = 123;
        assert(musashi_boot_run_resetgraph_prefix(&memory, 5, sink, &output,
                                                   &result, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x80042598u && stop.target_address == 0x80042718u);
        assert(result == 123);
        for (index = 0; index < 0x80; ++index)
            assert(memory.bytes[0x72788 + index] == 0);
    }

    /* Enter through the actual 800141F0 body. Its mode-zero ResetGraph call
     * uses the retail-format data before reaching the same guest-table stop. */
    {
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        memset(&output, 0, sizeof(output));
        memcpy(memory.bytes + 0x74084, "ResetGraph:jtb=%08x,env=%08x\n", 29);
        memcpy(memory.bytes + 0x742a0, "0123456789abcdef", 17);
        assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'x' * 4u,
                                     0x8005c9ccu));
        assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'0' * 4u,
                                     0x8005c7c8u));
        assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'8' * 4u,
                                     0x8005c7d0u));
        assert(musashi_boot_write32(&memory, 0x8006cb84u, 0x8006cb64u));
        assert(musashi_boot_write32(&memory, 0x8006cb70u, 0x80042718u));
        result = -1;
        assert(musashi_boot_run_800141f0_prefix(&memory, sink, &output,
                                                 &result, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x80042598u && stop.target_address == 0x80042718u);
        assert(output.count > 16 && !memcmp(output.bytes, "ResetGraph:jtb=", 15));
    }

    /* The exact callback initializer makes its observed IRQ/DMA accesses via
     * the explicit device contract, clears its retail state span, records
     * registers, and then stops at (rather than emulating) BIOS B0:19. */
    {
        CallbackDeviceLog log;
        MusashiCallbackDevice device = {0};
        MusashiResetGraphPrefixStop stop = {0, 0};
        uint32_t value;
        memset(&memory, 0xa5, sizeof(memory));
        memset(&log, 0, sizeof(log));
        log.mask = 0xa55a;
        device.read16 = callback_read16;
        device.write16 = callback_write16;
        device.write32 = callback_write32;
        device.hook_entry_int = NULL;
        device.registration_device = NULL;
        device.a0_72 = NULL;
        device.exit_critical_section = NULL;
        device.gpu_cw = NULL;
        device.reset_device = NULL;
        device.userdata = &log;
        assert(musashi_boot_write16(&memory, 0x8006bafcu, 0));
        assert(musashi_boot_write32(&memory, 0x8006cb88u, 0x1f801070u));
        assert(musashi_boot_write32(&memory, 0x8006cb8cu, 0x1f801074u));
        assert(musashi_boot_write32(&memory, 0x8006cb90u, 0x1f8010f0u));
        assert(musashi_boot_run_callback_init_prefix(&memory, &device, &stop) ==
               MUSASHI_CALLBACK_INIT_PREFIX_BIOS_BOUNDARY);
        assert(stop.call_address == 0x800427a0u && stop.target_address == 0x8005cef8u);
        assert(log.read16_count == 1 && log.write16_count == 2 && log.write32_count == 1);
        assert(log.write16_address[0] == 0x1f801074u && log.write16_value[0] == 0);
        /* Retail clears I_MASK, reads it back, then writes that value to I_STAT. */
        assert(log.write16_address[1] == 0x1f801070u && log.write16_value[1] == 0);
        assert(log.write32_address[0] == 0x1f8010f0u &&
               log.write32_value[0] == 0x33333333u);
        assert(!memory.bytes[0x6bafc] && !memory.bytes[0x6bb30]);
        assert(musashi_boot_read32(&memory, 0x8006bb34u, &value));
        assert(value == 0x80042780u);
        assert(musashi_boot_read32(&memory, 0x8006bb38u, &value));
        assert(value == 0x8006cb14u);
    }
    {
        CallbackDeviceLog log;
        HookLog hook_log;
        MusashiCallbackDevice device = {0};
        MusashiResetGraphPrefixStop stop = {0, 0};
        uint32_t value;
        memset(&memory, 0, sizeof(memory));
        memset(&log, 0, sizeof(log));
        memset(&hook_log, 0, sizeof(hook_log));
        log.mask = 0xa55a;
        device.read16 = callback_read16;
        device.write16 = callback_write16;
        device.write32 = callback_write32;
        device.hook_entry_int = isolated_hook_entry_int;
        device.hook_entry_int_userdata = &hook_log;
        init_registration(&device, &log);
        device.a0_72 = NULL;
        device.exit_critical_section = NULL;
        device.gpu_cw = NULL;
        device.reset_device = NULL;
        device.userdata = &log;
        assert(musashi_boot_write32(&memory, 0x8006cb88u, 0x1f801070u));
        assert(musashi_boot_write32(&memory, 0x8006cb8cu, 0x1f801074u));
        assert(musashi_boot_write32(&memory, 0x8006cb90u, 0x1f8010f0u));
        assert(musashi_boot_write32(&memory, 0x8006cbbcu, 0x1f801114u));
        assert(musashi_boot_write32(&memory, 0x8006cbc0u, 0x1f8010f4u));
        assert(musashi_boot_write32(&memory, 0x8006cb84u, 0x8006cb64u));
        assert(musashi_boot_write32(&memory, 0x8006cb6cu, 0x800429dcu));
        assert(musashi_boot_run_callback_init_prefix(&memory, &device, &stop) ==
               MUSASHI_CALLBACK_INIT_PREFIX_A0_72_BOUNDARY);
        assert(hook_log.count == 1 && hook_log.context == 0x8006bb34u);
        assert(log.hook_count == 0);
        check_registered_state(&memory, &log, 0);
        assert(log.write32_count == 3 && log.write32_address[1] == 0x1f801114u &&
               log.write32_value[1] == 0x107u);
        assert(log.write32_address[2] == 0x1f8010f4u && log.write32_value[2] == 0);
        assert(stop.call_address == 0x800427ccu && stop.target_address == 0x8005ce50u);
        assert(musashi_boot_read32(&memory, 0x8006cb78u, &value));
        assert(value == 0x80042d60u);
        assert(musashi_boot_read32(&memory, 0x8006cb68u, &value));
        assert(value == 0x80042f8cu);
    }
    {
        CallbackDeviceLog log;
        MusashiCallbackDevice device = {0};
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        memset(&log, 0, sizeof(log));
        log.mask = 0xa55a;
        device.read16 = callback_read16;
        device.write16 = callback_write16;
        device.write32 = callback_write32;
        device.hook_entry_int = callback_hook_entry_int;
        init_registration(&device, &log);
        device.a0_72 = callback_a0_72;
        device.exit_critical_section = callback_exit_critical;
        device.gpu_cw = callback_gpu_cw;
        device.reset_device = NULL;
        device.userdata = &log;
        assert(musashi_boot_write32(&memory, 0x8006cb88u, 0x1f801070u));
        assert(musashi_boot_write32(&memory, 0x8006cb8cu, 0x1f801074u));
        assert(musashi_boot_write32(&memory, 0x8006cb90u, 0x1f8010f0u));
        assert(musashi_boot_write32(&memory, 0x8006cbbcu, 0x1f801114u));
        assert(musashi_boot_write32(&memory, 0x8006cbc0u, 0x1f8010f4u));
        assert(musashi_boot_write32(&memory, 0x8006cb84u, 0x8006cb64u));
        assert(musashi_boot_write32(&memory, 0x8006cb6cu, 0x800429dcu));
        assert(musashi_boot_run_callback_init_prefix(&memory, &device, &stop) ==
               MUSASHI_CALLBACK_INIT_PREFIX_RETURN_BOUNDARY);
        assert(log.a0_72_count == 1 && log.a0_72_table == 0x8006cb64u);
        assert(log.exit_critical_count == 1);
        assert(stop.call_address == 0x800427f0u && stop.target_address == UINT32_MAX);
        {
            EventDeviceLog event_log;
            MusashiEventDevice event_device;
            MusashiResetGraphPrefixStop event_stop = {0, 0};
            InputDeviceLog input_log = {0};
            MusashiInputDevice input_device = {
                &input_log, input_enter_critical, input_c0_3, input_c0_2,
                input_c0_0a, input_exit_critical
            };
            int32_t event_result = -1;
            uint32_t event_slot;
            memset(&event_log, 0, sizeof(event_log));
            event_device.userdata = &event_log;
            event_device.enter_critical_section = event_enter;
            event_device.open_event = event_open;
            event_device.enable_event = event_enable;
            event_device.b0_0b = event_b0_0b;
            event_device.exit_critical_section = event_exit;
            assert(musashi_boot_run_8005fcb8_event_prefix(
                       &memory, &event_device, &event_result, &event_stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
            assert(event_stop.call_address == 0 &&
                   event_stop.target_address == UINT32_MAX);
            assert(musashi_boot_read32(&memory, 0x8006cbb4u, &event_slot));
            assert(event_slot == 0x800616d0u);
            assert(event_log.open_count == 8 && event_log.enable_count == 8 &&
                   event_log.b0_0b_count == 8);

            /* Use the slot and idle index installed by the actual startup
             * bodies. Direct handler invocation is not scheduled IRQ delivery. */
            assert(musashi_boot_read32(&memory, 0x80072a34u, &event_slot));
            assert(event_slot == UINT32_MAX);
            assert(musashi_boot_read32(&memory, 0x8006cbb8u, &event_slot));
            assert(event_slot == 0);
            assert(musashi_boot_call_80042ce8(
                &memory, musashi_boot_execute_vblank_callback, NULL));
            assert(musashi_boot_read32(&memory, 0x8006cbb8u, &event_slot));
            assert(event_slot == 1);

            /* Retail's static callback table supplies the wrapper's +0x0C
             * initializer pointer; 80042718 returns early after prior setup. */
            assert(musashi_boot_write32(&memory, 0x8006cb70u, 0x80042718u));
            memset(memory.bytes + 0x78d98, 0xa5, 0x98);
            assert(musashi_boot_write32(&memory, 0x800ae610u, UINT32_MAX));
            assert(musashi_boot_run_80018918_prefix(&memory, &event_stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
            assert(event_stop.call_address == 0x8005d100u &&
                   event_stop.target_address == 0x8005d8b4u);
            assert(musashi_boot_read32(&memory, 0x80078d9cu, &event_slot));
            assert(event_slot == 8u);
            assert(musashi_boot_read32(&memory, 0x80078de8u, &event_slot));
            assert(event_slot == 8u);
            assert(musashi_boot_read32(&memory, 0x800ae610u, &event_slot));
            assert(event_slot == 0);
            assert(musashi_boot_read32(&memory, 0x80072994u, &event_slot));
            assert(event_slot == 1u);

            assert(musashi_boot_write32(&memory, 0x800729bcu, 0x800789a0u));
            assert(musashi_boot_write32(&memory, 0x800789a4u, 0x12345678u));
            assert(musashi_boot_run_80018918_input_prefix(
                       &memory, &input_device, &event_stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
            assert(event_stop.call_address == 0 &&
                   event_stop.target_address == UINT32_MAX);
            assert(input_log.enter_count == 1 && input_log.c0_3_count == 1 &&
                   input_log.c0_2_count == 1 && input_log.c0_0a_count == 1 &&
                   input_log.exit_count == 1);
            assert(musashi_boot_read32(&memory, 0x80072994u, &event_slot));
            assert(event_slot == 1u);
            assert(musashi_boot_read32(&memory, 0x800789a0u, &event_slot));
            assert(event_slot == 0xfffffffeu);
            assert(musashi_boot_read32(&memory, 0x800789a4u, &event_slot));
            assert(event_slot == 0x12345679u);

            assert(musashi_boot_write32(&memory, 0x80072994u, UINT32_MAX));
            assert(musashi_boot_write32(&memory, 0x800729a8u, UINT32_MAX));
            memset(memory.bytes + 0x78a48, 0xa5, 0x1e0);
            assert(musashi_boot_run_8005f0c8_prefix(&memory, &event_stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
            assert(event_stop.call_address == 0 &&
                   event_stop.target_address == UINT32_MAX);
            assert(musashi_boot_read32(&memory, 0x80072994u, &event_slot));
            assert(event_slot == 1);
            assert(musashi_boot_read32(&memory, 0x800729a8u, &event_slot));
            assert(event_slot == 0);
            assert(musashi_boot_read32(&memory, 0x80072974u, &event_slot));
            assert(event_slot == 0x8005f75cu);
            assert(musashi_boot_read32(&memory, 0x80072978u, &event_slot));
            assert(event_slot == 0x8005fb70u);
            assert(musashi_boot_read32(&memory, 0x8007297cu, &event_slot));
            assert(event_slot == 0x8005f830u);
            assert(musashi_boot_read32(&memory, 0x80072980u, &event_slot));
            assert(event_slot == 0x8005f384u);
            assert(musashi_boot_read32(&memory, 0x80072990u, &event_slot));
            assert(event_slot == 0x80078a48u);
            assert(!memory.bytes[0x78a48] && !memory.bytes[0x78c27]);
            assert(musashi_boot_read32(&memory, 0x80078a78u, &event_slot));
            assert(event_slot == 0x80078da0u);
            assert(musashi_boot_read32(&memory, 0x80078b68u, &event_slot));
            assert(event_slot == 0x80078decu);
            assert(memory.bytes[0x78da0] == 0xff && !memory.bytes[0x78da1]);
            assert(memory.bytes[0x78dec] == 0xff && !memory.bytes[0x78ded]);
            assert(memory.bytes[0x78aa5] == 0xff);
            assert(memory.bytes[0x78aa9] == 0xff);
            assert(memory.bytes[0x78b95] == 0xff);
            assert(memory.bytes[0x78b99] == 0xff);
            assert(musashi_boot_read32(&memory, 0x80078988u, &event_slot));
            assert(event_slot == 0);
            assert(musashi_boot_read32(&memory, 0x8007898cu, &event_slot));
            assert(event_slot == 0x8005d734u);
            assert(musashi_boot_read32(&memory, 0x80078990u, &event_slot));
            assert(event_slot == 0x8005d6ccu);
            assert(musashi_boot_read32(&memory, 0x80078994u, &event_slot));
            assert(event_slot == 0);
            assert(musashi_boot_read32(&memory, 0x80072994u, &event_slot));
            assert(event_slot == 1);
        }
    }
    {
        CallbackDeviceLog log;
        MusashiCallbackDevice device = {0};
#ifdef MUSASHI_WITH_PSYCROSS
        SchedulerHardware scheduler_hardware = {0, 9, 0};
        MusashiIrqDispatchDevice scheduler_device = {
            &scheduler_hardware, scheduler_read16, scheduler_write16,
            scheduler_execute, NULL, NULL
        };
        MusashiPsyCrossIrqScheduler scheduler = {
            &memory, &scheduler_device, scheduler_raise_vblank,
            &scheduler_hardware, 0, 0, 0, 0, 0
        };
        fixture_irq_cpu_attach(&scheduler);
#endif
        ResetGpuLog reset_log = {.mask = 0xa55a};
        MusashiGpuDevice reset_device = {
            reset_gpu_read32, reset_gpu_write32, &reset_log,
            reset_gpu_read16, reset_gpu_write16
        };
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        memset(&output, 0, sizeof(output));
        memset(&log, 0, sizeof(log));
#ifdef MUSASHI_WITH_PSYCROSS
        assert(g_intrMutex == NULL);
        g_intrMutex = SDL_CreateMutex();
        assert(g_intrMutex != NULL);
#endif
        log.mask = 0xa55a;
        device.read16 = callback_read16;
        device.write16 = callback_write16;
        device.write32 = callback_write32;
#ifdef MUSASHI_WITH_PSYCROSS
        device.hook_entry_int = musashi_psycross_irq_scheduler_hook_entry_int;
        device.hook_entry_int_userdata = &scheduler;
#else
        device.hook_entry_int = callback_hook_entry_int;
#endif
        init_registration(&device, &log);
        device.a0_72 = callback_a0_72;
        device.exit_critical_section = callback_exit_critical;
        device.gpu_cw = callback_gpu_cw;
        device.reset_device = &reset_device;
        device.userdata = &log;
        memcpy(memory.bytes + 0x74084, "ResetGraph:jtb=%08x,env=%08x\n", 29);
        memcpy(memory.bytes + 0x742a0, "0123456789abcdef", 17);
        assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'x' * 4u,
                                     0x8005c9ccu));
        assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'0' * 4u,
                                     0x8005c7c8u));
        assert(musashi_boot_write32(&memory, 0x800742b4u + (uint32_t)'8' * 4u,
                                     0x8005c7d0u));
        assert(musashi_boot_write32(&memory, 0x8006cb88u, 0x1f801070u));
        assert(musashi_boot_write32(&memory, 0x8006cb8cu, 0x1f801074u));
        assert(musashi_boot_write32(&memory, 0x8006cb90u, 0x1f8010f0u));
        assert(musashi_boot_write32(&memory, 0x8006cbbcu, 0x1f801114u));
        assert(musashi_boot_write32(&memory, 0x8006cbc0u, 0x1f8010f4u));
        assert(musashi_boot_write32(&memory, 0x80072858u, 0x1f801810u));
        assert(musashi_boot_write32(&memory, 0x80072868u, 0x1f8010a8u));
        assert(musashi_boot_write32(&memory, 0x80072878u, 0x1f8010f0u));
        assert(musashi_boot_write32(&memory, 0x8007285cu, 0x1f801814u));
        assert(musashi_boot_write32(&memory, 0x80072780u, 0x80072740u));
        assert(musashi_boot_write32(&memory, 0x80072750u, 0x8005b684u));
        assert(musashi_boot_write32(&memory, 0x8006cb84u, 0x8006cb64u));
        assert(musashi_boot_write32(&memory, 0x8006cb70u, 0x80042718u));
        assert(musashi_boot_write32(&memory, 0x8006cb6cu, 0x800429dcu));
        memory.bytes[0x747b9] = 0xff;
        memory.bytes[0x747b8] = 0xff;
        memory.bytes[0x62bbc] = 0xff;
        memory.bytes[0x62bbd] = 0xff;
        memory.bytes[0x78d98 + 0x4a] = 0xff;
        memory.bytes[0x78d98 + 0x4b] = 0xff;
        memory.bytes[0x78d98 + 0x4c + 0x4a] = 0xff;
        memory.bytes[0x78d98 + 0x4c + 0x4b] = 0xff;
        assert(musashi_boot_write32(&memory, 0x800ae7ecu, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x800a654cu, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x800747f8u, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x800a2b70u, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x800ba0fcu, UINT32_MAX));
        memory.bytes[0xba1e4] = 0xff;
        memory.bytes[0xba1e5] = 0xff;
        memory.bytes[0xae649] = 0xff;
        {
            CdDeviceLog cd_log = {0};
            EventDeviceLog event_log = {0};
            InputDeviceLog input_log = {0};
            MusashiCdDevice cd_device = {
                &cd_log, cd_enter, cd_exit, cd_b0_5b, cd_b0_4a, cd_b0_4b, cd_a0_70
            };
            MusashiEventDevice event_device = {
                &event_log, event_enter, event_open, event_enable, event_b0_0b, event_exit
            };
            MusashiInputDevice input_device = {
                &input_log, input_enter_critical, input_c0_3, input_c0_2, input_c0_0a,
                input_exit_critical
            };
            MusashiStartupPrefixDevice startup_device = {
                &device, &cd_device, &event_device, &input_device
            };
            assert(musashi_boot_write32(&memory, 0x80078c7cu, UINT32_MAX));
            assert(musashi_boot_write32(&memory, 0x80078cc0u, UINT32_MAX));
            assert(musashi_boot_write32(&memory, 0x800729bcu, 0x800789a0u));
            assert(musashi_boot_write32(&memory, 0x800789a4u, 0x12345678u));
            result = -1;
            /* This composes only at the genuine post-entry call boundary.  The
             * CD-register pointers are retail guest state consumed by the
             * successful 80043450 alternate leaf; they are not host bindings. */
            {
                RetryDeviceLog retry_log = {0, 0};
                MusashiStartupRetryDevice retry_device = {&retry_log, retry_probe};
                assert(musashi_boot_write32(&memory, 0x8006cf60u, 0x80070000u));
                assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
                assert(musashi_boot_write32(&memory, 0x8006cf50u, 0x80071001u));
                assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071002u));
                assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071003u));
                assert(musashi_boot_run_post_entry_dispatch_prefix(
                           &memory, &startup_device, &retry_device, sink, &output,
                           &result, &stop) == MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
                assert(retry_log.count == 1 && stop.target_address == UINT32_MAX);
                assert(musashi_boot_read32(&memory, 0x8006cc84u, &word) &&
                       word == 0x80043398u);
                assert(musashi_boot_read32(&memory, 0x8006cc88u, &word) &&
                       word == 0x800433c0u);
                assert(musashi_boot_read32(&memory, 0x8006cf9cu, &word) &&
                       word == 0x800433e8u);
            }
            assert(cd_log.enter_count == 2 && cd_log.exit_count == 2 &&
                   cd_log.b0_5b_count == 2 && cd_log.b0_4a_count == 1 &&
                   cd_log.b0_4b_count == 1 && cd_log.a0_70_count == 1);
            assert(event_log.open_count == 8 && event_log.enable_count == 8 &&
                   event_log.b0_0b_count == 8);
            assert(input_log.enter_count == 1 && input_log.c0_3_count == 1 &&
                   input_log.c0_2_count == 1 && input_log.c0_0a_count == 1 &&
                   input_log.exit_count == 1);
#ifdef MUSASHI_WITH_PSYCROSS
            assert(scheduler.installed && scheduler.context_address == 0x8006bb34u);
            assert(scheduler.deliveries == 0 && scheduler_hardware.raised == 0);
            assert(musashi_psycross_irq_scheduler_uninstall(&scheduler));
            fixture_irq_cpu_close(&scheduler);
            SDL_DestroyMutex(g_intrMutex);
            g_intrMutex = NULL;
#else
            assert(log.hook_count == 1 && log.hook_context == 0x8006bb34u);
#endif
        }
        assert(memory.bytes[0x747b9] == 0 && memory.bytes[0x747b8] == 0);
        assert(memory.bytes[0x62bbc] == 0x40 && memory.bytes[0x62bbd] == 0);
        assert(memory.bytes[0x78d98 + 0x4a] == 0 && memory.bytes[0x78d98 + 0x4b] == 0);
        assert(memory.bytes[0x78d98 + 0x4c + 0x4a] == 0);
        assert(memory.bytes[0x78d98 + 0x4c + 0x4b] == 0);
        assert(!memory.bytes[0xae7ec] && !memory.bytes[0xa654c] &&
               !memory.bytes[0x747f8] && !memory.bytes[0xa2b70] &&
               !memory.bytes[0xba0fc]);
        assert(memory.bytes[0xba1e4] == 7 && memory.bytes[0xba1e5] == 7);
        assert(memory.bytes[0xae649] == 0);
        assert(log.gpu_cw_count == 1 && log.gpu_cw_command == 0x00072740u);
        assert(output.count > 16 && !memcmp(output.bytes, "ResetGraph:jtb=", 15));
    }
    {
        CdDeviceLog log;
        MusashiCdDevice device;
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        memset(&log, 0, sizeof(log));
        device.userdata = &log;
        device.enter_critical_section = cd_enter;
        device.exit_critical_section = cd_exit;
        device.b0_5b = cd_b0_5b;
        device.b0_4a = cd_b0_4a;
        device.b0_4b = cd_b0_4b;
        device.a0_70 = cd_a0_70;
        assert(musashi_boot_write32(&memory, 0x80078c7cu, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x80078cc0u, UINT32_MAX));
        result = -1;
        assert(musashi_boot_run_8005fc68_prefix(&memory, &device, &result, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.call_address == 0 && stop.target_address == UINT32_MAX && result == 0);
        assert(!memory.bytes[0x78c7c] && !memory.bytes[0x78cc0]);
        assert(log.enter_count == 2 && log.exit_count == 2 && log.b0_5b_count == 2 &&
               log.b0_4a_count == 1 && log.b0_4b_count == 1 && log.a0_70_count == 1);
    }
    {
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x80072a34u, 0));
        assert(musashi_boot_write32(&memory, 0x80078c80u, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x80078c84u, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x80078c88u, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x80078c90u, 0));
        assert(musashi_boot_run_8005fcb8_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x8005fcf0u && stop.target_address == 0x8006189cu);
        assert(memory.bytes[0x72a34] == 0xff && memory.bytes[0x72a35] == 0xff &&
               memory.bytes[0x72a36] == 0xff && memory.bytes[0x72a37] == 0xff);
        assert(!memory.bytes[0x78c80] && !memory.bytes[0x78c84] &&
               !memory.bytes[0x78c88] && memory.bytes[0x78c90] == 0xff);
    }
    {
        EventDeviceLog log;
        MusashiEventDevice device;
        memset(&memory, 0, sizeof(memory));
        memset(&log, 0, sizeof(log));
        device.userdata = &log;
        device.enter_critical_section = event_enter;
        device.open_event = event_open;
        device.enable_event = event_enable;
        device.b0_0b = event_b0_0b;
        device.exit_critical_section = event_exit;
        result = -1;
        assert(musashi_boot_call_8006189c(&memory, &device, &result));
        assert(result == 0 && log.enter_count == 1 && log.open_count == 8 &&
               log.enable_count == 8 && log.b0_0b_count == 8 && log.exit_count == 1);
        assert(memory.bytes[0x78c3c] == 0 && memory.bytes[0x78c3d] == 1 &&
               memory.bytes[0x78c58] == 7 && memory.bytes[0x78c59] == 1);
        assert(!memory.bytes[0x78c5c] && !memory.bytes[0x78c60] &&
               !memory.bytes[0x78c64] && !memory.bytes[0x78c68] &&
               !memory.bytes[0x78c6c] && !memory.bytes[0x78c70] &&
               !memory.bytes[0x78c74] && !memory.bytes[0x78c78]);
    }
    {
        EventDeviceLog log;
        MusashiEventDevice device;
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        memset(&log, 0, sizeof(log));
        device.userdata = &log;
        device.enter_critical_section = event_enter;
        device.open_event = event_open;
        device.enable_event = event_enable;
        device.b0_0b = event_b0_0b;
        device.exit_critical_section = event_exit;
        /* 80042644 dispatches through the table address loaded from D_8006CB84
         * and the callback-initializer-owned +0x14 slot. */
        assert(musashi_boot_write32(&memory, 0x8006cb84u, 0x8006cb64u));
        assert(musashi_boot_write32(&memory, 0x8006cb78u, 0x80042d60u));
        result = -1;
        assert(musashi_boot_run_8005fcb8_event_prefix(&memory, &device, &result,
                                                        &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.call_address == 0 && stop.target_address == UINT32_MAX);
        assert(musashi_boot_read32(&memory, 0x8006cbb4u, &result));
        assert((uint32_t)result == 0x800616d0u);
        assert(log.open_count == 8 && log.enable_count == 8 && log.b0_0b_count == 8);
    }
    {
        InputDeviceLog log = {0};
        MusashiInputDevice device = {
            &log, input_enter_critical, input_c0_3, input_c0_2, input_c0_0a,
            input_exit_critical
        };
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x80072994u, UINT32_MAX));
        assert(musashi_boot_write32(&memory, 0x80072960u, 0x8005f228u));
        assert(musashi_boot_write32(&memory, 0x80072990u, 0x80078a48u));
        assert(musashi_boot_write32(&memory, 0x800729bcu, 0x800789a0u));
        assert(musashi_boot_write32(&memory, 0x800789a4u, 0x12345678u));
        assert(musashi_boot_run_8005d8b4_prefix(&memory, &device, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(log.enter_count == 1 && log.c0_3_count == 1 && log.c0_2_count == 1 &&
               log.c0_0a_count == 1 && log.exit_count == 1);
        assert(stop.call_address == 0 && stop.target_address == UINT32_MAX);
        assert(musashi_boot_read32(&memory, 0x80072994u, &result) && result == 1);
        assert(musashi_boot_read32(&memory, 0x800789a0u, &result) &&
               (uint32_t)result == 0xfffffffeu);
        assert(musashi_boot_read32(&memory, 0x800789a4u, &result) &&
               (uint32_t)result == 0x12345679u);
        assert(musashi_boot_read32(&memory, 0x80078998u, &result) && result == 0);
        assert(musashi_boot_read32(&memory, 0x8007899cu, &result) && result == 0);
    }
    {
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_run_post_entry_prefix(&memory, NULL, sink, NULL,
                                                  &result, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
    }
    {
        RetryDeviceLog log = {1, 0};
        MusashiStartupRetryDevice device = {&log, retry_probe};
        MusashiResetGraphPrefixStop stop = {0, 0};
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006ba94u, 0x80070000u));
        assert(musashi_boot_write32(&memory, 0x8006ba98u, 0x80070004u));
        assert(musashi_boot_write32(&memory, 0x8006ba9cu, 0x80070008u));
        assert(musashi_boot_write32(&memory, 0x80070000u, 0x11111111u));
        assert(musashi_boot_write32(&memory, 0x80070004u, 0x00020003u));
        assert(musashi_boot_write32(&memory, 0x80070008u, 0x00010001u));
        assert(musashi_boot_write32(&memory, 0x8006cbb8u, 0x12345678u));
        assert(musashi_boot_run_8004239c_minus_one(&memory, &result, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(result == 0x12345678 && stop.target_address == UINT32_MAX);
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006ba94u, 0x80070000u));
        assert(musashi_boot_write32(&memory, 0x8006ba98u, 0x80070004u));
        assert(musashi_boot_write32(&memory, 0x8006ba9cu, 0x80070008u));
        assert(musashi_boot_write32(&memory, 0x80070000u, 0x11111111u));
        assert(musashi_boot_write32(&memory, 0x80070004u, 0x00020003u));
        assert(musashi_boot_write32(&memory, 0x80070008u, 0x00010001u));
        assert(musashi_boot_write32(&memory, 0x8006cbb8u, 0x100u));
        assert(musashi_boot_run_80044124_first_poll(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x80044190u && stop.target_address == 0x8004239cu);
        assert(musashi_boot_read32(&memory, 0x801fff40u, &word) &&
               word == 0x80044158u);
        assert(musashi_boot_read32(&memory, 0x800763f0u, &word) && word == 0x4c0u);
        assert(musashi_boot_read32(&memory, 0x800763f4u, &word) && word == 0);
        assert(musashi_boot_read32(&memory, 0x800763f8u, &word) && word == 0x800737e0u);
        assert(musashi_boot_write16(&memory, 0x8006bafeu, 0xa55au));
        assert(musashi_boot_call_800426d4(&memory, &half) && half == 0xa55au);
        {
            TickDeviceLog tick_log = {0x4c1, 0};
            MusashiTickDevice tick_device = {&tick_log, refresh_tick};
            assert(musashi_boot_run_80044124_poll_with_tick(
                       &memory, &tick_device, &stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
            assert(tick_log.count == 1 && stop.call_address == 0x800441d0u &&
                   stop.target_address == 0x800441d8u);
            assert(musashi_boot_write32(&memory, 0x8006cc90u, 1));
            assert(musashi_boot_write32(&memory, 0x8006cbb8u, 0x100u));
            assert(musashi_boot_run_80044670_with_tick(
                       &memory, 1, 0, &tick_device, &stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
            assert(tick_log.count == 2 && stop.call_address == 0x800441d0u &&
                   stop.target_address == 0x800441d8u);
            assert(musashi_boot_read32(&memory, 0x801fff34u, &word) &&
                   word == 0x8004473cu);
        }
        assert(musashi_boot_run_80044198_timeout_prefix(&memory, 0x4c1, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x800441d0u && stop.target_address == 0x800441d8u);
        assert(musashi_boot_run_80044198_timeout_prefix(&memory, 0, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x800441d0u && stop.target_address == 0x8004424cu);
        assert(musashi_boot_write32(&memory, 0x800763f4u, 0x003c0000u));
        assert(musashi_boot_run_80044198_timeout_prefix(&memory, 0, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x800441d0u && stop.target_address == 0x8004424cu);
        assert(musashi_boot_write32(&memory, 0x800763f4u, 0x003c0001u));
        assert(musashi_boot_run_80044198_timeout_prefix(&memory, 0, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.target_address == 0x800441d8u);
        /* A fresh, unexpired poll reaches the worker on its first iteration. */
        {
            TickDeviceLog tick_log = {0x100, 0};
            MusashiTickDevice tick_device = {&tick_log, refresh_tick};
            assert(musashi_boot_write32(&memory, 0x8006cbb8u, 0x100u));
            assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
            assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
            memory.bytes[0x71002] = 0;
            memory.bytes[0x6cf64] = 2;
            assert(musashi_boot_run_80044124_poll_with_tick(
                       &memory, &tick_device, &stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
            assert(tick_log.count == 1 && stop.target_address == UINT32_MAX);
            assert(musashi_boot_read32(&memory, 0x801fff40u, &word) &&
                   word == 0x80044288u);
            /* An actual MMIO command address must fail at the device write,
             * after returning from the poll, never alias into guest RAM. */
            assert(musashi_boot_write32(&memory, 0x8006cf50u, 0x1f801801u));
            assert(musashi_boot_run_80044670_with_tick(
                       &memory, 1, 0, &tick_device, &stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
            assert(stop.target_address == 0x8004482cu);
            assert(memory.bytes[0x6cca5] == 1);
            {
                CdRegisterLog registers = {0, 0, 0};
                MusashiCdRegisterDevice cd = {
                    .userdata=&registers, .read8=cd_register_read, .write8=cd_register_write
                };
                MusashiTickDevice clock = {&registers, cd_register_tick};
                assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x1f801800u));
                assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x1f801803u));
                memory.bytes[0x6cf64] = 2;
                assert(musashi_boot_run_80044670_with_device(
                           &memory, 1, 0, &clock, &cd, &stop) ==
                       MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
                assert(registers.commands == 1 && registers.ticks == 1 &&
                       stop.target_address == 0x8004482cu);
                registers.accept = 1;
                registers.commands = registers.ticks = 0;
                memory.bytes[0x6cf64] = 2;
                assert(musashi_boot_run_80044670_with_device(
                           &memory, 1, 0, &clock, &cd, &stop) ==
                       MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
                assert(registers.commands == 1 && registers.ticks == 2);
                registers.commands = registers.ticks = 0;
                memory.bytes[0x6cf64] = 2;
                assert(musashi_boot_write32(&memory, 0x8006cf50u, 0x1f801804u));
                assert(musashi_boot_run_80044670_with_device(
                           &memory, 1, 0, &clock, &cd, &stop) ==
                       MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
                assert(registers.commands == 0 && registers.ticks == 1);
                registers.commands = registers.ticks = 0;
                registers.respond = 1;
                memory.bytes[0x6cf64] = 2;
                assert(musashi_boot_write32(&memory, 0x8006cf50u, 0x1f801801u));
                assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x1f801802u));
                assert(musashi_boot_write32(&memory, 0x800737d4u, 0x80043e08u));
                assert(musashi_boot_run_80044670_with_device(
                           &memory, 1, 0, &clock, &cd, &stop) ==
                       MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
                assert(registers.commands == 1 && registers.consumed == 1 &&
                       registers.acknowledgements == 1 && !registers.pending);
                assert(memory.bytes[0x6cf64] == 2 && memory.bytes[0x763d8] == 2);
                {
                    MusashiCdController controller;
                    MusashiCdRegisterDevice backend = {
                        .userdata=&controller, .read8=musashi_cd_controller_read8,
                        .write8=musashi_cd_controller_write8
                    };
                    MusashiTickDevice scheduler = {&controller, controller_test_tick};
                    musashi_cd_controller_init(&controller, 2);
                    assert(musashi_boot_write32(&memory, 0x8006cd74u, 1));
                    assert(musashi_boot_write32(&memory, 0x800737d0u, 0x80043f08u));
                    assert(musashi_boot_run_80044670_with_device(
                               &memory, 1, 0, &scheduler, &backend, &stop) ==
                           MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
                    assert(controller.command == 1 && !controller.interrupt &&
                           !controller.response_ready && controller.cycle > 0);
                    assert(musashi_boot_run_80044670_with_device(
                               &memory, 0x0a, 0, &scheduler, &backend, &stop) ==
                           MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
                    assert(controller.phase == 2 && memory.bytes[0x6cf64] == 3);
                    assert(musashi_boot_run_80044670_with_device(
                               &memory, 0x0c, 0, &scheduler, &backend, &stop) ==
                           MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
                    assert(controller.command == 0x0c && !controller.phase &&
                           !controller.interrupt && !controller.response_ready);
                    assert(memory.bytes[0x6cf64] == 2 && memory.bytes[0x763d8] == 2);
                    {
                        CdSpuLog sound = {0};
                        MusashiCdSpuDevice spu = {&sound, cd_spu_read, cd_spu_write};
                        assert(musashi_boot_write32(&memory, 0x8006cf60u, 0x1f801c00u));
                        assert(musashi_boot_run_80044bf4_with_device(
                                   &memory, &backend, &spu, &stop) ==
                               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
                        assert(sound.reads == 2 && sound.writes == 5);
                        assert(controller.volume[0] == 0x80 && controller.volume[1] == 0 &&
                               controller.volume[2] == 0 && controller.volume[3] == 0x80);
                        sound.reject = 1;
                        assert(musashi_boot_run_80044bf4_with_device(
                                   &memory, &backend, &spu, &stop) ==
                               MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT);
                        assert(sound.writes == 6 && stop.target_address == 0x80044c20u);
                    }
                }
            }
        }
        /* This fixture supplies the retail-owned message/table cells solely to
         * prove the live 80044124 frame reaches 80044250 after its diagnostic.
         * It is not a loaded retail image or a start-menu assertion. */
        memset(&memory, 0, sizeof(memory));
        memset(&output, 0, sizeof(output));
        memset(memory.bytes + 0x728a9, 1, 0x100);
        memcpy(memory.bytes + 0x73758, "timeout\n", 9);
        memcpy(memory.bytes + 0x73768, "retry\n", 7);
        memcpy(memory.bytes + 0x74498, "fallback", 9);
        assert(musashi_boot_write32(&memory, 0x8006ba94u, 0x80070000u));
        assert(musashi_boot_write32(&memory, 0x8006ba98u, 0x80070004u));
        assert(musashi_boot_write32(&memory, 0x8006ba9cu, 0x80070008u));
        assert(musashi_boot_write32(&memory, 0x8006cbb8u, 0x100u));
        assert(musashi_boot_write32(&memory, 0x8006cd2cu, 7u));
        assert(musashi_boot_write32(&memory, 0x8006ccacu, 9u));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x8006cf5cu, 0x80071004u));
        memory.bytes[0x6cf64] = 1;
        memory.bytes[0x6cf65] = 0;
        memory.bytes[0x6cca5] = 0;
        {
            TickDeviceLog tick_log = {0x4c1, 0};
            MusashiTickDevice tick_device = {&tick_log, refresh_tick};
            assert(musashi_boot_run_80044124_poll_with_tick_console(
                       &memory, &tick_device, sink, &output, &stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
            assert(tick_log.count == 1 && stop.call_address == 0 &&
                   stop.target_address == UINT32_MAX);
        }
        assert(output.count == 16 && !memcmp(output.bytes, "timeout\r\nretry\r\n", 16));
        assert(memory.bytes[0x71000] == 0 && memory.bytes[0x6cf64] == 2);
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x8006cf5cu, 0x80071004u));
        assert(musashi_boot_run_80044b14_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && memory.bytes[0x71000] == 0 &&
               memory.bytes[0x71002] == 0 && memory.bytes[0x6cf64] == 2 &&
               memory.bytes[0x6cf65] == 0 && memory.bytes[0x6cf66] == 0);
        assert(musashi_boot_read32(&memory, 0x80071004u, &word) && word == 0x1325u);
        assert(musashi_boot_run_800434bc_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX);
        /* The retail-observed CD-status worker takes this no-command return
         * without crossing its formatter or jump-table boundaries. */
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        memory.bytes[0x71002] = 0;
        assert(musashi_boot_run_80043b9c_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && memory.bytes[0x71000] == 1);
        /* Retail jtbl_800737CC[0] is 80043F54. This state-1 route returns
         * after publishing the worker record, without a formatter boundary. */
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x800737ccu, 0x80043f54u));
        memory.bytes[0x71002] = 1;
        assert(musashi_boot_run_80043b9c_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && memory.bytes[0x71000] == 0 &&
               memory.bytes[0x71002] == 0 && memory.bytes[0x6cf65] == 1);
        /* Retail jtbl_800737CC[1] is 80043F08. */
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x800737ccu, 0x80043f54u));
        assert(musashi_boot_write32(&memory, 0x800737d0u, 0x80043f08u));
        memory.bytes[0x71002] = 2;
        assert(musashi_boot_run_80043b9c_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && memory.bytes[0x71000] == 1 &&
               memory.bytes[0x71001] == 7 && memory.bytes[0x71002] == 7 &&
               memory.bytes[0x6cf64] == 2);
        /* Retail jtbl_800737CC[2] is 80043E08. Its zero CD4C cell follows
         * the state-two return branch without an external call. */
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x800737ccu, 0x80043f54u));
        assert(musashi_boot_write32(&memory, 0x800737d0u, 0x80043f08u));
        assert(musashi_boot_write32(&memory, 0x800737d4u, 0x80043e08u));
        memory.bytes[0x71002] = 3;
        assert(musashi_boot_run_80043b9c_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && memory.bytes[0x6cf64] == 2);
        /* Retail jtbl_800737CC[3] is 80043FD8. */
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x800737ccu, 0x80043f54u));
        assert(musashi_boot_write32(&memory, 0x800737d0u, 0x80043f08u));
        assert(musashi_boot_write32(&memory, 0x800737d4u, 0x80043e08u));
        assert(musashi_boot_write32(&memory, 0x800737d8u, 0x80043fd8u));
        memory.bytes[0x71002] = 4;
        assert(musashi_boot_run_80043b9c_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && memory.bytes[0x6cf66] == 4 &&
               memory.bytes[0x6cf65] == 4);
        /* Retail jtbl_800737CC[4] is 8004405C. */
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x800737ccu, 0x80043f54u));
        assert(musashi_boot_write32(&memory, 0x800737d0u, 0x80043f08u));
        assert(musashi_boot_write32(&memory, 0x800737d4u, 0x80043e08u));
        assert(musashi_boot_write32(&memory, 0x800737d8u, 0x80043fd8u));
        assert(musashi_boot_write32(&memory, 0x800737dcu, 0x8004405cu));
        memory.bytes[0x71002] = 5;
        assert(musashi_boot_run_80043b9c_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && memory.bytes[0x6cf65] == 5 &&
               memory.bytes[0x6cf64] == 5);
        /* State 6 takes the default route through retail 8005CD48 and
         * 8005C604; the fixture supplies only the strings/classification. */
        memset(&memory, 0, sizeof(memory));
        memset(&output, 0, sizeof(output));
        memset(memory.bytes + 0x728a9, 1, 0x100);
        memcpy(memory.bytes + 0x737ac, "state\n", 7);
        memcpy(memory.bytes + 0x737c0, "route\n", 7);
        memcpy(memory.bytes + 0x74498, "fallback", 9);
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        memory.bytes[0x71002] = 6;
        assert(musashi_boot_run_80043b9c_console_prefix(
                   &memory, sink, &output, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && output.count == 14 &&
               !memcmp(output.bytes, "state\r\nroute\r\n", 14));
        /* A positive CC90 takes both worker diagnostics before the state-five
         * table return. Literal fixture strings isolate the actual call path. */
        memset(&memory, 0, sizeof(memory));
        memset(&output, 0, sizeof(output));
        memset(memory.bytes + 0x728a9, 1, 0x100);
        memcpy(memory.bytes + 0x73784, "first\n", 7);
        memcpy(memory.bytes + 0x73790, "second\n", 8);
        memcpy(memory.bytes + 0x74498, "fallback", 9);
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x8006cc90u, 1));
        assert(musashi_boot_write32(&memory, 0x800737dcu, 0x8004405cu));
        memory.bytes[0x71002] = 5;
        assert(musashi_boot_run_80043b9c_console_prefix(
                   &memory, sink, &output, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(stop.target_address == UINT32_MAX && output.count == 15 &&
               !memcmp(output.bytes, "first\r\nsecond\r\n", 15));
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cc90u, 1));
        assert(musashi_boot_run_80044670_prefix(&memory, 1, 0, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x80044734u && stop.target_address == 0x80044124u);
        assert(musashi_boot_write32(&memory, 0x8006cc90u, 2));
        assert(musashi_boot_write32(&memory, 0x8006ccb0u, 0x8006cf68u));
        assert(musashi_boot_run_80044670_prefix(&memory, 1, 0, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x800446ccu && stop.target_address == 0x8005c604u);
        /* The 80044DBC initializer publishes its setup bytes and issues the
         * command call with (1,0,0,0); the busy flag reads 0x80 (no low
         * three bits) so the poll loop is skipped. Prefills prove every
         * store instead of matching zeroes. */
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071010u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071030u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071020u));
        assert(musashi_boot_write32(&memory, 0x8006cf5cu, 0x80071040u));
        memory.bytes[0x71000] = 0x00;
        memory.bytes[0x71010] = 0x33;
        memory.bytes[0x71020] = 0x80;
        memory.bytes[0x6cf64] = 0x00;
        memory.bytes[0x6cf65] = 0x5c;
        memory.bytes[0x6cf66] = 0xa5;
        {
            uint32_t call_args[4] = {0, 0, 0, 0};
            assert(musashi_boot_run_80044dbc_command(&memory, 0x5a, 0x80071000u,
                       call_args, &stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
            assert(stop.call_address == 0x80044e94u &&
                   stop.target_address == 0x80044670u);
            assert(call_args[0] == 1 && call_args[1] == 0 &&
                   call_args[2] == 0 && call_args[3] == 0);
        }
        assert(memory.bytes[0x71000] == 0x5a);
        assert(memory.bytes[0x71010] == 0);
        assert(memory.bytes[0x71020] == 0);
        assert(memory.bytes[0x6cf65] == 0);
        assert(memory.bytes[0x6cf66] == 0);
        assert(memory.bytes[0x6cf64] == 2);
        assert(musashi_boot_read32(&memory, 0x80071040u, &word) && word == 0x1325u);
        memset(&memory, 0, sizeof(memory));
        assert(musashi_boot_run_80044d38_prefix(&memory, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x80044d48u && stop.target_address == 0x8005cd48u);
        memcpy(memory.bytes + 0x73848, "CD\n", 4);
        memcpy(memory.bytes + 0x74498, "fallback", 9);
        memset(memory.bytes + 0x728a9, 1, 0x100);
        memset(&output, 0, sizeof(output));
        assert(musashi_boot_run_80044d38_console_prefix(&memory, sink, &output, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x80044d60u && stop.target_address == 0x8005c604u);
        assert(output.count == 4 && !memcmp(output.bytes, "CD\r\n", 4));
        memcpy(memory.bytes + 0x73854, "ready\n", 7);
        memset(&output, 0, sizeof(output));
        assert(musashi_boot_run_80044d38_console_format_prefix(
                   &memory, sink, &output, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(stop.call_address == 0x80044d98u && stop.target_address == 0x80042580u);
        assert(output.count == 11 && !memcmp(output.bytes, "CD\r\nready\r\n", 11));
        {
            CallbackDeviceLog callback_log;
            MusashiCallbackDevice callback_device = {0};
            memset(&callback_log, 0, sizeof(callback_log));
            callback_log.mask = 0xa55a;
            callback_device.read16 = callback_read16;
            callback_device.write16 = callback_write16;
            callback_device.write32 = callback_write32;
            callback_device.hook_entry_int = callback_hook_entry_int;
            init_registration(&callback_device, &callback_log);
            callback_device.a0_72 = callback_a0_72;
            callback_device.exit_critical_section = callback_exit_critical;
            callback_device.gpu_cw = callback_gpu_cw;
            callback_device.reset_device = NULL;
            callback_device.userdata = &callback_log;
            assert(musashi_boot_write32(&memory, 0x8006cb88u, 0x1f801070u));
            assert(musashi_boot_write32(&memory, 0x8006cb8cu, 0x1f801074u));
            assert(musashi_boot_write32(&memory, 0x8006cb90u, 0x1f8010f0u));
            assert(musashi_boot_write32(&memory, 0x8006cbbcu, 0x1f801114u));
            assert(musashi_boot_write32(&memory, 0x8006cbc0u, 0x1f8010f4u));
            assert(musashi_boot_write32(&memory, 0x8006cb84u, 0x8006cb64u));
            assert(musashi_boot_write32(&memory, 0x8006cb6cu, 0x800429dcu));
            assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
            assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071001u));
            assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071002u));
            assert(musashi_boot_write32(&memory, 0x8006cf5cu, 0x80071004u));
            memset(&output, 0, sizeof(output));
            assert(musashi_boot_run_80044d38_callback_prefix(
                       &memory, &callback_device, sink, &output, &stop) ==
                   MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
            assert(stop.call_address == 0x80044734u && stop.target_address == 0x80044124u);
            assert(musashi_boot_read32(&memory, 0x801fff60u, &word) &&
                   word == 0x80044e9cu);
            check_registered_state(&memory, &callback_log, 1);
            assert(callback_log.hook_count == 1 &&
                   callback_log.a0_72_count == 1 && callback_log.exit_critical_count == 1);
        }
        assert(musashi_boot_run_80043450_prefix(&memory, &device, 1, &result, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(log.count == 1);
        /* A nonzero 80044D38 result returns through the branch delay slot,
         * whose explicit v0 clear is part of the retail outcome. */
        assert(result == 0);
        assert(stop.target_address == UINT32_MAX);
        log.result = 0;
        /* The alternate CD command leaf consumes only guest state and the
         * four caller-supplied CD register pointers. */
        assert(musashi_boot_write32(&memory, 0x8006cf60u, 0x80070000u));
        assert(musashi_boot_write32(&memory, 0x8006cf4cu, 0x80071000u));
        assert(musashi_boot_write32(&memory, 0x8006cf50u, 0x80071001u));
        assert(musashi_boot_write32(&memory, 0x8006cf54u, 0x80071002u));
        assert(musashi_boot_write32(&memory, 0x8006cf58u, 0x80071003u));
        assert(musashi_boot_run_80043450_prefix(&memory, &device, 1, &result, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(log.count == 2);
        /* The alternate leaf returns zero, then 80043450 turns that into its
         * documented ready result one at 800434A4. */
        assert(result == 1);
        assert(stop.target_address == UINT32_MAX);
        assert(musashi_boot_read16(&memory, 0x80070180u, &half) && half == 0x3fffu);
        assert(musashi_boot_read16(&memory, 0x800701aau, &half) && half == 0xc001u);
        assert(memory.bytes[0x71000] == 3 && memory.bytes[0x71001] == 0x80 &&
               memory.bytes[0x71002] == 0 && memory.bytes[0x71003] == 0x20);
        assert(musashi_boot_run_80043300_prefix(&memory, &device, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(log.count == 3 && stop.target_address == UINT32_MAX);
        /* Nested retry saves its real caller below the dispatcher frame. */
        assert(musashi_boot_read32(&memory, 0x801fff64u, &word) &&
               word == 0x80043318u);
        assert(musashi_boot_read32(&memory, 0x801fff7cu, &word) &&
               word == UINT32_MAX);
        assert(musashi_boot_read32(&memory, 0x8006cc84u, &word) &&
               word == 0x80043398u);
        assert(musashi_boot_read32(&memory, 0x8006cc88u, &word) &&
               word == 0x800433c0u);
        assert(musashi_boot_read32(&memory, 0x8006cf9cu, &word) &&
               word == 0x800433e8u);
        assert(musashi_boot_read32(&memory, 0x8006cfd0u, &word) && word == 0);
        log.result = 1;
        assert(musashi_boot_run_80043300_prefix(&memory, &device, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL);
        assert(log.count == 8);
        assert(stop.call_address == 0x80043378u);
        assert(stop.target_address == 0x8005c604u);
        memset(&memory, 0, sizeof(memory));
        memset(&output, 0, sizeof(output));
        memcpy(memory.bytes + 0x735fc, "CdInit: Init failed\n", 21);
        log.count = 0;
        assert(musashi_boot_run_80043300_console_prefix(
                   &memory, &device, sink, &output, &result, &stop) ==
               MUSASHI_RESETGRAPH_PREFIX_COMPLETE);
        assert(log.count == 5 && result == 0 && stop.target_address == UINT32_MAX);
        assert(output.count == 21 &&
               !memcmp(output.bytes, "CdInit: Init failed\r\n", 21));
    }
    return 0;
}
