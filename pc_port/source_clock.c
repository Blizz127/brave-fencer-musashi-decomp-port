#include "musashi_source_clock.h"

void musashi_source_clock_init(MusashiSourceClock *clock) {
    if (!clock) return;
    clock->cycles = 0;
    clock->faulted = 0;
}

int musashi_source_clock_advance(void *userdata, uint32_t cycles) {
    MusashiSourceClock *clock = userdata;
    uint64_t delta = (uint64_t)cycles;
    if (!clock || clock->faulted) return 0;
    if (clock->cycles > UINT64_MAX - delta) {
        clock->faulted = 1;
        return 0;
    }
    clock->cycles += delta;
    return 1;
}

