#ifndef MUSASHI_GPU_CONTROLLER_H
#define MUSASHI_GPU_CONTROLLER_H

#include <stdint.h>

#define MUSASHI_GPU_GPUREAD 0x1f801810u
#define MUSASHI_GPU_GP0 MUSASHI_GPU_GPUREAD
#define MUSASHI_GPU_GPUSTAT 0x1f801814u
#define MUSASHI_GPU_GP1 MUSASHI_GPU_GPUSTAT

typedef struct MusashiGpuDisplayState {
    uint32_t origin;
    uint32_t horizontal;
    uint32_t vertical;
    uint32_t mode;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t h_start;
    uint16_t h_end;
    uint16_t v_start;
    uint16_t v_end;
    uint8_t dot_divisor;
} MusashiGpuDisplayState;

/* Standard 1024x512 GP0(02) fill. Raw words are retained for diagnostics;
 * derived fields use hardware-reference masking/rounding and RGB555. */
typedef struct MusashiGpuFill {
    uint32_t command, xy, wh;
    uint16_t x, y, width, height, color;
} MusashiGpuFill;

/* Derive fields from raw words; rejects a command other than opcode02. */
int musashi_gpu_fill_derive(MusashiGpuFill *fill);

/* The frontend never treats a guest word as a host callback or pointer. The
 * backend owns renderer state and accepts only the checked subset below. */
typedef struct MusashiGpuBackend {
    void *userdata;
    int (*reset)(void *userdata);
    int (*draw_mode)(void *userdata, uint32_t word);
    int (*display_enable)(void *userdata, int enabled);
    int (*clear_fifo)(void *userdata);
    /* >0 accepts work, 0 reports busy, and <0 refuses the query. */
    int (*ready)(void *userdata);
    /* Applies one checked GP0 environment word (E2..E6). */
    int (*environment)(void *userdata, uint32_t word);
    /* Applies one checked GP1 display descriptor; candidate is borrowed. */
    int (*display)(void *userdata, const MusashiGpuDisplayState *candidate);
    /* Synchronously commits the whole fill into canonical renderer VRAM.
     * Return0 guarantees no fill effects; return1 commits even if a nested
     * controller violation has separately faulted the frontend. */
    int (*fill_vram)(void *userdata, const MusashiGpuFill *fill);
    /* Optional GP0(A0) upload or GP0(80..9F) copy pixel. One 16-bit texel at wrapped VRAM
     * coordinates. NULL refuses image-store packets. */
    int (*store_vram)(void *userdata, uint16_t x, uint16_t y, uint16_t pixel);
    /* Optional. One 16-bit texel read; NULL refuses textured primitives and VRAM copies. */
    int (*read_vram)(void *userdata, uint16_t x, uint16_t y, uint16_t *pixel);
} MusashiGpuBackend;

typedef struct MusashiGpuController {
    MusashiGpuBackend backend;
    uint32_t read_latch;
    uint32_t draw_mode;
    uint32_t dma_direction;
    int display_disabled;
    int faulted;
    int executing;
    uint32_t vblank_parity;
    uint32_t texture_window;
    uint32_t drawing_area_start;
    uint32_t drawing_area_end;
    uint32_t drawing_offset;
    uint32_t mask_flags;
    /* Monotonic accepted GP0 words; GP1 reset never clears this audit count. */
    uint64_t accepted_gp0_words;
    MusashiGpuDisplayState display;
    /* Pending accepted GP0 words survive DMA boundaries. Reset00/01 clears
     * them only after backend acceptance; audit counts survive all resets. */
    uint32_t fill_command, fill_xy;
    uint8_t fill_words;
    uint64_t completed_fills, filled_pixels;
    MusashiGpuFill last_fill;
    /* Pending GP0(A0) CPU-to-VRAM packet. Reset 00/01 discards it. */
    uint32_t store_command, store_xy, store_wh;
    uint16_t store_x, store_y, store_w, store_h, store_px, store_py;
    uint32_t store_remaining;
    uint8_t store_phase;
    uint64_t stored_pixels;
    /* Pending four-word GP0(80..9F) VRAM copy. Reset 00/01 discards it.
     * Pixel audit counts successful stores, including a partial faulted copy. */
    uint32_t copy_command, copy_src, copy_dst;
    uint8_t copy_words;
    uint64_t copied_pixels, completed_copies;
    /* Pending GP0 polygon/sprite packet. Reset 00/01 discards it. */
    uint8_t prim_needed, prim_got;
    uint32_t prim_data[12];
    /* Optional tee for the bfm_plat GPU bridge (pc_port/platform/
     * bfm_plat_gpu_bridge.h): every GP0/GP1 port write is passed on before
     * it is processed. With raster_disabled set, draw commands are parsed
     * and retired as usual but not rasterised (FIFO, GPUSTAT, fills and
     * VRAM transfers are unchanged). Both default to off (zeroed). */
    int (*tap)(void *userdata, uint32_t address, uint32_t value);
    void *tap_userdata;
    int raster_disabled;
    uint64_t drawn_pixels;
    /* Semi-transparency of the primitive being rasterized (psx-spx GPU):
     * semi = command bit 1; semi_mode = abr 0..3; semi_textured = blend only
     * texels whose bit 15 (STP) is set. */
    uint8_t semi, semi_mode, semi_textured;
    /* Open polyline (GP0 0x48-0x4F flat, 0x58-0x5F gouraud): words after the
     * command, the previous vertex and the colour of the next vertex. The
     * packet ends at a 0x5xxx5xxx word in a vertex (flat) or colour
     * (gouraud) position after at least two vertices. */
    uint8_t poly_active, poly_gouraud;
    uint32_t poly_words, poly_vertices, poly_command, poly_color, poly_prev_color;
    int32_t poly_x, poly_y;
} MusashiGpuController;

/* Derive and validate a checked descriptor in place. The helper masks no
 * caller-visible disagreement: callers may compare the result with their
 * candidate before publishing it. */
int musashi_gpu_display_derive(MusashiGpuDisplayState *state);

/* The caller supplies fresh-zero storage. Initialization publishes backend
 * ownership and reset state only after the checked backend reset succeeds. */
int musashi_gpu_controller_init(MusashiGpuController *controller,
                                const MusashiGpuBackend *backend);
int musashi_gpu_controller_read32(MusashiGpuController *controller,
                                   uint32_t address, uint32_t *value);
int musashi_gpu_controller_write32(MusashiGpuController *controller,
                                   uint32_t address, uint32_t value);
/* Advance the source-owned odd/even GPUSTAT bit for one real VBlank edge. */
int musashi_gpu_controller_vblank(MusashiGpuController *controller);

#endif
