#ifndef MUSASHI_SOURCE_CLOCK_H
#define MUSASHI_SOURCE_CLOCK_H
#include <stdint.h>

/* Relative source-execution cost owned by one caller. The caller supplies
 * modeled instruction/data-access costs; this module never reads wall time or
 * services devices. */
typedef struct MusashiSourceClock {
    uint64_t cycles;
    int faulted;
} MusashiSourceClock;

void musashi_source_clock_init(MusashiSourceClock *);
int musashi_source_clock_advance(void *, uint32_t cycles);
#endif

