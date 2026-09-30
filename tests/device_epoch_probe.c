#include "musashi_device_epoch.h"
#include <assert.h>
#include <string.h>

typedef struct Fixture {
    uint64_t owner, now, frequency;
    MusashiIrqSourceBatch next;
    unsigned collects, advances, edges, wait_calls;
    unsigned fail_advance, fail_edge;
    int wait_fails, wait_stalls;
    uint64_t device_cycle;
    unsigned parity, irq;
    char order[32];
    unsigned order_size;
} Fixture;

static uint64_t owner(void *p) { return ((Fixture *)p)->owner; }
static int collect(void *p, MusashiIrqSourceBatch *out) {
    Fixture *f = p;
    ++f->collects;
    *out = f->next;
    out->cut_tick = f->now;
    out->frequency = f->frequency;
    f->next.count = 0;
    return 1;
}
static int wait_until(void *p, uint64_t tick) {
    Fixture *f = p;
    ++f->wait_calls;
    if (f->wait_fails) return 0;
    if (!f->wait_stalls) f->now = tick;
    return 1;
}
static int device_advance(void *p, uint64_t cycle) {
    Fixture *f = p;
    ++f->advances;
    if (f->fail_advance == f->advances) return 0;
    assert(cycle >= f->device_cycle);
    f->device_cycle = cycle;
    f->order[f->order_size++] = 'A';
    return 1;
}
static int edge(void *p, uint64_t sequence, uint64_t cycle) {
    Fixture *f = p;
    ++f->edges;
    if (f->fail_edge == f->edges) return 0;
    assert(sequence == f->edges && cycle == f->device_cycle);
    f->parity ^= 1;
    f->irq |= 1;
    f->order[f->order_size++] = 'V';
    return 1;
}
static void fresh(Fixture *f, MusashiDeviceEpoch *epoch) {
    MusashiDeviceEpochAdapter adapter = {f,owner,collect,wait_until,device_advance,edge};
    memset(f, 0, sizeof(*f));
    f->owner = 7;
    f->frequency = MUSASHI_DEVICE_CLOCK_HZ;
    f->now = 100;
    assert(musashi_device_epoch_init(epoch, &adapter));
}

static void test_order_pacing_and_elapsed_wait(void) {
    Fixture f;
    MusashiDeviceEpoch epoch;
    fresh(&f, &epoch);
    f.now = 110;
    f.next.count = 2;
    f.next.edges[0] = (MusashiIrqSourceEdge){1,105};
    f.next.edges[1] = (MusashiIrqSourceEdge){2,105};
    assert(musashi_device_epoch_sync(&epoch));
    assert(epoch.cycle == 10 && epoch.last_sequence == 2);
    assert(f.parity == 0 && f.irq == 1 && f.edges == 2);
    assert(f.order_size == 5 && !memcmp(f.order, "AVAVA", 5));
    assert(musashi_device_epoch_advance_cost(&epoch, 3));
    assert(epoch.cycle == 13 && epoch.waits == 1 && f.wait_calls == 1);
    f.now = 120; /* Measured host work/wait, not invented source instructions. */
    assert(musashi_device_epoch_sync(&epoch) && epoch.cycle == 20);
    assert(musashi_device_epoch_sync(&epoch) && epoch.cycle == 20);
    assert(f.edges == 2 && f.wait_calls == 1);
    assert(musashi_device_epoch_shutdown(&epoch));
    assert(!musashi_device_epoch_sync(&epoch));
}

static void test_partial_failure_keeps_remaining_metadata(void) {
    Fixture f;
    MusashiDeviceEpoch epoch;
    unsigned collects;
    fresh(&f, &epoch);
    f.now = 110;
    f.next.count = 2;
    f.next.edges[0] = (MusashiIrqSourceEdge){1,105};
    f.next.edges[1] = (MusashiIrqSourceEdge){2,108};
    f.fail_advance = 2;
    assert(!musashi_device_epoch_sync(&epoch));
    assert(epoch.faulted && !epoch.busy && epoch.cycle == 5);
    assert(epoch.pending.count == 2 && epoch.pending_next == 1);
    assert(epoch.pending.edges[1].sequence == 2 && f.edges == 1 && f.irq == 1);
    collects = f.collects;
    assert(!musashi_device_epoch_sync(&epoch) && f.collects == collects);
    assert(musashi_device_epoch_shutdown(&epoch));
}

static void test_refused_clocks_and_metadata(void) {
    unsigned choice;
    for (choice = 0; choice < 5; ++choice) {
        Fixture f;
        MusashiDeviceEpoch epoch;
        fresh(&f, &epoch);
        f.now = 110;
        if (choice == 0) f.now = 99;
        if (choice == 1) f.frequency++;
        if (choice >= 2) {
            f.next.count = 1;
            f.next.edges[0] = (MusashiIrqSourceEdge){1,105};
            if (choice == 2) f.next.edges[0].sequence = 2;
            if (choice == 3) f.next.edges[0].tick = 99;
            if (choice == 4) f.next.edges[0].tick = 111;
        }
        assert(!musashi_device_epoch_sync(&epoch));
        assert(epoch.faulted && !f.advances && !f.edges);
    }
}

static void test_owner_stall_and_overflow(void) {
    Fixture f;
    MusashiDeviceEpoch epoch;
    unsigned calls;
    fresh(&f, &epoch);
    calls = f.collects;
    f.owner = 8;
    assert(!musashi_device_epoch_sync(&epoch) && f.collects == calls);
    f.owner = 7;
    epoch.busy = 1;
    assert(!musashi_device_epoch_sync(&epoch) && f.collects == calls);
    epoch.busy = 0;
    f.wait_stalls = 1;
    assert(!musashi_device_epoch_advance_cost(&epoch, 2));
    assert(epoch.faulted && epoch.cycle == 0 && f.wait_calls == 1);
    fresh(&f, &epoch);
    epoch.cycle = UINT64_MAX;
    calls = f.collects;
    assert(!musashi_device_epoch_advance_cost(&epoch, 1));
    assert(epoch.faulted && f.collects == calls);
    fresh(&f, &epoch);
    epoch.frequency = f.frequency = 1;
    f.now = UINT64_MAX;
    assert(!musashi_device_epoch_sync(&epoch) && epoch.faulted && !f.advances);
}

static void test_fractional_conversion_and_initial_refusal(void) {
    Fixture f = {0};
    MusashiDeviceEpoch epoch, before;
    MusashiDeviceEpochAdapter adapter = {&f,owner,collect,wait_until,device_advance,edge};
    f.owner = 7;
    memset(&epoch, 0xa5, sizeof(epoch));
    before = epoch;
    assert(!musashi_device_epoch_init(&epoch, &adapter));
    assert(!memcmp(&before, &epoch, sizeof(epoch)));
    f.frequency = (uint64_t)UINT32_MAX + 1;
    assert(!musashi_device_epoch_init(&epoch, &adapter));
    f.frequency = 1000000000;
    f.now = 500;
    f.next.count = 1;
    f.next.edges[0] = (MusashiIrqSourceEdge){1,500};
    assert(!musashi_device_epoch_init(&epoch, &adapter));
    assert(!memcmp(&before, &epoch, sizeof(epoch)));
    /* A distinct fresh source fixture, not a retry of the consumed batch. */
    memset(&f, 0, sizeof(f));
    f.owner = 7;
    f.frequency = 1000000000;
    f.now = 500;
    assert(musashi_device_epoch_init(&epoch, &adapter));
    assert(musashi_device_epoch_advance_cost(&epoch, 1));
    assert(f.now == 530 && epoch.cycle == 1); /* ceil(1GHz/33.8688MHz). */
    f.now = 1000000500;
    assert(musashi_device_epoch_sync(&epoch) && epoch.cycle == MUSASHI_DEVICE_CLOCK_HZ);
}

int main(void) {
    test_order_pacing_and_elapsed_wait();
    test_partial_failure_keeps_remaining_metadata();
    test_refused_clocks_and_metadata();
    test_owner_stall_and_overflow();
    test_fractional_conversion_and_initial_refusal();
    return 0;
}
