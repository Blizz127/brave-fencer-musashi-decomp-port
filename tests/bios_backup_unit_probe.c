/* Explicit synthetic MMIO/VBlank fixture. Actual disconnected SIO and live
 * scheduler integration are separate probes, not established by this test. */
#include "musashi_bios_backup_unit.h"
#include <assert.h>
#include <string.h>

typedef struct Fixture {
    MusashiBiosKernel kernel;
    MusashiBiosCard card;
    MusashiBiosBackupUnit bu;
    MusashiBiosCardStartDevice device;
    uint32_t status, mask;
    uint16_t control, mode, baud;
    int32_t pad;
    int enabled, absent;
    unsigned data_writes, reads, writes;
    uint32_t addresses[128], values[128];
} Fixture;

static int put16(void *u, uint32_t a, uint16_t v) {
    Fixture *f = u;
    assert(f->writes < 128);
    f->addresses[f->writes] = a; f->values[f->writes++] = v;
    if (a == 0x1f80104a) f->control = v & ~0x10u;
    else if (a == 0x1f801048) f->mode = v;
    else if (a == 0x1f80104e) f->baud = v;
    else return 0;
    return 1;
}
static int get16(void *u, uint32_t a, uint16_t *v) {
    Fixture *f = u;
    if (a != 0x1f80104a || !v) return 0;
    *v = f->control; return 1;
}
static int get8(void *u, uint32_t a, uint8_t *v) {
    Fixture *f = u;
    if (a != 0x1f801040 || !v) return 0;
    f->reads++; *v = 0xff; return 1;
}
static int put8(void *u, uint32_t a, uint8_t v) {
    Fixture *f = u;
    assert(a == 0x1f801040 && v == 0x81);
    assert(f->writes < 128);
    f->addresses[f->writes] = a; f->values[f->writes++] = v;
    f->data_writes++; return 1;
}
static int get_irq(void *u, uint32_t a, uint32_t *v) {
    Fixture *f = u;
    if (a == 0x1f801070) *v = f->status;
    else if (a == 0x1f801074) *v = f->mask;
    else return 0;
    return 1;
}
static int put_irq(void *u, uint32_t a, uint32_t v) {
    Fixture *f = u;
    if (a == 0x1f801070) f->status &= v;
    else if (a == 0x1f801074) f->mask = v;
    else return 0;
    assert(f->writes < 128);
    f->addresses[f->writes] = a; f->values[f->writes++] = v;
    return 1;
}
static int enter(void *u) {
    Fixture *f = u;
    if (!musashi_bios_kernel_before_exception(&f->kernel)) return 0;
    f->enabled = 0; return 1;
}
static int leave(void *u) {
    Fixture *f = u;
    if (!musashi_bios_kernel_before_exception(&f->kernel)) return 0;
    f->enabled = 1; return 1;
}
static int get_pad(void *u, int32_t *v) { *v = ((Fixture *)u)->pad; return 1; }
static int put_pad(void *u, int32_t v, int32_t *old) {
    Fixture *f = u; *old = f->pad; f->pad = v; return 1;
}
static int timer(void *u, int32_t c, int32_t v) {
    (void)u; return c == 3 && v == 0;
}
static int absent(void *u) { return ((Fixture *)u)->absent; }
static void init(Fixture *f) {
    int32_t result;
    memset(f, 0, sizeof(*f));
    f->absent = 1;
    musashi_bios_kernel_init(&f->kernel);
    assert(musashi_bios_card_init(&f->card, &f->kernel));
    assert(musashi_bios_backup_unit_init(&f->bu, &f->kernel, &f->card));
    assert(musashi_bios_card_init_service(&f->card, 0, &result));
    f->device = (MusashiBiosCardStartDevice){
        .userdata=f, .write_sio16=put16, .read_irq32=get_irq,
        .write_irq32=put_irq, .enter_critical=enter, .exit_critical=leave,
        .read_pad=get_pad, .exchange_pad=put_pad, .change_timer=timer,
        .read_sio16=get16, .read_sio8=get8, .write_sio8=put8,
        .cards_disconnected=absent};
    assert(musashi_bios_card_bind_start_device(&f->card, &f->device));
    assert(musashi_bios_card_start_service(&f->card, &result));
    f->pad = 0;
}
static int pulse(Fixture *f) {
    int result;
    assert(f->enabled);
    f->status |= 1;
    result = musashi_bios_kernel_dispatch(&f->kernel, f->status & f->mask);
    f->status &= ~1u; /* Explicit simulated game VBlank acknowledgment. */
    return result;
}
static void launch_port0(Fixture *f) {
    static const uint32_t addresses[] = {0x1f80104a,0x1f80104a,0x1f801040,
        0x1f80104a,0x1f801070,0x1f801070,0x1f801074};
    static const uint32_t values[] = {0x12,0x1003,0x81,0x1013,
        0xffffff7f,0xffffff7f,0x81};
    unsigned before = f->writes;
    assert(pulse(f)); /* Initial port0 -> idle port1. */
    assert(f->data_writes == 0);
    assert(pulse(f));
    assert(f->data_writes == 1 && f->card.action_in_progress == 1);
    assert(f->card.card_node.owner == &f->kernel && f->mask == 0x81);
    assert(f->card.operation_step == 1 && f->card.fast_track_active == 0);
    assert(f->writes == before + 7);
    assert(!memcmp(f->addresses + before, addresses, sizeof(addresses)));
    assert(!memcmp(f->values + before, values, sizeof(values)));
    assert(f->card.fast_track_buffer == f->bu.buffer[0]);
    assert(f->card.fast_track_flags == &f->card.flags[0]);
    assert(f->card.fast_track_checksum == &f->card.checksum[0]);
    assert(musashi_bios_kernel_before_exception(&f->kernel));
}

int main(void) {
    Fixture f;
    int complete = 7;
    int32_t result = 0x12345678, handle, event;
    unsigned i, j, k, before;
    static const uint32_t timeout_addresses[] = {0x1f801070,0x1f801074,
        0x1f80104a,0x1f80104a,0x1f80104e,0x1f801048,0x1f80104a};
    static const uint32_t timeout_values[] = {0xffffff7f,1,0,0x40,0x88,0xd,0};
    init(&f);
    f.absent = 0;
    assert(!musashi_bios_backup_unit_begin(&f.bu));
    f.absent = 1;
    assert(musashi_bios_backup_unit_begin(&f.bu));
    assert(f.card.flags[0] == 2 && f.card.flags[1] == 1);
    assert(f.data_writes == 0);
    assert(f.bu.directory[0][0][0] == 0xa0 && f.bu.directory[1][14][8] == 0xff);
    f.bu.status[2] = 0x11; /* BU flags require exactly1, unlike card flags11. */
    assert(musashi_bios_backup_unit_poll(&f.bu, &complete, &result));
    assert(complete == 0 && result == 0x12345678);
    f.bu.status[2] = 0;
    assert(!musashi_bios_backup_unit_begin(&f.bu));
    launch_port0(&f);
    assert(musashi_bios_backup_unit_poll(&f.bu, &complete, &result));
    assert(!complete && !f.bu.status[2]);
    before = f.writes;
    assert(pulse(&f));
    assert(f.writes == before + 7);
    assert(!memcmp(f.addresses + before, timeout_addresses, sizeof(timeout_addresses)));
    assert(!memcmp(f.values + before, timeout_values, sizeof(timeout_values)));
    assert(f.bu.status[2] == 1 && f.card.flags[0] == 0x11);
    assert(!f.card.card_node.owner && !f.card.action_in_progress);
    assert(f.mask == 1 && f.control == 0 && f.baud == 0x88 && f.mode == 0xd);
    assert(musashi_bios_backup_unit_poll(&f.bu, &complete, &result));
    assert(!complete && result == 0x12345678 && !f.bu.status[2]);
    assert(f.card.flags[1] == 2);
    assert(pulse(&f) && f.data_writes == 2);
    assert(pulse(&f));
    assert(musashi_bios_backup_unit_poll(&f.bu, &complete, &result));
    assert(complete == 1 && result == 0 && f.card.last_port == 1);
    for (i=0;i<2;i++) {
        assert(f.card.flags[i] == 0x11);
        for (j=0;j<15;j++) for(k=0;k<32;k++) assert(f.bu.directory[i][j][k] == 0);
        for (j=0;j<20;j++) assert(f.bu.broken[i][j] == UINT32_MAX);
    }
    assert(musashi_bios_backup_unit_shutdown(&f.bu));
    assert(musashi_bios_card_shutdown(&f.card));

    /* Unexpected success is not treated as an absent-card read: payload
     * would need to be real and validated before filesystem consumption. */
    init(&f);
    assert(musashi_bios_backup_unit_begin(&f.bu));
    f.bu.status[0] = 1;
    result = 55;
    assert(!musashi_bios_backup_unit_poll(&f.bu, &complete, &result));
    assert(result == 55 && f.bu.status[0] == 1);
    assert(musashi_bios_backup_unit_shutdown(&f.bu));
    assert(musashi_bios_card_shutdown(&f.card));

    /* Callback refusal happens after real timeout/error effects, before
     * node removal and reset. Poll must not turn that partial failure into0. */
    init(&f);
    assert(musashi_bios_events_open(&f.kernel.events, 0xf0000011, 0x100,
                                    0x1000, 0x80012340, &handle));
    assert(musashi_bios_events_enable(&f.kernel.events, handle, &event));
    assert(musashi_bios_backup_unit_begin(&f.bu));
    launch_port0(&f);
    assert(!pulse(&f));
    assert(f.bu.status[2] == 1 && f.card.flags[0] == 0x11);
    assert(f.card.card_node.owner && f.card.transfer_faulted);
    result = 99;
    assert(!musashi_bios_backup_unit_poll(&f.bu, &complete, &result) && result == 99);
    assert(musashi_bios_card_shutdown(&f.card));
    assert(musashi_bios_backup_unit_shutdown(&f.bu));

    init(&f);
    assert(musashi_bios_backup_unit_begin(&f.bu));
    launch_port0(&f);
    f.status = 0x80;
    assert(!musashi_bios_kernel_dispatch(&f.kernel, 0x80));
    assert(f.card.transfer_faulted && f.data_writes == 1);
    assert(musashi_bios_card_shutdown(&f.card));
    assert(musashi_bios_backup_unit_shutdown(&f.bu));
    return 0;
}
