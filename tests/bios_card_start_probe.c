#include "musashi_bios_card.h"
#include <assert.h>
#include <string.h>

typedef struct Fixture {
    MusashiBiosKernel kernel;
    MusashiBiosCard card;
    MusashiBiosCardStartDevice device;
    uint32_t status, mask;
    int32_t pad, timer;
    int enabled;
    unsigned operations, fail_at, writes, enters, exits;
    uint32_t addresses[32], values[32], delays[32];
} Fixture;

static int step(Fixture *f) { return ++f->operations != f->fail_at; }
static int sio(void *u, uint32_t address, uint16_t value) {
    Fixture *f = u;
    if (!step(f)) return 0;
    assert(f->writes < 32);
    f->addresses[f->writes] = address;
    f->values[f->writes] = value;
    f->delays[f->writes++] = f->card.delay_calls;
    return 1;
}
static int irq_read(void *u, uint32_t address, uint32_t *value) {
    Fixture *f = u;
    if (!step(f)) return 0;
    if (address == 0x1f801074u) *value = f->mask;
    else if (address == 0x1f801070u) *value = f->status;
    else return 0;
    return 1;
}
static int irq_write(void *u, uint32_t address, uint32_t value) {
    Fixture *f = u;
    if (!step(f)) return 0;
    if (address == 0x1f801074u) f->mask = value;
    else if (address == 0x1f801070u) f->status &= value;
    else return 0;
    assert(f->writes < 32);
    f->addresses[f->writes] = address;
    f->values[f->writes++] = value;
    return 1;
}
static int enter(void *u) {
    Fixture *f = u;
    if (!step(f)) return 0;
    /* All seven writes and the source word clear precede SYS1. */
    assert(f->writes >= 7 && f->card.serial_reset_word == 0);
    assert(musashi_bios_kernel_before_exception(&f->kernel));
    f->enters++;
    f->enabled = 0;
    return 1;
}
static int leave(void *u) {
    Fixture *f = u;
    if (!step(f)) return 0;
    assert(f->card.card_started == 1 && f->pad == 1 && f->timer == 0);
    assert(musashi_bios_kernel_before_exception(&f->kernel));
    f->exits++;
    f->enabled = 1;
    return 1;
}
static int read_pad(void *u, int32_t *value) {
    Fixture *f = u;
    if (!step(f)) return 0;
    *value = f->pad;
    return 1;
}
static int exchange(void *u, int32_t value, int32_t *old) {
    Fixture *f = u;
    if (!step(f)) return 0;
    *old = f->pad; f->pad = value;
    return 1;
}
static int timer(void *u, int32_t channel, int32_t value) {
    Fixture *f = u;
    if (!step(f)) return 0;
    assert(channel == 3); f->timer = value;
    return 1;
}
static void init(Fixture *f) {
    int32_t old = -1;
    memset(f, 0, sizeof(*f));
    musashi_bios_kernel_init(&f->kernel);
    assert(musashi_bios_card_init(&f->card, &f->kernel));
    f->device = (MusashiBiosCardStartDevice){.userdata=f, .write_sio16=sio,
        .read_irq32=irq_read, .write_irq32=irq_write, .enter_critical=enter,
        .exit_critical=leave, .read_pad=read_pad, .exchange_pad=exchange,
        .change_timer=timer};
    assert(musashi_bios_card_bind_start_device(&f->card, &f->device));
    f->card.card_started = 0x12345678;
    f->card.serial_reset_word = 0x87654321;
    assert(musashi_bios_card_init_service(&f->card, 0, &old) && old == 0);
    assert(f->card.card_started == 0x12345678 &&
           f->card.serial_reset_word == 0x87654321);
    f->card.card_started = 0; /* Explicit test initial hardware/session policy. */
    f->mask = 8;
    f->timer = 77;
}
static void ready(Fixture *f, uint32_t class_word, uint32_t spec, int32_t *handle) {
    int32_t result;
    assert(musashi_bios_events_open(&f->kernel.events, class_word, spec,
        0x2000u, 0, handle));
    assert(musashi_bios_events_enable(&f->kernel.events, *handle, &result));
    assert(musashi_bios_events_deliver(&f->kernel.events, class_word, spec, NULL, NULL));
}

int main(void) {
    Fixture f;
    int32_t result = -77, handles[5], unrelated;
    unsigned i;
    static const uint32_t specs[5] = {4, 0x8000, 0x100, 0x200, 0x2000};
    static const uint32_t addresses[8] = {0x1f80104a,0x1f80104e,0x1f801048,
        0x1f80104a,0x1f80104a,0x1f80104a,0x1f80104a,0x1f801074};
    static const uint32_t values[8] = {0x40,0x88,0xd,0,2,0x2002,0,9};
    static const uint32_t delays[7] = {0,0,0,0,1,2,3};
    init(&f);
    assert(musashi_bios_card_start_service(&f.card, &result) && result == 1);
    assert(f.operations == 13 && f.writes == 8 && f.enabled == 1);
    assert(!memcmp(addresses, f.addresses, sizeof(addresses)));
    assert(!memcmp(values, f.values, sizeof(values)));
    assert(!memcmp(delays, f.delays, sizeof(delays)));
    assert(f.card.delay_calls == 3 && f.card.delay_final_word == -1);
    assert(f.card.hook_calls == 2 && f.enters == 1 && f.exits == 1);
    assert(f.kernel.heads[2] == &f.card.sio_node && f.kernel.heads[1] == NULL);
    assert(!musashi_bios_card_init_service(&f.card, 0, &result));
    assert(musashi_bios_card_start_service(&f.card, &result));
    assert(f.card.delay_calls == 6 && f.card.sio_node.next == NULL);
    for (i = 0; i < 5; ++i) ready(&f, 0xf0000011u, specs[i], &handles[i]);
    ready(&f, 0xf0000022u, 4, &unrelated);
    f.status = 0x81;
    assert(musashi_bios_kernel_dispatch(&f.kernel, 1));
    assert(f.status == 0x80 && f.card.port == 1 && f.card.vblank_calls == 1);
    assert(f.card.maintenance_calls == 1 && f.enters == 2 && f.exits == 2);
    for (i = 0; i < 5; ++i)
        assert(f.kernel.events.records[(uint32_t)handles[i] & 0xffffu].status ==
               0x2000u);
    assert(f.kernel.events.records[(uint32_t)unrelated & 0xffffu].status ==
           0x4000u);
    /* Stale candidate does no handler work. Masked verifier avoids STAT read. */
    assert(musashi_bios_card_sio_vblank(&f.card, 1));
    assert(f.card.vblank_calls == 1 && f.card.port == 1);
    f.pad = 0; f.status = 1;
    assert(musashi_bios_kernel_dispatch(&f.kernel, 1));
    assert(f.status == 1 && f.card.port == 0 && f.card.maintenance_calls == 2);
    /* Unsupported branches retain real preceding ack/event/byte effects. */
    assert(musashi_bios_events_deliver(&f.kernel.events, 0xf0000011u, 4, NULL, NULL));
    f.pad = 1; f.card.action_in_progress = 1;
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1));
    assert(f.status == 0 && f.card.port == 0 && f.card.action_in_progress == 1);
    assert(f.kernel.events.records[(uint32_t)handles[0] & 0xffffu].status ==
           0x2000u);
    f.card.action_in_progress = 0; f.card.flags[1] = 0; f.status = 1;
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1));
    assert(f.card.port == 1 && f.card.fast_track_active == 0);
    f.card.port = 3; f.status = 1;
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1) && f.card.port == 254);
    f.card.pad_started = 1; f.status = 1;
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 1) && f.status == 1);
    assert(musashi_bios_card_shutdown(&f.card));
    assert(f.kernel.heads[2] == NULL && f.kernel.early_hook == NULL);

    /* Every device failure stops at that operation, retains prior effects,
     * never writes a guest result and never performs fabricated cleanup/SYS2. */
    for (i = 1; i <= 13; ++i) {
        init(&f); f.fail_at = i; result = -77;
        assert(!musashi_bios_card_start_service(&f.card, &result));
        assert(f.operations == i && result == -77 && f.card.start_faulted);
        assert(f.exits == 0 && f.enabled == 0);
        assert(f.card.serial_reset_word == (i <= 7 ? 0x87654321u : 0u));
        assert((f.card.sio_node.owner != NULL) == (i >= 9));
        assert(!musashi_bios_card_start_service(&f.card, &result));
        assert(f.operations == i);
        f.status = 1;
        assert(!musashi_bios_card_sio_vblank(&f.card, 1));
        assert(f.operations == i && f.status == 1);
        assert(musashi_bios_card_shutdown(&f.card));
        assert(!f.kernel.heads[2] && !f.kernel.early_hook);
    }
    return 0;
}
