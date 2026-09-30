#include "musashi_device_epoch.h"

#include <string.h>

static int owned(const MusashiDeviceEpoch *epoch) {
    return epoch && epoch->active && epoch->adapter.owner_thread &&
           epoch->owner == epoch->adapter.owner_thread(epoch->adapter.userdata);
}

static int to_cycle(const MusashiDeviceEpoch *epoch, uint64_t tick,
                     uint64_t *cycle) {
    uint64_t elapsed, whole, fraction;
    if (tick < epoch->origin_tick) return 0;
    elapsed = tick - epoch->origin_tick;
    whole = elapsed / epoch->frequency;
    fraction = (elapsed % epoch->frequency) * MUSASHI_DEVICE_CLOCK_HZ /
               epoch->frequency;
    if (whole > (UINT64_MAX - fraction) / MUSASHI_DEVICE_CLOCK_HZ) return 0;
    *cycle = whole * MUSASHI_DEVICE_CLOCK_HZ + fraction;
    return 1;
}

static int to_tick(const MusashiDeviceEpoch *epoch, uint64_t cycle,
                    uint64_t *tick) {
    uint64_t whole = cycle / MUSASHI_DEVICE_CLOCK_HZ;
    uint64_t product = (cycle % MUSASHI_DEVICE_CLOCK_HZ) * epoch->frequency;
    uint64_t fraction = product / MUSASHI_DEVICE_CLOCK_HZ +
                        (product % MUSASHI_DEVICE_CLOCK_HZ != 0);
    uint64_t elapsed;
    if (whole > (UINT64_MAX - fraction) / epoch->frequency) return 0;
    elapsed = whole * epoch->frequency + fraction;
    if (elapsed > UINT64_MAX - epoch->origin_tick) return 0;
    *tick = epoch->origin_tick + elapsed;
    return 1;
}

int musashi_device_epoch_init(MusashiDeviceEpoch *epoch,
                              const MusashiDeviceEpochAdapter *adapter) {
    MusashiIrqSourceBatch cut = {0};
    MusashiDeviceEpochAdapter selected;
    uint64_t owner;
    if (!epoch || !adapter || !adapter->owner_thread || !adapter->collect_source ||
        !adapter->wait_until_counter || !adapter->advance_devices || !adapter->vblank_edge)
        return 0;
    owner = adapter->owner_thread(adapter->userdata);
    if (!owner || !adapter->collect_source(adapter->userdata, &cut) ||
        !cut.frequency || cut.frequency > UINT32_MAX || cut.count) return 0;
    selected = *adapter;
    memset(epoch, 0, sizeof(*epoch));
    epoch->adapter = selected;
    epoch->owner = owner;
    epoch->frequency = cut.frequency;
    epoch->origin_tick = epoch->last_cut_tick = cut.cut_tick;
    epoch->last_edge_tick = cut.cut_tick;
    epoch->active = 1;
    return 1;
}

/* Commit devices before a same-time video edge. This is an explicit native
 * tie policy; no reference tie-order or whole-cycle parity is implied. */
static int collect_and_commit(MusashiDeviceEpoch *epoch) {
    MusashiIrqSourceBatch batch;
    uint64_t target, edge_cycle, sequence, edge_tick;
    uint32_t i;
    /* Only the header and the first count edges carry meaning. This runs on
     * every instruction charge, so zeroing and copying the whole 1 KiB edge
     * array each time was measurable host overhead. */
    batch.frequency = 0;
    batch.cut_tick = 0;
    batch.count = 0;
    if (epoch->cuts == UINT64_MAX ||
        !epoch->adapter.collect_source(epoch->adapter.userdata, &batch)) {
        epoch->fault_reason = "source-collect";
        return 0;
    }
    /* Keep transferred metadata inspectable even if validation or a later
     * backend operation refuses. A faulted epoch is never replayed. */
    epoch->pending.frequency = batch.frequency;
    epoch->pending.cut_tick = batch.cut_tick;
    epoch->pending.count = batch.count;
    if (batch.count)
        memcpy(epoch->pending.edges, batch.edges,
               (batch.count < MUSASHI_IRQ_SOURCE_CAPACITY ? batch.count : MUSASHI_IRQ_SOURCE_CAPACITY) *
               sizeof(batch.edges[0]));
    epoch->pending_next = 0;
    if (batch.count > MUSASHI_IRQ_SOURCE_CAPACITY) {
        /* More VBlank edges queued than one cut holds: the CPU thread fell
         * behind host time (a starved host with host-paced time). */
        epoch->fault_reason = "source-overflow";
        return 0;
    }
    if (batch.frequency != epoch->frequency || batch.cut_tick < epoch->last_cut_tick ||
        !to_cycle(epoch, batch.cut_tick, &target) || target < epoch->cycle) {
        epoch->fault_reason = "source-cut";
        return 0;
    }
    sequence = epoch->last_sequence;
    edge_tick = epoch->last_edge_tick;
    /* Validate the entire cut before changing device state. */
    for (i = 0; i < batch.count; ++i) {
        const MusashiIrqSourceEdge *edge = &batch.edges[i];
        if (sequence == UINT64_MAX || edge->sequence != sequence + 1 ||
            edge->tick < edge_tick || edge->tick < epoch->last_cut_tick ||
            edge->tick > batch.cut_tick || !to_cycle(epoch, edge->tick, &edge_cycle) ||
            edge_cycle < epoch->cycle) {
            epoch->fault_reason = "source-edge";
            return 0;
        }
        sequence = edge->sequence;
        edge_tick = edge->tick;
    }
    epoch->last_cut_tick = batch.cut_tick;
    ++epoch->cuts;
    for (i = 0; i < batch.count; ++i) {
        const MusashiIrqSourceEdge *edge = &epoch->pending.edges[i];
        if (!to_cycle(epoch, edge->tick, &edge_cycle) ||
            !epoch->adapter.advance_devices(epoch->adapter.userdata, edge_cycle)) {
            epoch->fault_reason = "device-advance";
            return 0;
        }
        epoch->cycle = edge_cycle;
        if (!epoch->adapter.vblank_edge(epoch->adapter.userdata, edge->sequence, edge_cycle)) {
            epoch->fault_reason = "vblank-edge";
            return 0;
        }
        epoch->last_sequence = edge->sequence;
        epoch->last_edge_tick = edge->tick;
        epoch->pending_next = i + 1;
    }
    if (!epoch->adapter.advance_devices(epoch->adapter.userdata, target)) {
        epoch->fault_reason = "device-advance";
        return 0;
    }
    epoch->cycle = target;
    return 1;
}

static int advance(MusashiDeviceEpoch *epoch, uint32_t cost) {
    uint64_t reserved, target_tick;
    int ok = 0;
    if (!owned(epoch) || epoch->busy || epoch->faulted) return 0;
    epoch->busy = 1;
    if (epoch->cycle > UINT64_MAX - cost) goto done;
    reserved = epoch->cycle + cost;
    if (!collect_and_commit(epoch)) goto done;
    if (epoch->cycle < reserved) {
        if (epoch->waits == UINT64_MAX || !to_tick(epoch, reserved, &target_tick) ||
            !epoch->adapter.wait_until_counter(epoch->adapter.userdata, target_tick)) {
            epoch->fault_reason = "host-wait";
            goto done;
        }
        ++epoch->waits;
        /* The adapter promised to reach the reservation using actual host
         * time. A stalled/early wait cannot manufacture elapsed cycles. */
        if (!collect_and_commit(epoch)) goto done;
        if (epoch->last_cut_tick < target_tick || epoch->cycle < reserved) {
            epoch->fault_reason = "host-wait-short";
            goto done;
        }
    }
    ok = 1;
done:
    epoch->busy = 0;
    if (!ok) epoch->faulted = 1;
    return ok;
}

int musashi_device_epoch_advance_cost(void *userdata, uint32_t cost) {
    return advance(userdata, cost);
}

int musashi_device_epoch_sync(MusashiDeviceEpoch *epoch) {
    return advance(epoch, 0);
}

int musashi_device_epoch_shutdown(MusashiDeviceEpoch *epoch) {
    if (!owned(epoch) || epoch->busy) return 0;
    epoch->active = 0;
    return 1;
}
