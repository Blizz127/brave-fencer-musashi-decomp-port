#ifndef MUSASHI_GPU_PSYCROSS_H
#define MUSASHI_GPU_PSYCROSS_H

#include <stdint.h>
#include "musashi_gpu_controller.h"

/* Exclusive owner of the pinned PsyCross global GPU state. Initialize zeroed
 * storage on the live SDL/GL thread; shutdown before destroying that context.
 * The appended raw environment latches are owned adapter state, never guest
 * pointers or aliases into guest memory. */
typedef struct MusashiGpuPsyCross {
    uint64_t owner_thread;
    void *context;
    void *window;
    uint64_t presentations;
    int initialized;
    int display_enabled;
    int command_active;
    int faulted;
    MusashiGpuDisplayState display;
    /* Guest raw E2..E6 state owned by this renderer adapter. */
    uint32_t texture_window;
    uint32_t drawing_area_start;
    uint32_t drawing_area_end;
    uint32_t drawing_offset;
    uint32_t mask_flags;
    /* Inclusive canonical corners; RECT16 width/height are derived. */
    int16_t clip_x1;
    int16_t clip_y1;
    int16_t clip_x2;
    int16_t clip_y2;
    int16_t texture_mask_x;
    int16_t texture_mask_y;
    int16_t texture_offset_x;
    int16_t texture_offset_y;
    uint64_t completed_fills, filled_pixels;
    MusashiGpuFill last_fill;
} MusashiGpuPsyCross;

int musashi_gpu_psycross_init(MusashiGpuPsyCross *, MusashiGpuBackend *);
/* Host overlay drawn after each present's VRAM blit (drawable size w x h);
 * NULL removes it. overlay_rect fills a solid rectangle from inside it. */
typedef void (*MusashiGpuPsyCrossOverlay)(void *user, int width, int height);
void musashi_gpu_psycross_set_overlay(MusashiGpuPsyCrossOverlay fn, void *user);
void musashi_gpu_psycross_overlay_rect(int x, int y, int w, int h,
                                       uint8_t r, uint8_t g, uint8_t b);
int musashi_gpu_psycross_shutdown(MusashiGpuPsyCross *);

/* Checked read-only canonical VRAM diagnostics. Rectangle must be nonempty,
 * wholly in1024x512, and out_halfwords sufficient. No output on refusal. */
int musashi_gpu_psycross_read_vram(MusashiGpuPsyCross *, unsigned x, unsigned y,
                                  unsigned width, unsigned height,
                                  uint16_t *out, uint32_t out_halfwords);

/* Presentation: integer-scaled VRAM blit that fills the drawable.
 * Leftover letterbox is black. GP1 display-on blits the canonical VRAM
 * rectangle. Preserves VRAM. Refuses an active PsyCross scene or queued
 * geometry. */
int musashi_gpu_psycross_present(MusashiGpuPsyCross *);

#endif
