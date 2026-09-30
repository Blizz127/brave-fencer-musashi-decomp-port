#ifndef MUSASHI_SCANLINE_TIMER_H
#define MUSASHI_SCANLINE_TIMER_H
#include <stdint.h>

/* The legacy zero-initialized profile accepts explicit VBlank/HBlank edges for
 * the observed 0107 mode. init_paced() selects the separate timely-event NTSC
 * profile: it waits for the first VBlank, then advances at 2146 CPU cycles per
 * Timer 1 tick with a 65535-count modulus. A video timing owner supplies the
 * edges; reads never advance time. */
typedef struct MusashiScanlineTimer {
    uint16_t count, mode, target;
    int running, configured;
    uint64_t last_cycles;
    uint32_t subcycle;
    int paced, faulted;
} MusashiScanlineTimer;
int musashi_scanline_timer_write32(void *, uint32_t address, uint32_t value);
int musashi_scanline_timer_read16(void *, uint32_t address, uint16_t *value);
int musashi_scanline_timer_init_paced(MusashiScanlineTimer *, uint64_t);
int musashi_scanline_timer_advance(void *, uint64_t absolute_cycle);
int musashi_scanline_timer_vblank_at(MusashiScanlineTimer *, uint64_t absolute_cycle);
void musashi_scanline_timer_vblank(MusashiScanlineTimer *);
void musashi_scanline_timer_hblank(MusashiScanlineTimer *);
#endif
