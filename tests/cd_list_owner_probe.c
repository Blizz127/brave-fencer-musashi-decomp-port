#include "musashi_cd_controller.h"
#include "musashi_disc_media.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Fixture {
    uint64_t thread;
    int healthy;
    int idle;
    int reenter_idle;
    int missing_idle;
    unsigned raises;
    MusashiCdOwned *owner;
} Fixture;

static uint64_t current_thread(void *userdata) {
    return ((Fixture *)userdata)->thread;
}

static int healthy(void *userdata) {
    Fixture *f = (Fixture *)userdata;
    return f->healthy;
}

static int raise_irq(void *userdata) {
    ++((Fixture *)userdata)->raises;
    return 1;
}

static int transfer_idle(void *userdata, int *idle) {
    Fixture *f = (Fixture *)userdata;
    if (f->reenter_idle) {
        uint8_t value = 0xa5;
        assert(f->owner);
        (void)musashi_cd_owned_read8(f->owner, 0x1f801800u, &value);
        return 0;
    }
    if (!idle || !f->healthy) return 0;
    *idle = f->idle;
    return 1;
}

static MusashiCdOwnedState snapshot(MusashiCdOwned *owner) {
    MusashiCdOwnedState state;
    assert(musashi_cd_owned_get_state(owner, &state));
    return state;
}

static void write_reg(MusashiCdOwned *owner, unsigned reg, unsigned value) {
    assert(musashi_cd_owned_write8(owner, 0x1f801800u + reg, (uint8_t)value));
}

static unsigned read_reg(MusashiCdOwned *owner, unsigned reg) {
    uint8_t value = 0xcc;
    assert(musashi_cd_owned_read8(owner, 0x1f801800u + reg, &value));
    return value;
}

static void ack_packet(MusashiCdOwned *owner, unsigned interrupt, unsigned status) {
    MusashiCdOwnedState state = snapshot(owner);
    assert(state.interrupt == interrupt && state.response_count == 1);
    assert(read_reg(owner, 1) == status);
    write_reg(owner, 0, 1);
    write_reg(owner, 3, 7);
    write_reg(owner, 0, 0);
}

static void service_command(MusashiCdOwned *owner, unsigned command) {
    MusashiCdOwnedState state;
    write_reg(owner, 1, command);
    state = snapshot(owner);
    assert(state.busy && state.due >= state.cycle);
    assert(musashi_cd_owned_advance(owner, state.due));
}

static MusashiCdOwned *open_owner(const MusashiDiscMedia *media, Fixture *fixture,
                                  uint64_t epoch) {
    MusashiCdOwnedHardware hardware = {
        .userdata = fixture,
        .current_thread = current_thread,
        .healthy = healthy,
        .raise_irq4 = raise_irq,
        .data_transfer_idle = fixture->missing_idle ? NULL : transfer_idle,
    };
    MusashiCdOwned *owner = musashi_cd_owned_open_bios_idle(media, &hardware, epoch);
    assert(owner);
    fixture->owner = owner;
    return owner;
}

static void setloc_mode(MusashiCdOwned *owner) {
    write_reg(owner, 2, 0);
    write_reg(owner, 2, 2);
    write_reg(owner, 2, 0x16);
    service_command(owner, 2);
    ack_packet(owner, 3, 2);
    write_reg(owner, 2, 0xa0);
    service_command(owner, 0x0e);
    ack_packet(owner, 3, 2);
}

static uint64_t seek_to_pvd(MusashiCdOwned *owner) {
    MusashiCdOwnedState before;
    setloc_mode(owner);
    service_command(owner, 0x15);
    before = snapshot(owner);
    assert(before.seeking == 1 && before.seek_target_lba == 16);
    assert(before.seek_starts == 1 && before.seek_completions == 0);
    assert(snapshot(owner).interrupt == 3);
    assert(snapshot(owner).sectors_fetched == 0);
    assert(snapshot(owner).sectors_published == 0);
    ack_packet(owner, 3, 2);
    before = snapshot(owner);
    assert(before.seeking && before.due == before.cycle + 1806336);
    assert(musashi_cd_owned_advance(owner, before.due - 1));
    assert(snapshot(owner).seeking && snapshot(owner).seek_completions == 0);
    assert(musashi_cd_owned_advance(owner, before.due));
    assert(snapshot(owner).interrupt == 2);
    assert(snapshot(owner).seek_completions == 1);
    assert(snapshot(owner).seek_sector_reads == 1);
    assert(snapshot(owner).position_valid && snapshot(owner).position_lba == 16);
    assert(!snapshot(owner).seeking);
    ack_packet(owner, 2, 2);
    return snapshot(owner).cycle;
}

static uint64_t start_read(MusashiCdOwned *owner) {
    MusashiCdOwnedState state;
    service_command(owner, 6);
    ack_packet(owner, 3, 0x22);
    state = snapshot(owner);
    assert(state.reading && state.sector_due > state.cycle);
    return state.sector_due;
}

static void assert_real_bytes(MusashiCdOwned *owner, const uint8_t *expected,
                              size_t offset, size_t size) {
    uint8_t actual[2340];
    assert(size <= sizeof(actual));
    memset(actual, 0xa5, sizeof(actual));
    assert(musashi_cd_owned_read_data(owner, actual, size));
    assert(!memcmp(actual, expected + offset, size));
}

static void seek_deadlines_and_real_data(const MusashiDiscMedia *media) {
    Fixture fixture = { .thread = 7, .healthy = 1, .idle = 1 };
    MusashiCdOwned *owner = open_owner(media, &fixture, 100);
    MusashiCdOwnedState state;
    uint8_t raw16[2352], raw17[2352];
    uint64_t first_due;

    assert(musashi_disc_media_read_sector(media, 16, raw16, sizeof(raw16)));
    assert(musashi_disc_media_read_sector(media, 17, raw17, sizeof(raw17)));
    assert(raw16[15] == 2 && raw17[15] == 2);
    seek_to_pvd(owner);
    first_due = start_read(owner);
    assert(musashi_cd_owned_advance(owner, first_due));
    state = snapshot(owner);
    assert(state.interrupt == 1 && state.sectors_fetched == 1);
    assert(state.announced_valid && state.announced_lba == 16);
    assert(!state.fifo_valid && !state.data_requested);
    assert(state.seek_starts == 1 && state.seek_completions == 1);
    ack_packet(owner, 1, 0x22);

    write_reg(owner, 3, 0x80);
    state = snapshot(owner);
    assert(state.fifo_valid && state.data_requested && state.fifo_lba == 16);
    assert(state.fifo_latches == 1 && state.announced_claimed);
    assert_real_bytes(owner, raw16, 12, 12);
    assert_real_bytes(owner, raw16, 24, 2048);
    state = snapshot(owner);
    assert(state.fifo_cursor == 2060 && state.data_bytes_read == 2060);

    /* The next announcement must not replace an active requested view. */
    assert(musashi_cd_owned_advance(owner, first_due + 225792));
    state = snapshot(owner);
    assert(state.interrupt == 1 && state.sectors_fetched == 2);
    assert(state.announced_valid && state.announced_lba == 17);
    assert(state.fifo_lba == 16 && state.fifo_cursor == 2060);
    ack_packet(owner, 1, 0x22);
    write_reg(owner, 3, 0x80); /* Repeated BFRD cannot switch or rewind. */
    state = snapshot(owner);
    assert(state.fifo_lba == 16 && state.fifo_cursor == 2060);
    assert_real_bytes(owner, raw16, 2072, 280);

    write_reg(owner, 3, 0);
    assert(!snapshot(owner).data_requested && snapshot(owner).fifo_cursor == 0);
    write_reg(owner, 3, 0x80);
    state = snapshot(owner);
    assert(state.fifo_lba == 17 && state.fifo_cursor == 0 && state.data_requested);
    assert(state.fifo_latches == 2);
    assert_real_bytes(owner, raw17, 12, 12);
    assert_real_bytes(owner, raw17, 24, 1600);
    assert(snapshot(owner).fifo_cursor == 1612);
    assert(musashi_cd_owned_close(owner));
}

static void unclaimed_overrun(const MusashiDiscMedia *media) {
    Fixture fixture = { .thread = 7, .healthy = 1, .idle = 1 };
    MusashiCdOwned *owner = open_owner(media, &fixture, 0);
    uint64_t due = seek_to_pvd(owner);
    (void)due;
    due = start_read(owner);
    assert(musashi_cd_owned_advance(owner, due));
    ack_packet(owner, 1, 0x22);
    assert(musashi_cd_owned_advance(owner, due + 225792));
    assert(snapshot(owner).incoming_valid && snapshot(owner).incoming_lba == 17);
    assert(!musashi_cd_owned_advance(owner, due + 2 * 225792));
    assert(snapshot(owner).fault == MUSASHI_CD_OWNED_FIFO_OVERRUN);
    assert(snapshot(owner).announced_lba == 16 && snapshot(owner).incoming_lba == 17);
    assert(musashi_cd_owned_close(owner));
}

static void bounded_cycle_holds_incoming(const MusashiDiscMedia *media) {
    Fixture fixture = { .thread = 7, .healthy = 1, .idle = 1 };
    MusashiCdOwned *owner = open_owner(media, &fixture, 0);
    MusashiCdOwnedState state, after;
    uint64_t due;
    uint64_t bounded;
    (void)seek_to_pvd(owner);
    due = start_read(owner);
    assert(musashi_cd_owned_advance(owner, due));
    ack_packet(owner, 1, 0x22);
    assert(musashi_cd_owned_advance(owner, due + 225792));
    state = snapshot(owner);
    assert(state.incoming_valid && state.incoming_lba == 17 && state.reading);
    assert(state.sector_due != 0 && state.fault == MUSASHI_CD_OWNED_OK);
    bounded = musashi_cd_owned_bounded_cycle(&state, due + 2 * 225792);
    assert(bounded >= state.cycle && bounded < state.sector_due);
    assert(musashi_cd_owned_advance(owner, bounded));
    after = snapshot(owner);
    assert(after.fault == MUSASHI_CD_OWNED_OK);
    assert(after.incoming_valid && after.incoming_lba == 17);
    assert(after.sectors_fetched == 2);
    assert(musashi_cd_owned_close(owner));
}

static void bounded_cycle_one_fetch_per_cut(const MusashiDiscMedia *media) {
    Fixture fixture = { .thread = 7, .healthy = 1, .idle = 1 };
    MusashiCdOwned *owner = open_owner(media, &fixture, 0);
    MusashiCdOwnedState state, after;
    uint64_t due, bounded;
    (void)seek_to_pvd(owner);
    due = start_read(owner);
    assert(musashi_cd_owned_advance(owner, due));
    ack_packet(owner, 1, 0x22);
    state = snapshot(owner);
    assert(!state.incoming_valid && state.reading && state.sector_due);
    bounded = musashi_cd_owned_bounded_cycle(&state, due + 10 * 225792);
    assert(bounded == state.sector_due + 225791);
    assert(musashi_cd_owned_advance(owner, bounded));
    after = snapshot(owner);
    assert(after.fault == MUSASHI_CD_OWNED_OK);
    assert(after.incoming_valid && after.sectors_fetched == 2);
    assert(musashi_cd_owned_close(owner));
}

static void setmode_during_readn(const MusashiDiscMedia *media) {
    Fixture fixture = { .thread = 7, .healthy = 1, .idle = 1 };
    MusashiCdOwned *owner = open_owner(media, &fixture, 0);
    MusashiCdOwnedState state;
    uint64_t due;
    (void)seek_to_pvd(owner);
    due = start_read(owner);
    assert(musashi_cd_owned_advance(owner, due));
    ack_packet(owner, 1, 0x22);
    state = snapshot(owner);
    assert(state.reading && state.mode == 0xa0 && state.fault == MUSASHI_CD_OWNED_OK);
    write_reg(owner, 2, 0xa0);
    write_reg(owner, 1, 0x0e);
    state = snapshot(owner);
    assert(state.fault == MUSASHI_CD_OWNED_OK);
    assert(state.reading && state.mode == 0xa0 && state.command == 0x0e);
    assert(state.busy && state.phase == 1);
    assert(musashi_cd_owned_advance(owner, state.due));
    ack_packet(owner, 3, 0x22);
    write_reg(owner, 2, 0x00);
    write_reg(owner, 2, 0x02);
    write_reg(owner, 2, 0x16);
    write_reg(owner, 1, 2);
    state = snapshot(owner);
    assert(state.fault == MUSASHI_CD_OWNED_OK);
    assert(state.reading && state.command == 2 && state.location_valid);
    assert(musashi_cd_owned_advance(owner, state.due));
    ack_packet(owner, 3, 0x22);
    write_reg(owner, 1, 0x15);
    state = snapshot(owner);
    assert(state.fault == MUSASHI_CD_OWNED_OK);
    assert(state.command == 0x15 && state.busy && state.phase == 1);
    assert(musashi_cd_owned_advance(owner, state.due));
    state = snapshot(owner);
    assert(state.fault == MUSASHI_CD_OWNED_OK);
    assert(state.seeking && !state.reading && !state.incoming_valid);
    assert(musashi_cd_owned_close(owner));
}

static void command_completes_while_incoming_full(const MusashiDiscMedia *media) {
    Fixture fixture = { .thread = 7, .healthy = 1, .idle = 1 };
    MusashiCdOwned *owner = open_owner(media, &fixture, 0);
    MusashiCdOwnedState state, after;
    uint64_t due;
    (void)seek_to_pvd(owner);
    due = start_read(owner);
    assert(musashi_cd_owned_advance(owner, due));
    ack_packet(owner, 1, 0x22);
    assert(musashi_cd_owned_advance(owner, due + 225792));
    state = snapshot(owner);
    assert(state.incoming_valid && state.reading && state.fault == MUSASHI_CD_OWNED_OK);
    write_reg(owner, 1, 1);
    state = snapshot(owner);
    assert(state.phase == 1 && state.command == 1 && state.due > state.cycle);
    assert(musashi_cd_owned_advance_without_fetch(owner, state.due));
    after = snapshot(owner);
    assert(after.fault == MUSASHI_CD_OWNED_OK);
    assert(after.incoming_valid && after.incoming_lba == 17);
    assert(after.interrupt == 3 && after.phase == 0);
    assert(!musashi_cd_owned_advance(owner, due + 2 * 225792));
    assert(snapshot(owner).fault == MUSASHI_CD_OWNED_FIFO_OVERRUN);
    assert(musashi_cd_owned_close(owner));
}

static void retirement_rechecks_dma_idle(const MusashiDiscMedia *media) {
    Fixture fixture = { .thread = 7, .healthy = 1, .idle = 1 };
    MusashiCdOwned *owner = open_owner(media, &fixture, 0);
    MusashiCdOwnedState state;
    uint64_t due;
    uint8_t old_header[12];

    setloc_mode(owner);
    due = start_read(owner);
    assert(musashi_cd_owned_advance(owner, due));
    ack_packet(owner, 1, 0x22);
    write_reg(owner, 3, 0x80);
    assert(musashi_cd_owned_read_data(owner, old_header, sizeof(old_header)));
    state = snapshot(owner);
    assert(state.fifo_valid && state.data_requested && state.fifo_lba == 16);

    service_command(owner, 9);
    ack_packet(owner, 3, 0x22);
    state = snapshot(owner);
    assert(musashi_cd_owned_advance(owner, state.due));
    ack_packet(owner, 2, 2);
    state = snapshot(owner);
    assert(!state.reading && state.fifo_valid && state.data_requested);

    write_reg(owner, 2, 0);
    write_reg(owner, 2, 2);
    write_reg(owner, 2, 0x17);
    service_command(owner, 2);
    ack_packet(owner, 3, 2);
    fixture.idle = 1;
    write_reg(owner, 1, 0x15); /* Admission observes idle. */
    state = snapshot(owner);
    assert(state.fifo_valid && state.data_requested && state.fifo_cursor == 12);
    fixture.idle = 0; /* Retirement recheck must refuse before clearing the view. */
    assert(!musashi_cd_owned_advance(owner, state.due));
    state = snapshot(owner);
    assert(state.fault == MUSASHI_CD_OWNED_BAD_ACCESS ||
           state.fault == MUSASHI_CD_OWNED_BACKEND_LOST);
    assert(state.fifo_valid && state.data_requested && state.fifo_lba == 16);
    assert(musashi_cd_owned_close(owner));
}

static void refusal_and_holds(const MusashiDiscMedia *media) {
    Fixture fixture = { .thread = 7, .healthy = 1, .idle = 1 };
    MusashiCdOwned *owner = open_owner(media, &fixture, 0);
    MusashiCdOwnedState before, after;

    setloc_mode(owner);
    before = snapshot(owner);
    fixture.idle = 0;
    assert(!musashi_cd_owned_write8(owner, 0x1f801801u, 0x15));
    after = snapshot(owner);
    assert(after.fault == MUSASHI_CD_OWNED_BACKEND_LOST ||
           after.fault == MUSASHI_CD_OWNED_BAD_ACCESS);
    assert(after.cycle == before.cycle && after.due == before.due);
    assert(musashi_cd_owned_close(owner));

    fixture.idle = 1;
    fixture.missing_idle = 1;
    owner = open_owner(media, &fixture, 0);
    setloc_mode(owner);
    assert(!musashi_cd_owned_write8(owner, 0x1f801801u, 0x15));
    fixture.missing_idle = 0;
    assert(snapshot(owner).fault == MUSASHI_CD_OWNED_BAD_ACCESS);
    assert(musashi_cd_owned_close(owner));

    owner = open_owner(media, &fixture, 0);
    setloc_mode(owner);
    fixture.thread = 8;
    assert(!musashi_cd_owned_write8(owner, 0x1f801801u, 0x15));
    fixture.thread = 7;
    assert(snapshot(owner).fault == MUSASHI_CD_OWNED_WRONG_THREAD);
    assert(musashi_cd_owned_close(owner));

    fixture.thread = 7;
    owner = open_owner(media, &fixture, 0);
    setloc_mode(owner);
    fixture.reenter_idle = 1;
    assert(!musashi_cd_owned_write8(owner, 0x1f801801u, 0x15));
    assert(snapshot(owner).fault == MUSASHI_CD_OWNED_REENTRY);
    fixture.reenter_idle = 0;
    assert(musashi_cd_owned_close(owner));

    /* An occupied IF holds the seek completion and retries on a bounded
     * 256-cycle cadence; it must not publish INT2 early. */
    owner = open_owner(media, &fixture, 0);
    setloc_mode(owner);
    service_command(owner, 0x15);
    before = snapshot(owner);
    assert(before.interrupt == 3 && before.seeking);
    assert(musashi_cd_owned_advance(owner, before.due));
    after = snapshot(owner);
    assert(after.interrupt == 3 && after.seeking && after.seek_completions == 0);
    assert(after.due == before.due + 256);
    ack_packet(owner, 3, 2);
    assert(musashi_cd_owned_advance(owner, after.due));
    assert(snapshot(owner).interrupt == 2 && snapshot(owner).position_valid);
    ack_packet(owner, 2, 2);
    assert(musashi_cd_owned_close(owner));
}

int main(int argc, char **argv) {
    MusashiDiscMedia *media;
    assert(argc == 3);
    media = musashi_disc_media_open_pinned(argv[1], argv[2]);
    assert(media);
    seek_deadlines_and_real_data(media);
    unclaimed_overrun(media);
    bounded_cycle_holds_incoming(media);
    bounded_cycle_one_fetch_per_cut(media);
    setmode_during_readn(media);
    command_completes_while_incoming_full(media);
    retirement_rechecks_dma_idle(media);
    refusal_and_holds(media);
    musashi_disc_media_close(media);
    puts("CD_LIST_OWNER_PASS");
    return 0;
}
