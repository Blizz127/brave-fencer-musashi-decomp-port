#include "musashi_scanline_timer.h"

#include <string.h>

#define TIMER_COUNT 0x1f801110u
#define TIMER_MODE 0x1f801114u
#define TIMER_TARGET 0x1f801118u
#define TIMER_MODE_VALUE 0x507u
#define TIMER_STATUS_FLAGS 0x1800u
#define TIMER_OVERFLOW 0x1000u
#define TIMER_TARGET_REACHED 0x0800u

/* The selected NTSC Timer 1 profile is the source's 33.8688 MHz clock
 * divided by 60 frames and 263 scanlines. Integer division matches the
 * source counter rate. */
#define PACED_RATE (33868800u / (60u * 263u))
#define PACED_MODULUS 65535u

static int paced_state_valid(MusashiScanlineTimer *timer) {
    if (!timer || timer->faulted) return 0;
    if (timer->paced != 1) {
        timer->faulted = 1;
        return 0;
    }
    if ((timer->configured != 0 && timer->configured != 1) ||
        (timer->running != 0 && timer->running != 1) ||
        timer->target != 0 || timer->subcycle >= PACED_RATE ||
        timer->count >= PACED_MODULUS ||
        (!timer->configured &&
         (timer->count != 0 || timer->mode != 0 || timer->running != 0 ||
          timer->subcycle != 0)) ||
        (timer->configured &&
         timer->mode != TIMER_MODE_VALUE &&
         timer->mode != TIMER_MODE_VALUE + TIMER_OVERFLOW) ||
        (timer->configured && !timer->running &&
         (timer->count != 0 || timer->subcycle != 0))) {
        timer->faulted = 1;
        return 0;
    }
    return 1;
}

int musashi_scanline_timer_init_paced(MusashiScanlineTimer *timer,
                                      uint64_t fresh_absolute_cycle) {
    if (!timer) return 0;
    memset(timer, 0, sizeof(*timer));
    timer->last_cycles = fresh_absolute_cycle;
    timer->paced = 1;
    return 1;
}

static void paced_add_cycles(MusashiScanlineTimer *timer, uint64_t delta) {
    uint64_t ticks = delta / PACED_RATE;
    uint32_t remainder = (uint32_t)(delta % PACED_RATE);
    uint64_t quotient;
    uint64_t residue;
    uint64_t total;

    if ((uint64_t)timer->subcycle + remainder >= PACED_RATE) {
        ticks++;
        timer->subcycle = timer->subcycle + remainder - PACED_RATE;
    } else {
        timer->subcycle += remainder;
    }

    quotient = ticks / PACED_MODULUS;
    residue = ticks % PACED_MODULUS;
    total = (uint64_t)timer->count + residue;
    if (total >= PACED_MODULUS) {
        quotient++;
        total -= PACED_MODULUS;
    }
    timer->count = (uint16_t)total;
    if (quotient != 0) timer->mode |= TIMER_OVERFLOW;
}

int musashi_scanline_timer_advance(void *userdata, uint64_t absolute_cycle) {
    MusashiScanlineTimer *timer = userdata;
    uint64_t delta;

    if (!timer || !paced_state_valid(timer)) return 0;
    if (absolute_cycle < timer->last_cycles) {
        timer->faulted = 1;
        return 0;
    }
    delta = absolute_cycle - timer->last_cycles;
    timer->last_cycles = absolute_cycle;
    if (!timer->configured || !timer->running) return 1;
    paced_add_cycles(timer, delta);
    return 1;
}

int musashi_scanline_timer_write32(void *userdata, uint32_t address,
                                   uint32_t value) {
    MusashiScanlineTimer *timer = userdata;

    if (!timer || timer->faulted || address != TIMER_MODE ||
        value != 0x107u)
        return 0;
    if (timer->paced && !paced_state_valid(timer)) return 0;
    timer->count = 0;
    timer->mode = TIMER_MODE_VALUE;
    timer->running = 0;
    timer->configured = 1;
    if (timer->paced) timer->subcycle = 0;
    return 1;
}

int musashi_scanline_timer_read16(void *userdata, uint32_t address,
                                  uint16_t *value) {
    MusashiScanlineTimer *timer = userdata;

    if (!timer || !value || timer->faulted) return 0;
    if (address != TIMER_COUNT && address != TIMER_TARGET &&
        address != TIMER_MODE)
        return 0;
    if (timer->paced && !paced_state_valid(timer)) return 0;
    if (!timer->configured) return 0;
    if (address == TIMER_COUNT)
        *value = timer->count;
    else if (address == TIMER_TARGET)
        *value = timer->target;
    else if (address == TIMER_MODE) {
        *value = timer->mode;
        timer->mode &= (uint16_t)~TIMER_STATUS_FLAGS;
    } else return 0;
    return 1;
}

int musashi_scanline_timer_vblank_at(MusashiScanlineTimer *timer,
                                     uint64_t absolute_cycle) {
    if (!paced_state_valid(timer) || !musashi_scanline_timer_advance(
                                   timer, absolute_cycle))
        return 0;
    if (timer->configured && !timer->running) {
        timer->running = 1;
        timer->count = 0;
        timer->subcycle = 0;
    }
    return 1;
}

void musashi_scanline_timer_vblank(MusashiScanlineTimer *timer) {
    if (!timer) return;
    if (timer->paced) {
        timer->faulted = 1;
        return;
    }
    if (timer->configured && !timer->faulted) timer->running = 1;
}

void musashi_scanline_timer_hblank(MusashiScanlineTimer *timer) {
    if (!timer) return;
    if (timer->paced) {
        timer->faulted = 1;
        return;
    }
    if (!timer->configured || !timer->running || timer->faulted) return;
    timer->count = (uint16_t)(timer->count + 1u);
    if (timer->count == timer->target) timer->mode |= TIMER_TARGET_REACHED;
    if (timer->count == 0xffffu) timer->mode |= TIMER_OVERFLOW;
}
