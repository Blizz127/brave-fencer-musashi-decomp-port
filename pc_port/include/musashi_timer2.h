#ifndef MUSASHI_TIMER2_H
#define MUSASHI_TIMER2_H
#include <stdint.h>

/* Fresh selected-reference Timer 2 owner. The caller supplies an absolute
 * monotonic epoch; this model has no wall-clock or captured-phase source.
 * It represents the timely event-serviced profile with a 0xFFFF period,
 * mode 0x0400, target zero, and no IRQ40 production. */
typedef struct MusashiTimer2 {
    uint64_t epoch_cycles;
    uint64_t last_cycles;
    uint16_t count;
    uint16_t mode;
    uint16_t target;
    int initialized;
} MusashiTimer2;

void musashi_timer2_init(MusashiTimer2 *, uint64_t epoch_cycles);
int musashi_timer2_advance(void *, uint64_t absolute_cycles);
int musashi_timer2_read16(void *, uint32_t address, uint16_t *value);
#endif

