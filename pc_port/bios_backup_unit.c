#include "musashi_bios_backup_unit.h"
#include <string.h>

static int error2(void *userdata, uint32_t device_id);

static int coherent(const MusashiBiosBackupUnit *bu) {
    return bu && bu->kernel && bu->card && bu->card->kernel == bu->kernel &&
        bu->completion.userdata == bu && bu->completion.error2 == error2 &&
        bu->card->completion == &bu->completion;
}

static int disconnected(const MusashiBiosBackupUnit *bu) {
    const MusashiBiosCardStartDevice *d = bu->card->start_device;
    return d && d->read_sio16 && d->read_sio8 && d->write_sio8 &&
        d->cards_disconnected && d->cards_disconnected(d->userdata) == 1;
}

static int reset_status(MusashiBiosBackupUnit *bu) {
    static const uint32_t specs[4] = {4, 0x8000, 0x2000, 0x100};
    unsigned i;
    for (i = 0; i < 5; i++) bu->status[i] = 0;
    for (i = 0; i < 4; i++)
        if (!musashi_bios_events_undeliver(&bu->kernel->events,
                                           0xf4000001, specs[i]))
            return 0;
    return 1;
}

/* A0:A9 first writes B9D8=1, then derives the port through B0:58 and
 * dispatches a BU operation. A0:70 owns operation0, whose branch is empty. */
static int error2(void *userdata, uint32_t device_id) {
    MusashiBiosBackupUnit *bu = userdata;
    unsigned port;
    if (!coherent(bu) || bu->busy ||
        !bu->kernel->dispatching ||
        bu->kernel->executing_node != &bu->card->sio_node ||
        (bu->stage != MUSASHI_BU_WAIT_PORT0 &&
         bu->stage != MUSASHI_BU_WAIT_PORT1))
        return 0;
    bu->status[2] = 1;
    port = device_id >> 4;
    if ((device_id != 0 && device_id != 0x10) ||
        port != (unsigned)(bu->stage == MUSASHI_BU_WAIT_PORT1) ||
        bu->card->last_port != port || bu->operation[port] != 0) {
        bu->stage = MUSASHI_BU_FAULT;
        return 0;
    }
    return 1;
}

int musashi_bios_backup_unit_init(MusashiBiosBackupUnit *bu,
    MusashiBiosKernel *kernel, MusashiBiosCard *card) {
    size_t i;
    const unsigned char *bytes = (const unsigned char *)bu;
    if (!bu || !kernel || !card || card->kernel != kernel ||
        kernel->dispatching || card->completion)
        return 0;
    for (i = 0; i < sizeof(*bu); i++) if (bytes[i]) return 0;
    bu->kernel = kernel;
    bu->card = card;
    bu->completion.userdata = bu;
    bu->completion.error2 = error2;
    if (!musashi_bios_card_bind_completion(card, &bu->completion)) {
        /* Construction failure retains its unregistered native fields. */
        bu->stage = MUSASHI_BU_FAULT;
        return 0;
    }
    return 1;
}

static void fail_port(MusashiBiosBackupUnit *bu, unsigned port) {
    unsigned i;
    for (i = 0; i < 15; i++) memset(bu->directory[port][i], 0, 32);
    for (i = 0; i < 20; i++) bu->broken[port][i] = UINT32_MAX;
}

static int queue_port(MusashiBiosBackupUnit *bu, unsigned port) {
    int32_t accepted;
    if (!musashi_bios_card_allow_new(bu->card) ||
        !musashi_bios_card_queue_read(bu->card, port * 16u, 0,
                                      bu->buffer[port], 128, &accepted))
        return 0;
    /* Begin requires both slots idle; a source rejection here is a refused
     * ownership contract, not fabricated asynchronous completion. */
    if (accepted != 1) return 0;
    bu->stage = port ? MUSASHI_BU_WAIT_PORT1 : MUSASHI_BU_WAIT_PORT0;
    return 1;
}

int musashi_bios_backup_unit_begin(MusashiBiosBackupUnit *bu) {
    unsigned i, port;
    if (!coherent(bu) || bu->busy || bu->kernel->dispatching ||
        (bu->stage != MUSASHI_BU_IDLE && bu->stage != MUSASHI_BU_DONE) ||
        !disconnected(bu) || !bu->card->initialized_word ||
        !bu->card->card_started || bu->card->pad_started ||
        bu->card->start_faulted || bu->card->transfer_faulted ||
        bu->card->action_in_progress || bu->card->fast_track_active ||
        bu->card->card_node.owner || !(bu->card->flags[0] & 1u) ||
        !(bu->card->flags[1] & 1u))
        return 0;
    bu->busy = 1;
    bu->operation[1] = 0;
    bu->operation[0] = 0;
    /* BFC088A0 buClear repeats the operation clears, then status and the
     * interleaved directory initialization. It does not clear data buffers. */
    bu->operation[1] = 0;
    bu->operation[0] = 0;
    bu->auto_format = 0;
    if (!reset_status(bu)) goto refused;
    for (i = 0; i < 15; i++) {
        memset(bu->directory[0][i], 0, 32);
        memset(bu->directory[1][i], 0, 32);
        for (port = 0; port < 2; port++) {
            bu->directory[port][i][0] = 0xa0;
            bu->directory[port][i][8] = 0xff;
            bu->directory[port][i][9] = 0xff;
        }
    }
    if (!queue_port(bu, 0)) goto refused;
    bu->busy = 0;
    return 1;
refused:
    bu->stage = MUSASHI_BU_FAULT;
    bu->busy = 0;
    return 0;
}

int musashi_bios_backup_unit_poll(MusashiBiosBackupUnit *bu, int *complete,
    int32_t *guest_result) {
    unsigned i, port;
    int error_ready = 0;
    if (!complete || !guest_result || !coherent(bu) || bu->busy ||
        bu->kernel->dispatching || bu->stage == MUSASHI_BU_IDLE ||
        bu->stage == MUSASHI_BU_FAULT)
        return 0;
    if (bu->card->start_faulted || bu->card->transfer_faulted ||
        bu->card->fast_track_active || !disconnected(bu)) {
        bu->stage = MUSASHI_BU_FAULT;
        return 0;
    }
    if (bu->stage == MUSASHI_BU_DONE) {
        *complete = 1; *guest_result = 0; return 1;
    }
    bu->busy = 1;
    /* Selected ROM compares these BU status words to exactly1. An observed
     * success would require real sector payload, beyond disconnected scope. */
    if (bu->status[0] == 1) goto refused;
    for (i = 1; i < 5; i++)
        if (bu->status[i] == 1) { error_ready = 1; break; }
    if (!error_ready) {
        *complete = 0;
        bu->busy = 0;
        return 1;
    }
    port = bu->stage == MUSASHI_BU_WAIT_PORT1;
    if (bu->card->action_in_progress || bu->card->card_node.owner ||
        bu->card->flags[port] != 0x11 || bu->card->last_port != port)
        goto refused;
    if (!reset_status(bu)) goto refused;
    fail_port(bu, port);
    if (port == 0) {
        if (!queue_port(bu, 1)) goto refused;
        *complete = 0;
    } else {
        bu->stage = MUSASHI_BU_DONE;
        *complete = 1;
        *guest_result = 0;
    }
    bu->busy = 0;
    return 1;
refused:
    bu->stage = MUSASHI_BU_FAULT;
    bu->busy = 0;
    return 0;
}

int musashi_bios_backup_unit_shutdown(MusashiBiosBackupUnit *bu) {
    if (!bu || !bu->kernel || !bu->card || bu->busy ||
        (bu->card->kernel && bu->card->kernel != bu->kernel) ||
        bu->kernel->dispatching || bu->completion.userdata != bu ||
        bu->completion.error2 != error2 ||
        !musashi_bios_card_unbind_completion(bu->card, &bu->completion))
        return 0;
    bu->card = NULL;
    bu->kernel = NULL;
    return 1;
}
