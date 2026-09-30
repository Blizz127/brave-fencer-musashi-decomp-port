/* GPU bridge: GP0/GP1 port protocol -> bfm_plat renderer. See the header. */

#include "bfm_plat_gpu_bridge.h"
#include "psyq/bfm_psyq_compat.h"

#include <stdlib.h>
#include <string.h>

void bfm_gpu_bridge_init(BfmGpuBridge *b) {
    memset(b, 0, sizeof *b);
    b->env.clip.w = 1024;
    b->env.clip.h = 512;
    b->disp.disp.w = 320;
    b->disp.disp.h = 240;
    b->snap_dirty = 1;
    b->display_disabled = 1;
}

void bfm_gpu_bridge_free(BfmGpuBridge *b) {
    free(b->up_px);
    free(b->rd_px);
    free(b->snap);
    b->up_px = b->rd_px = NULL;
    b->snap = NULL;
}

int bfm_gpu_bridge_display_enabled(const BfmGpuBridge *b) {
    return b && !b->display_disabled;
}

int bfm_gpu_bridge_read_vram(BfmGpuBridge *b, uint16_t x, uint16_t y, uint16_t *pixel) {
    if (!b || !pixel) return 0;
    if (!b->snap) {
        b->snap = (uint16_t *)calloc(1024u * 512u, 2);
        if (!b->snap) return 0;
        b->snap_dirty = 1;
    }
    if (b->snap_dirty) {
        BfmPlatRect all = {0, 0, 1024, 512};
        bfm_gpu_bridge_flush(b);
        if (bfm_plat_renderer_download_vram(&all, b->snap) != BFM_PLAT_OK)
            memset(b->snap, 0, 1024u * 512u * 2u);
        b->snap_dirty = 0;
    }
    *pixel = b->snap[(size_t)(y & 511u) * 1024u + (x & 1023u)];
    return 1;
}

int bfm_gpu_bridge_selected(const BfmPlatConfig *cfg) {
    return cfg && cfg->gpu_path == BFM_GPU_BFM_PLAT;
}

int bfm_gpu_bridge_flush(BfmGpuBridge *b) {
    int r = BFM_PLAT_OK;
    if (b->nbatch) {
        b->snap_dirty = 1;
        r = bfm_plat_renderer_submit(b->batch, b->nbatch);
        b->stats.submits++;
        b->nbatch = 0;
    }
    return r;
}

/* ---- environment ------------------------------------------------------- */

static int sext11(uint32_t v) { return (int)(v & 0x7ff) - ((v & 0x400) ? 0x800 : 0); }

static void push_env(BfmGpuBridge *b) {
    int x1 = (int)(b->e3 & 0x3ff), y1 = (int)((b->e3 >> 10) & 0x1ff);
    int x2 = (int)(b->e4 & 0x3ff), y2 = (int)((b->e4 >> 10) & 0x1ff);
    uint32_t mx = b->e2 & 31, my = (b->e2 >> 5) & 31, ox = (b->e2 >> 10) & 31,
             oy = (b->e2 >> 15) & 31;
    bfm_gpu_bridge_flush(b);
    b->env.clip.x = (int16_t)x1;
    b->env.clip.y = (int16_t)y1;
    b->env.clip.w = (int16_t)(x2 >= x1 ? x2 - x1 + 1 : 0);
    b->env.clip.h = (int16_t)(y2 >= y1 ? y2 - y1 + 1 : 0);
    b->env.offset_x = (int16_t)sext11(b->e5);
    b->env.offset_y = (int16_t)sext11(b->e5 >> 11);
    /* GP0 E2 mask/offset (8-texel units) -> window rect: the PsyQ RECT form
     * (size = the unmasked span); only power-of-two spans are exact */
    b->env.texture_window.w = (int16_t)(mx ? 256 - (int)(mx * 8) : 0);
    b->env.texture_window.h = (int16_t)(my ? 256 - (int)(my * 8) : 0);
    b->env.texture_window.x = (int16_t)((ox & mx) * 8);
    b->env.texture_window.y = (int16_t)((oy & my) * 8);
    b->env.tpage = (uint16_t)(b->state & 0x3fff);
    b->env.dither = (uint8_t)((b->state >> 9) & 1);
    b->env.draw_on_display = (uint8_t)((b->state >> 10) & 1);
    b->env.clear_bg = 0;
    bfm_plat_renderer_set_draw_env(&b->env);
    b->stats.env_changes++;
}

static void push_disp(BfmGpuBridge *b) {
    static const int16_t widths[4] = {256, 320, 512, 640};
    uint32_t m = b->gp1_mode;
    BfmPlatDispEnv d;
    memset(&d, 0, sizeof d);
    d.disp.x = (int16_t)(b->gp1_start & 0x3ff);
    d.disp.y = (int16_t)((b->gp1_start >> 10) & 0x1ff);
    d.disp.w = (m & 0x40) ? 368 : widths[m & 3];
    d.interlaced = (uint8_t)((m >> 5) & 1);
    d.disp.h = (int16_t)(((m & 4) && d.interlaced) ? 480 : 240);
    d.rgb24 = (uint8_t)((m >> 4) & 1);
    if (d.disp.x + d.disp.w > 1024) d.disp.w = (int16_t)(1024 - d.disp.x);
    if (d.disp.y + d.disp.h > 512) d.disp.h = (int16_t)(512 - d.disp.y);
    if (!memcmp(&d, &b->disp, sizeof d)) return;
    bfm_gpu_bridge_flush(b);
    b->disp = d;
    bfm_plat_renderer_set_disp_env(&d);
    b->stats.disp_changes++;
}

/* ---- VRAM transfers ------------------------------------------------------ */

/* Calls fn for the up-to-4 pieces of a rect that wraps at the VRAM edges. */
static void split(const BfmPlatRect *r, uint16_t *px, int upload) {
    int w0 = r->x + r->w > 1024 ? 1024 - r->x : r->w;
    int h0 = r->y + r->h > 512 ? 512 - r->y : r->h;
    int xs[2], ws[2], ys[2], hs[2], nx = 1, ny = 1, i, j, row;
    xs[0] = r->x; ws[0] = w0;
    ys[0] = r->y; hs[0] = h0;
    if (w0 < r->w) { xs[1] = 0; ws[1] = r->w - w0; nx = 2; }
    if (h0 < r->h) { ys[1] = 0; hs[1] = r->h - h0; ny = 2; }
    for (j = 0; j < ny; j++)
        for (i = 0; i < nx; i++) {
            BfmPlatRect piece;
            uint16_t *tmp;
            int ox = i ? w0 : 0, oy = j ? h0 : 0;
            piece.x = (int16_t)xs[i]; piece.y = (int16_t)ys[j];
            piece.w = (int16_t)ws[i]; piece.h = (int16_t)hs[j];
            if (piece.w <= 0 || piece.h <= 0) continue;
            tmp = (uint16_t *)malloc((size_t)piece.w * piece.h * 2);
            if (!tmp) return;
            if (upload) {
                for (row = 0; row < piece.h; row++)
                    memcpy(tmp + (size_t)row * piece.w, px + (size_t)(oy + row) * r->w + ox,
                           (size_t)piece.w * 2);
                bfm_plat_renderer_upload_vram(&piece, tmp);
            } else {
                if (bfm_plat_renderer_download_vram(&piece, tmp) != BFM_PLAT_OK)
                    memset(tmp, 0, (size_t)piece.w * piece.h * 2);
                for (row = 0; row < piece.h; row++)
                    memcpy(px + (size_t)(oy + row) * r->w + ox, tmp + (size_t)row * piece.w,
                           (size_t)piece.w * 2);
            }
            free(tmp);
        }
}

static BfmPlatRect xfer_rect(uint32_t xy, uint32_t wh) {
    BfmPlatRect r;
    unsigned w = wh & 0xffff, h = wh >> 16;
    r.x = (int16_t)(xy & 0x3ff);
    r.y = (int16_t)((xy >> 16) & 0x1ff);
    /* psx-spx: size 0 means the maximum */
    r.w = (int16_t)(((w - 1) & 0x3ff) + 1);
    r.h = (int16_t)(((h - 1) & 0x1ff) + 1);
    return r;
}

static void finish_upload(BfmGpuBridge *b) {
    bfm_gpu_bridge_flush(b);
    b->snap_dirty = 1;
    split(&b->up_rect, b->up_px, 1);
    b->stats.uploads++;
    b->up_total = b->up_count = 0;
}

/* ---- GP0 ------------------------------------------------------------------ */

/* Words a GP0 command needs (0 = polyline, terminated by a word). */
static unsigned cmd_words(uint32_t w) {
    unsigned op = w >> 24;
    if (op >= 0x20 && op < 0x40) {
        unsigned n = (op & 8) ? 4 : 3;
        return 1 + n + ((op & 4) ? n : 0) + ((op & 0x10) ? n - 1 : 0);
    }
    if (op >= 0x40 && op < 0x60) {
        if (op & 8) return 0;
        return (op & 0x10) ? 4 : 3;
    }
    if (op >= 0x60 && op < 0x80) return 2 + ((op & 4) ? 1 : 0) + (((op >> 3) & 3) == 0 ? 1 : 0);
    if (op == 0x02) return 3;
    if (op >= 0x80 && op < 0xa0) return 4;
    if (op >= 0xa0 && op < 0xe0) return 3;
    return 1;
}

static void run_command(BfmGpuBridge *b) {
    uint32_t *w = b->cmd;
    unsigned op = w[0] >> 24;
    b->stats.commands++;
    if ((op >= 0x20 && op < 0x80) || op == 0x02) {
        size_t n;
        if (b->nbatch + 64 > BFM_GPU_BRIDGE_BATCH) bfm_gpu_bridge_flush(b);
        n = bfm_psyq_decode_packet(w, b->have, &b->state, b->batch + b->nbatch,
                                   BFM_GPU_BRIDGE_BATCH - b->nbatch);
        b->nbatch += n;
        b->stats.prims += n;
        if (n) b->snap_dirty = 1;   /* queued: a VRAM read must flush first */
        return;
    }
    switch (op) {
    case 0xe1: case 0xe6: {
        uint16_t before = b->state;
        /* the decoder's state word: E1 in bits 0-13, E6 in 14-15 */
        if (op == 0xe1) b->state = (uint16_t)((b->state & 0xc000u) | (w[0] & 0x3fffu));
        else b->state = (uint16_t)((b->state & 0x3fffu) | ((w[0] & 1u) ? 0x4000u : 0) |
                                   ((w[0] & 2u) ? 0x8000u : 0));
        /* dither / draw-to-display live in the env; the rest rides on prims */
        if (((before ^ b->state) & 0x0600) != 0) push_env(b);
        return;
    }
    case 0xe2: b->e2 = w[0] & 0xffffff; push_env(b); return;
    case 0xe3: b->e3 = w[0] & 0xffffff; push_env(b); return;
    case 0xe4: b->e4 = w[0] & 0xffffff; push_env(b); return;
    case 0xe5: b->e5 = w[0] & 0xffffff; push_env(b); return;
    default: break;
    }
    if (op >= 0x80 && op < 0xa0) {        /* VRAM -> VRAM */
        BfmPlatRect src = xfer_rect(w[1], w[3]), dst = xfer_rect(w[2], w[3]);
        uint16_t *px = (uint16_t *)malloc((size_t)src.w * src.h * 2);
        bfm_gpu_bridge_flush(b);
        if (px) {
            split(&src, px, 0);
            split(&dst, px, 1);
            free(px);
        }
        b->stats.copies++;
        b->snap_dirty = 1;
        return;
    }
    if (op >= 0xc0 && op < 0xe0) {        /* VRAM -> CPU (GPUREAD) */
        BfmPlatRect r = xfer_rect(w[1], w[2]);
        size_t n = (size_t)r.w * r.h;
        bfm_gpu_bridge_flush(b);
        free(b->rd_px);
        b->rd_px = (uint16_t *)calloc(n + 1, 2);
        b->rd_count = b->rd_px ? n : 0;
        b->rd_pos = 0;
        if (b->rd_px) split(&r, b->rd_px, 0);
        b->stats.downloads++;
        return;
    }
    if (op == 0x00 || op == 0x01 || (op >= 0x03 && op <= 0x1f) || op >= 0xe0) return;
    b->stats.unknown++;
}

static int gp0(BfmGpuBridge *b, uint32_t w) {
    b->stats.gp0_words++;
    if (b->up_total) {                    /* A0 data words */
        if (b->up_count < b->up_total) b->up_px[b->up_count++] = (uint16_t)w;
        if (b->up_count < b->up_total) b->up_px[b->up_count++] = (uint16_t)(w >> 16);
        if (b->up_count >= b->up_total) finish_upload(b);
        return 1;
    }
    if (!b->have) {
        b->need = cmd_words(w);
        b->polyline = b->need == 0;
    }
    if (b->have < 256) b->cmd[b->have++] = w;
    if (b->polyline) {
        /* The terminator (w & 0xF000F000) == 0x50005000 counts only after
         * two vertices, in a vertex position (flat: cmd v0 v1 v2 ..) or a
         * colour position (gouraud: c0 v0 c1 v1 c2 ..); a gouraud vertex
         * word that looks like one is still a vertex. */
        unsigned idx = b->have - 1u;
        int gouraud = (b->cmd[0] >> 24) & 0x10;
        int term_slot = gouraud ? (idx >= 4 && (idx & 1u) == 0) : idx >= 3;
        if (term_slot && (w & 0xf000f000u) == 0x50005000u) {
            run_command(b);
            b->have = 0;
        } else if (b->have >= 256) {
            b->have = 0;
            b->faulted = 1;
        }
        return 1;
    }
    if (b->have < b->need) return 1;
    if ((b->cmd[0] >> 24) >= 0xa0 && (b->cmd[0] >> 24) < 0xc0) {   /* CPU -> VRAM */
        size_t n;
        b->up_rect = xfer_rect(b->cmd[1], b->cmd[2]);
        n = (size_t)b->up_rect.w * b->up_rect.h;
        free(b->up_px);
        b->up_px = (uint16_t *)malloc((n + 1) * 2);
        if (!b->up_px) { b->faulted = 1; b->have = 0; return 0; }
        b->up_total = n;
        b->up_count = 0;
        b->stats.commands++;
        b->have = 0;
        return 1;
    }
    run_command(b);   /* uses cmd[0 .. have) */
    b->have = 0;
    return 1;
}

/* ---- GP1 ------------------------------------------------------------------ */

static int gp1(BfmGpuBridge *b, uint32_t w) {
    unsigned op = (w >> 24) & 0x3f;
    b->stats.gp1_words++;
    switch (op) {
    case 0x00:                            /* reset */
        bfm_gpu_bridge_flush(b);
        b->have = 0;
        b->up_total = 0;
        b->state = 0;
        b->e2 = b->e3 = b->e4 = b->e5 = 0;
        b->display_disabled = 1;
        b->gp1_mode = 0;
        b->gp1_start = 0;
        push_env(b);
        push_disp(b);
        break;
    case 0x01:                            /* reset command buffer */
        b->have = 0;
        b->up_total = 0;
        break;
    case 0x03: b->display_disabled = (int)(w & 1); break;
    case 0x05: b->gp1_start = w & 0x7ffff; push_disp(b); break;
    case 0x08: b->gp1_mode = w & 0xff; push_disp(b); break;
    case 0x10: {                          /* GPU info */
        unsigned what = w & 7;
        b->info_latch = what == 2 ? b->e2 : what == 3 ? b->e3 : what == 4 ? b->e4 :
                        what == 5 ? b->e5 : what == 7 ? 2u : 0u;
        b->info_pending = 1;
        break;
    }
    default: break;                       /* 02 ack, 04 dma dir, 06/07 ranges */
    }
    return 1;
}

int bfm_gpu_bridge_write32(BfmGpuBridge *b, uint32_t address, uint32_t value) {
    if (!b) return 0;
    if (address == BFM_GPU_BRIDGE_GP0) return gp0(b, value);
    if (address == BFM_GPU_BRIDGE_GP1) return gp1(b, value);
    return 0;
}

int bfm_gpu_bridge_tap(void *bridge, uint32_t address, uint32_t value) {
    return bfm_gpu_bridge_write32((BfmGpuBridge *)bridge, address, value);
}

int bfm_gpu_bridge_read32(BfmGpuBridge *b, uint32_t address, uint32_t *value) {
    if (!b || !value) return 0;
    if (address == BFM_GPU_BRIDGE_GP0) {  /* GPUREAD */
        if (b->rd_pos < b->rd_count) {
            uint32_t lo = b->rd_px[b->rd_pos++], hi = 0;
            if (b->rd_pos < b->rd_count) hi = b->rd_px[b->rd_pos++];
            *value = lo | (hi << 16);
        } else {
            *value = b->info_pending ? b->info_latch : 0;
        }
        return 1;
    }
    if (address == BFM_GPU_BRIDGE_GP1) {  /* GPUSTAT */
        uint32_t s = (uint32_t)(b->state & 0x7ff);
        s |= (uint32_t)((b->state >> 14) & 3) << 11;          /* mask bits */
        s |= (b->gp1_mode & 0x3f) << 17;                      /* display mode */
        s |= ((b->gp1_mode >> 6) & 1) << 16;
        s |= (uint32_t)b->display_disabled << 23;
        s |= 1u << 26 | 1u << 28;                             /* ready */
        if (b->rd_pos < b->rd_count) s |= 1u << 27;           /* VRAM->CPU ready */
        *value = s;
        return 1;
    }
    return 0;
}

int bfm_gpu_bridge_vblank(BfmGpuBridge *b) {
    int r;
    if (!b) return BFM_PLAT_INVALID;
    bfm_gpu_bridge_flush(b);
    r = bfm_plat_renderer_present();
    bfm_plat_renderer_begin_frame();
    b->stats.frames++;
    return r;
}
