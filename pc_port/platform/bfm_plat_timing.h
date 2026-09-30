#ifndef BFM_PLAT_TIMING_H
#define BFM_PLAT_TIMING_H

/* Timing interface: VSync pacing, frame-rate target, fast-forward.
 *
 * The retail game waits on VSync(n); the port calls bfm_plat_timing_vsync(n)
 * where the retail code waited. The pacer sleeps to the next deadline at the
 * NTSC field rate (59.94 Hz) divided by the fast-forward speed, and resyncs
 * instead of racing when it falls more than a few fields behind.
 *
 * The clock is injectable so tests (and a deterministic replay mode) never
 * depend on wall time. */

#include "bfm_plat_types.h"

/* NTSC field period in nanoseconds: 1001/60000 s. */
#define BFM_PLAT_NTSC_FIELD_NS 16683333ull
#define BFM_PLAT_TIMING_RESYNC_FIELDS 4u

typedef struct BfmPlatClock {
    uint64_t (*now_ns)(void *user);
    void (*sleep_ns)(void *user, uint64_t ns);
    void *user;
} BfmPlatClock;

typedef struct BfmPlatTimingStats {
    uint64_t vsyncs;            /* fields waited */
    uint64_t frames;            /* bfm_plat_timing_vsync calls */
    uint64_t slept_ns;
    uint64_t resyncs;
    int fast_forward;
    double speed;
} BfmPlatTimingStats;

/* NULL restores the host monotonic clock. */
void bfm_plat_timing_set_clock(const BfmPlatClock *clock);
void bfm_plat_timing_init(void);
uint64_t bfm_plat_timing_now_ns(void);

/* Waits `fields` NTSC fields (>= 1) since the previous call's deadline.
 * Emits BFM_EVENT_VSYNC once per field. Returns fields waited. */
int bfm_plat_timing_vsync(unsigned fields);

/* Fast-forward speed multiplier (0 = uncapped). */
void bfm_plat_timing_set_fast_forward_speed(double speed);
void bfm_plat_timing_set_fast_forward(int active);
int bfm_plat_timing_fast_forward(void);

void bfm_plat_timing_stats(BfmPlatTimingStats *out);

#endif
