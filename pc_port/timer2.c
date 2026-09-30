#include "musashi_timer2.h"

#define TIMER2_COUNT_ADDRESS 0x1f801120u
#define TIMER2_MODE_ADDRESS 0x1f801124u
#define TIMER2_TARGET_ADDRESS 0x1f801128u
#define TIMER2_PERIOD 0xffffu

void musashi_timer2_init(MusashiTimer2 *timer, uint64_t epoch_cycles) {
    if (!timer) return;
    timer->epoch_cycles = epoch_cycles;
    timer->last_cycles = epoch_cycles;
    timer->count = 0;
    timer->mode = 0x0400u;
    timer->target = 0;
    timer->initialized = 1;
}

int musashi_timer2_advance(void *userdata, uint64_t absolute_cycles) {
    MusashiTimer2 *timer = userdata;
    uint64_t delta;
    uint64_t wraps;
    uint64_t total;
    if (!timer || !timer->initialized || absolute_cycles < timer->last_cycles)
        return 0;
    delta = absolute_cycles - timer->last_cycles;
    wraps = delta / TIMER2_PERIOD;
    total = (uint64_t)timer->count + (delta % TIMER2_PERIOD);
    wraps += total / TIMER2_PERIOD;
    timer->count = (uint16_t)(total % TIMER2_PERIOD);
    if (wraps) timer->mode |= 0x1000u;
    timer->last_cycles = absolute_cycles;
    return 1;
}

int musashi_timer2_read16(void *userdata, uint32_t address, uint16_t *value) {
    MusashiTimer2 *timer = userdata;
    if (!timer || !timer->initialized || !value) return 0;
    if (address == TIMER2_COUNT_ADDRESS) {
        *value = timer->count;
        return 1;
    }
    if (address == TIMER2_MODE_ADDRESS) {
        *value = timer->mode;
        timer->mode &= (uint16_t)~0x1800u;
        return 1;
    }
    if (address == TIMER2_TARGET_ADDRESS) {
        *value = timer->target;
        return 1;
    }
    return 0;
}

