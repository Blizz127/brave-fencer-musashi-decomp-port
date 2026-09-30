#ifndef BFM_PLAT_GPU_BRIDGE_H
#define BFM_PLAT_GPU_BRIDGE_H

/* GPU bridge: the PS1 GPU's GP0/GP1 register protocol in front of the
 * bfm_plat renderer, so a host that emulates the GPU ports (native_boot's
 * gpu_controller) can render through any bfm_plat backend (psycross, gl)
 * instead of a CPU rasteriser.
 *
 *   GP0 draw commands     -> BfmPlatPrim (bfm_psyq_decode_packet), batched
 *                            into bfm_plat_renderer_submit
 *   GP0 E2..E5            -> draw environment (texture window, drawing
 *                            area, offset); E1/E6 ride on the prims
 *   GP0 A0 / C0 / 80      -> upload_vram / download_vram (GPUREAD) / copy
 *   GP1 05 / 08 / 03      -> display environment (origin, size, 24-bit)
 *   vblank                -> flush + present + begin_frame
 *
 * Two ways to attach it:
 *   tee      the host keeps its GPU model for FIFO/status/DMA semantics and
 *            passes every port write to bfm_gpu_bridge_tap. The model's
 *            rasteriser is turned off, and its VRAM backend should be too:
 *            fills/stores are the bridge's job, and reads go to
 *            bfm_gpu_bridge_read_vram (both paths share one renderer VRAM);
 *   replace  the host routes its GP0/GP1/GPUREAD/GPUSTAT accesses to
 *            bfm_gpu_bridge_write32/read32 directly.
 * Selected at run time by [video] gpu = cpu | bfm_plat
 * (bfm_gpu_bridge_selected). Not covered: E6 mask settings on VRAM
 * transfers, GPU timing (GPUSTAT reports always ready). */

#include <stddef.h>
#include <stdint.h>

#include "bfm_plat_config.h"
#include "bfm_plat_renderer.h"

#define BFM_GPU_BRIDGE_GP0 0x1f801810u
#define BFM_GPU_BRIDGE_GP1 0x1f801814u
#define BFM_GPU_BRIDGE_BATCH 256

typedef struct BfmGpuBridgeStats {
    uint64_t gp0_words, gp1_words, commands, prims, submits, uploads, downloads,
        copies, env_changes, disp_changes, frames, unknown;
} BfmGpuBridgeStats;

typedef struct BfmGpuBridge {
    /* GP0 command assembly */
    uint32_t cmd[256];
    unsigned have, need;          /* words collected / required (0 = none) */
    int polyline;
    /* A0 upload in progress */
    BfmPlatRect up_rect;
    uint16_t *up_px;
    size_t up_count, up_total;
    /* C0 readback */
    uint16_t *rd_px;
    size_t rd_count, rd_pos;
    uint32_t info_latch;          /* GP1 10 answer */
    int info_pending;
    /* state */
    uint16_t state;               /* decoder state: E1 | E6 << 14 */
    uint32_t e2, e3, e4, e5;
    BfmPlatDrawEnv env;
    BfmPlatDispEnv disp;
    uint32_t gp1_mode, gp1_start;
    int display_disabled;
    BfmPlatPrim batch[BFM_GPU_BRIDGE_BATCH];
    size_t nbatch;
    BfmGpuBridgeStats stats;
    int faulted;
    /* VRAM as the renderer holds it, for hosts that read pixels back
     * (bfm_gpu_bridge_read_vram); refreshed after anything was drawn */
    uint16_t *snap;
    int snap_dirty;
} BfmGpuBridge;

void bfm_gpu_bridge_init(BfmGpuBridge *b);
void bfm_gpu_bridge_free(BfmGpuBridge *b);
/* Port access (addresses BFM_GPU_BRIDGE_GP0/GP1). write returns 1 when
 * accepted; read returns 1 and the GPUREAD/GPUSTAT value. */
int bfm_gpu_bridge_write32(BfmGpuBridge *b, uint32_t address, uint32_t value);
int bfm_gpu_bridge_read32(BfmGpuBridge *b, uint32_t address, uint32_t *value);
/* Tee entry with a void* for the host's callback slot. */
int bfm_gpu_bridge_tap(void *bridge, uint32_t address, uint32_t value);
/* Flushes pending prims, presents the frame and begins the next. */
int bfm_gpu_bridge_vblank(BfmGpuBridge *b);
/* Submits pending prims (done automatically before state changes). */
int bfm_gpu_bridge_flush(BfmGpuBridge *b);
/* One VRAM pixel as the renderer holds it: for a host GPU model whose
 * rasteriser is off (tee mode) but that still answers VRAM reads (C0
 * StoreImage, VRAM copies). The whole VRAM is downloaded once and reused
 * until something is drawn, filled, uploaded or copied again. Returns 1. */
int bfm_gpu_bridge_read_vram(BfmGpuBridge *b, uint16_t x, uint16_t y, uint16_t *pixel);
/* GP1 03 display enable state (reset: disabled). */
int bfm_gpu_bridge_display_enabled(const BfmGpuBridge *b);
/* [video] gpu = bfm_plat */
int bfm_gpu_bridge_selected(const BfmPlatConfig *cfg);

#endif
