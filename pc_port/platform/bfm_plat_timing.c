#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "bfm_plat_timing.h"
#include "bfm_plat_audio.h"
#include "bfm_plat_mods.h"

#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static uint64_t host_now(void *user) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    (void)user;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
#else
    struct timespec ts;
    (void)user;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static void host_sleep(void *user, uint64_t ns) {
#ifdef _WIN32
    (void)user;
    Sleep((DWORD)(ns / 1000000ull));
#else
    struct timespec ts;
    (void)user;
    ts.tv_sec = (time_t)(ns / 1000000000ull);
    ts.tv_nsec = (long)(ns % 1000000000ull);
    nanosleep(&ts, NULL);
#endif
}

static BfmPlatClock clock_ = {host_now, host_sleep, NULL};
static uint64_t deadline;
static int started;
static double ff_speed = 4.0;
static int ff_active;
static BfmPlatTimingStats stats;

void bfm_plat_timing_set_clock(const BfmPlatClock *c) {
    if (c && c->now_ns && c->sleep_ns) clock_ = *c;
    else {
        clock_.now_ns = host_now;
        clock_.sleep_ns = host_sleep;
        clock_.user = NULL;
    }
    started = 0;
}

void bfm_plat_timing_init(void) {
    started = 0;
    ff_active = 0;
    memset(&stats, 0, sizeof stats);
    stats.speed = 1.0;
    bfm_plat_audio_set_fast_forward(0);
}

uint64_t bfm_plat_timing_now_ns(void) { return clock_.now_ns(clock_.user); }

static uint64_t field_period(void) {
    if (!ff_active) return BFM_PLAT_NTSC_FIELD_NS;
    if (ff_speed <= 0.0) return 0;
    return (uint64_t)((double)BFM_PLAT_NTSC_FIELD_NS / ff_speed);
}

int bfm_plat_timing_vsync(unsigned fields) {
    uint64_t now, period, i;
    if (fields == 0) fields = 1;
    period = field_period();
    now = clock_.now_ns(clock_.user);
    if (!started) {
        deadline = now;
        started = 1;
    }
    deadline += period * fields;
    if (period == 0) {
        deadline = now;
    } else if (now < deadline) {
        uint64_t wait = deadline - now;
        clock_.sleep_ns(clock_.user, wait);
        stats.slept_ns += wait;
    } else if (now - deadline > period * BFM_PLAT_TIMING_RESYNC_FIELDS) {
        /* Fell far behind (load stall, debugger): don't race to catch up. */
        deadline = now;
        stats.resyncs++;
    }
    for (i = 0; i < fields; i++) bfm_plat_mods_emit(BFM_EVENT_VSYNC, NULL);
    stats.vsyncs += fields;
    stats.frames++;
    return (int)fields;
}

void bfm_plat_timing_set_fast_forward_speed(double s) {
    if (s >= 0.0 && s <= 64.0) ff_speed = s;
}

void bfm_plat_timing_set_fast_forward(int a) {
    ff_active = a != 0;
    bfm_plat_audio_set_fast_forward(ff_active);
}

int bfm_plat_timing_fast_forward(void) { return ff_active; }

void bfm_plat_timing_stats(BfmPlatTimingStats *out) {
    if (!out) return;
    *out = stats;
    out->fast_forward = ff_active;
    out->speed = ff_active ? ff_speed : 1.0;
}
