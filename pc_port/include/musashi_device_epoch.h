#ifndef MUSASHI_DEVICE_EPOCH_H
#define MUSASHI_DEVICE_EPOCH_H

#include "musashi_irq_scheduler.h"

#define MUSASHI_DEVICE_CLOCK_HZ 33868800u

/* All callbacks execute on the installing owner. collect_source atomically
 * samples a monotonic host clock and transfers actual source metadata only.
 * Neither wait nor any device operation may dispatch guest code. */
typedef struct MusashiDeviceEpochAdapter {
    void *userdata;
    uint64_t (*owner_thread)(void *);
    int (*collect_source)(void *, MusashiIrqSourceBatch *);
    int (*wait_until_counter)(void *, uint64_t target_tick);
    int (*advance_devices)(void *, uint64_t device_cycle);
    int (*vblank_edge)(void *, uint64_t sequence, uint64_t device_cycle);
} MusashiDeviceEpochAdapter;

/* Fresh native elapsed-time profile, never a captured BIOS/device phase.
 * Source instruction costs reserve time; host work and waits also elapse.
 * The independent source-cost ledger is owned by the caller. */
typedef struct MusashiDeviceEpoch {
    MusashiDeviceEpochAdapter adapter;
    MusashiIrqSourceBatch pending;
    uint32_t pending_next;
    uint64_t owner;
    uint64_t frequency;
    uint64_t origin_tick;
    uint64_t last_cut_tick;
    uint64_t cycle;
    uint64_t last_sequence;
    uint64_t last_edge_tick;
    uint64_t cuts;
    uint64_t waits;
    int active;
    int busy;
    int faulted;
    /* Which check faulted the epoch (static string), for diagnostics. */
    const char *fault_reason;
} MusashiDeviceEpoch;

/* Fresh-storage constructor only: never reinitialize an active/faulted epoch
 * to resume or reanchor it. A rejected nonempty initial collection may already
 * have transferred source metadata; failure is not a retry boundary.
 * init requires a source-empty cut, before source installation. Host clock
 * frequencies 1..UINT32_MAX are supported by overflow-safe C99 conversion. */
int musashi_device_epoch_init(MusashiDeviceEpoch *, const MusashiDeviceEpochAdapter *);
int musashi_device_epoch_advance_cost(void *, uint32_t cost);
int musashi_device_epoch_sync(MusashiDeviceEpoch *);
int musashi_device_epoch_shutdown(MusashiDeviceEpoch *);

#endif
