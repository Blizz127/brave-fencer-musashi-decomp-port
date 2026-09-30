/* Tests for bfm_plat_gpu_bridge: synthetic GP0/GP1 streams through the
 * bridge into a reference renderer backend (a VRAM image drawn with
 * bfm_gl_ref_draw, recording every env/display/transfer/present call).
 * With -DBFM_BRIDGE_WITH_CONTROLLER (and native_boot's gpu_controller.c
 * linked in) the same streams also run through the controller's CPU
 * rasteriser and the two VRAM images are compared. Synthetic data only. */

#include "bfm_plat.h"
#include "bfm_plat_gpu_bridge.h"
#include "bfm_plat_renderer.h"
#include "backends/gl/bfm_gl_core.h"
#ifdef BFM_BRIDGE_WITH_CONTROLLER
#include "musashi_gpu_controller.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);               \
            fails++;                                                          \
        }                                                                     \
    } while (0)

/* ---- reference renderer backend ---------------------------------------- */

typedef struct RefBackend {
    BfmGlVram vram;
    BfmPlatDrawEnv env;
    BfmPlatDispEnv disp;
    int envs, disps, uploads, downloads, presents, submits;
    size_t prims;
    int kinds[BFM_PRIM_KIND_COUNT];
    BfmPlatPrim last[64];
    size_t nlast;
} RefBackend;
static RefBackend rb;

static int quad_as_tile(const BfmPlatPrim *p, BfmPlatPrim *t) {
    int x0 = p->v[0].x, y0 = p->v[0].y, x1 = p->v[3].x, y1 = p->v[3].y;
    if (p->kind != BFM_PRIM_POLY_F4 || p->v[1].x != x1 || p->v[1].y != y0 ||
        p->v[2].x != x0 || p->v[2].y != y1 || x1 <= x0 || y1 <= y0) return 0;
    *t = *p;
    t->kind = BFM_PRIM_TILE;
    t->w = (uint16_t)(x1 - x0);
    t->h = (uint16_t)(y1 - y0);
    return 1;
}

static int rb_init(void *s, const BfmPlatOutput *o) { (void)s; (void)o; return BFM_PLAT_OK; }
static int rb_env(void *s, const BfmPlatDrawEnv *e) { (void)s; rb.env = *e; rb.envs++; return 0; }
static int rb_disp(void *s, const BfmPlatDispEnv *e) { (void)s; rb.disp = *e; rb.disps++; return 0; }
static int rb_submit(void *s, const BfmPlatPrim *p, size_t n) {
    size_t i;
    (void)s;
    rb.submits++;
    for (i = 0; i < n; i++) {
        BfmPlatPrim q = p[i], t;
        rb.prims++;
        if (q.kind < BFM_PRIM_KIND_COUNT) rb.kinds[q.kind]++;
        if (rb.nlast < 64) rb.last[rb.nlast++] = q;
        if (quad_as_tile(&q, &t)) q = t;
        bfm_gl_ref_draw(&rb.vram, &rb.env, &q);
    }
    return 0;
}
static int rb_upload(void *s, const BfmPlatRect *r, const uint16_t *px) {
    (void)s;
    rb.uploads++;
    return bfm_gl_vram_upload(&rb.vram, r, px);
}
static int rb_download(void *s, const BfmPlatRect *r, uint16_t *px) {
    (void)s;
    rb.downloads++;
    return bfm_gl_vram_download(&rb.vram, r, px);
}
static int rb_present(void *s) { (void)s; rb.presents++; return 0; }

static const BfmPlatRendererBackend ref_backend = {
    "ref", rb_init, NULL, NULL, NULL, rb_env, rb_disp, rb_submit, rb_upload, NULL,
    rb_download, rb_present, NULL, &rb
};

/* ---- the synthetic stream ------------------------------------------------ */

typedef struct Stream { uint32_t addr[4096], word[4096]; unsigned n; } Stream;
static Stream st;
static void gp0(uint32_t w) { st.addr[st.n] = BFM_GPU_BRIDGE_GP0; st.word[st.n++] = w; }
static void gp1(uint32_t w) { st.addr[st.n] = BFM_GPU_BRIDGE_GP1; st.word[st.n++] = w; }

static uint16_t rgb15(int r, int g, int b) { return (uint16_t)(r | (g << 5) | (b << 10)); }

static void build_stream(void) {
    int i;
    st.n = 0;
    gp1(0x00000000u);                          /* reset */
    gp1(0x08000027u);                          /* 640x480 interlaced (hres 3, vres, interlace) */
    gp1(0x05000000u | (0u << 10) | 0u);        /* display origin 0,0 */
    gp0(0xE3000000u);                          /* drawing area 0,0 - 1023,511 */
    gp0(0xE4000000u | (511u << 10) | 1023u);
    gp0(0xE5000000u);                          /* offset 0,0 */
    /* A0: 5x2 texture at (640,0), 15-bit, odd pixel count (half-word pad) */
    gp0(0xA0000000u); gp0(0x00000280u); gp0(0x00020005u);
    {
        uint16_t t[10];
        for (i = 0; i < 10; i++) t[i] = (uint16_t)(i == 3 ? 0 : (rgb15(i * 3, 31 - i * 3, 16) | (i & 1 ? 0x8000 : 0)));
        for (i = 0; i < 10; i += 2) gp0((uint32_t)t[i] | ((uint32_t)(i + 1 < 10 ? t[i + 1] : 0) << 16));
    }
    /* A0: 16-entry CLUT at (0,500), 4-bit texture word row at (704,0) */
    gp0(0xA0000000u); gp0((500u << 16) | 0u); gp0(0x00010010u);
    for (i = 0; i < 16; i += 2)
        gp0((uint32_t)rgb15(i, 2 * i, 31 - i) | ((uint32_t)rgb15(i + 1, 2 * i + 2, 30 - i) << 16));
    gp0(0xA0000000u); gp0(0x000002C0u); gp0(0x00010002u);
    gp0(0x76543210u);                          /* texels 0..7 */
    /* backdrop, opaque tile, semi tile (abr 1), fade quad (abr 2), fill */
    gp0(0x02102030u); gp0(0x00000000u); gp0(0x00400080u);            /* FILL 128x64 */
    gp0(0x60FFFFFFu); gp0((16u << 16) | 0u); gp0(0x00100020u);       /* white 32x16 */
    gp0(0xE1000020u);                                                /* abr 1 */
    gp0(0x62404040u); gp0((16u << 16) | 8u); gp0(0x00080010u);       /* semi add */
    gp0(0xE1000040u);                                                /* abr 2 */
    gp0(0x2A404040u); gp0((32u << 16) | 0u); gp0((32u << 16) | 64u); /* fade quad */
    gp0((48u << 16) | 0u); gp0((48u << 16) | 64u);
    /* sprites: 0x65 raw, 0x64 neutral 0x808080, 0x64 dim 0x101010 (15-bit page 640) */
    gp0(0xE1000000u | (2u << 7) | (640u >> 6));
    gp0(0x65000000u); gp0((0u << 16) | 80u); gp0(0x00000000u); gp0(0x00020005u);
    gp0(0x64808080u); gp0((4u << 16) | 80u); gp0(0x00000000u); gp0(0x00020005u);
    gp0(0x64101010u); gp0((8u << 16) | 80u); gp0(0x00000000u); gp0(0x00020005u);
    /* 4-bit sprite from page 704 with CLUT (0,500) */
    gp0(0xE1000000u | (0u << 7) | (704u >> 6));
    gp0(0x65000000u); gp0((12u << 16) | 80u); gp0(((uint32_t)((500u << 6) | 0u) << 16)); gp0(0x00010008u);
    /* semi sprite (abr 0) with STP texels over the fill */
    gp0(0xE1000000u | (2u << 7) | (640u >> 6) | (0u << 5));
    gp0(0x67000000u); gp0((20u << 16) | 80u); gp0(0x00000000u); gp0(0x00020005u);
    /* VRAM->VRAM copy of the 32x16 white/semi area to (200,0) */
    gp0(0x80000000u); gp0((16u << 16) | 0u); gp0(200u); gp0(0x00100020u);
    /* offset + clip change, then a tile relative to it */
    gp0(0xE3000000u | (100u << 10) | 300u);
    gp0(0xE4000000u | (131u << 10) | 331u);
    gp0(0xE5000000u | ((uint32_t)(100u & 0x7ff) << 11) | (300u & 0x7ff));
    gp0(0x60FF00FFu); gp0(0xFFFCFFFCu); gp0(0x00400040u);            /* at -4,-4 64x64, clipped */
    gp0(0xE3000000u);
    gp0(0xE4000000u | (511u << 10) | 1023u);
    gp0(0xE5000000u);
    /* upload wrapping at the right edge */
    gp0(0xA0000000u); gp0(0x000003FEu | (400u << 16)); gp0(0x00010004u);
    gp0(0x00020001u); gp0(0x00040003u);
    /* polyline (flat) with terminator: 3 vertices -> 2 lines. Last, so a
     * GPU model without polylines can only misparse what follows it. */
    gp0(0x48FF0000u); gp0((60u << 16) | 0u); gp0((60u << 16) | 20u); gp0((62u << 16) | 20u);
    gp0(0x55555555u);
    /* texture window */
    gp0(0xE2000000u | (0x1Eu) | (0x1Eu << 5) | (0x02u << 10) | (0x02u << 15));
}

static void run_bridge(BfmGpuBridge *b) {
    unsigned i;
    for (i = 0; i < st.n; i++) CHECK(bfm_gpu_bridge_write32(b, st.addr[i], st.word[i]));
}

#ifdef BFM_BRIDGE_WITH_CONTROLLER
static uint16_t cvram[512][1024];
static int c_ok(void *u) { (void)u; return 1; }
static int c_word(void *u, uint32_t w) { (void)u; (void)w; return 1; }
static int c_en(void *u, int e) { (void)u; (void)e; return 1; }
static int c_disp(void *u, const MusashiGpuDisplayState *d) { (void)u; (void)d; return 1; }
static int c_fill(void *u, const MusashiGpuFill *f) {
    unsigned x, y;
    (void)u;
    for (y = 0; y < f->height; y++)
        for (x = 0; x < f->width; x++) cvram[(f->y + y) & 511][(f->x + x) & 1023] = f->color;
    return 1;
}
static int c_store(void *u, uint16_t x, uint16_t y, uint16_t p) {
    (void)u; cvram[y & 511][x & 1023] = p; return 1;
}
static int c_load(void *u, uint16_t x, uint16_t y, uint16_t *p) {
    (void)u; *p = cvram[y & 511][x & 1023]; return 1;
}

/* Counts differing pixels (colour bits) in a rect; prints the first few. */
static int compare_rect(const char *what, int x0, int y0, int w, int h, int report) {
    int x, y, bad = 0;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) {
            uint16_t a = rb.vram.px[y * 1024 + x] & 0x7fff, c = cvram[y][x] & 0x7fff;
            if (a != c && bad++ < 3 && report)
                printf("  %s (%d,%d): bridge+reference %04x controller %04x\n", what, x, y, a, c);
        }
    return bad;
}

static void run_controller(void) {
    static MusashiGpuController g;
    MusashiGpuBackend be;
    unsigned i;
    memset(&be, 0, sizeof be);
    be.reset = c_ok; be.draw_mode = c_word; be.display_enable = c_en; be.clear_fifo = c_ok;
    be.ready = c_ok; be.environment = c_word; be.display = c_disp; be.fill_vram = c_fill;
    be.store_vram = c_store; be.read_vram = c_load;
    CHECK(musashi_gpu_controller_init(&g, &be));
    for (i = 0; i < st.n; i++)
        if (!musashi_gpu_controller_write32(&g, st.addr[i], st.word[i]))
            printf("  controller refused word %u (%08x)\n", i, st.word[i]);
}

/* Does this controller build blend semi-transparent prims (port-int fix)? */
static int controller_blends(void) {
    return (cvram[16 + 4][8 + 4] & 0x7fff) != (rgb15(8, 8, 8));
}
/* Does it draw raw-texture rects (0x65) and modulate textured rects? */
static int controller_raw_rects(void) { return (cvram[0][81] & 0x7fff) != rgb15(6, 4, 2); }
static int controller_modulates(void) { return (cvram[8][81] & 0x7fff) != rgb15(3, 28, 16); }
static int controller_polylines(void) { return (cvram[60][10] & 0x7fff) != rgb15(6, 4, 2); }
#endif

int main(void) {
    static BfmGpuBridge b;
    BfmPlatOutput out;
    uint32_t v;
    unsigned i;
    memset(&out, 0, sizeof out);
    bfm_gl_vram_init(&rb.vram);
    /* the GPU is PS1-exact: compare with the integer modulation formula */
    bfm_gl_ref_set_ps1_modulation(1);
    CHECK(bfm_plat_renderer_register(&ref_backend) == BFM_PLAT_OK);
    CHECK(bfm_plat_renderer_open("ref", &out, NULL) == BFM_PLAT_OK);
    bfm_gpu_bridge_init(&b);
    build_stream();
    run_bridge(&b);
    CHECK(bfm_gpu_bridge_flush(&b) == BFM_PLAT_OK);   /* prims batch until a state change */

    /* protocol results */
    CHECK(rb.disp.disp.w == 640 && rb.disp.disp.h == 480 && rb.disp.interlaced == 1);
    CHECK(rb.kinds[BFM_PRIM_POLY_F4] == 1 && rb.kinds[BFM_PRIM_TILE] == 3 &&
          rb.kinds[BFM_PRIM_SPRITE] == 5 && rb.kinds[BFM_PRIM_FILL] == 1 &&
          rb.kinds[BFM_PRIM_LINE_F] == 2);
    CHECK(rb.uploads >= 5);            /* 3 textures + copy + wrap (2 pieces) */
    CHECK(b.stats.copies == 1 && b.stats.unknown == 0 && !b.faulted);
    /* texture window from E2 (mask 0x1e = 16-texel window, offset 2 -> 16) */
    CHECK(rb.env.texture_window.w == 16 && rb.env.texture_window.h == 16 &&
          rb.env.texture_window.x == 16 && rb.env.texture_window.y == 16);
    for (i = 0; i < rb.nlast; i++)
        if (rb.last[i].kind == BFM_PRIM_POLY_F4)
            CHECK((rb.last[i].flags & BFM_PRIM_FLAG_SEMI_TRANS) && ((rb.last[i].tpage >> 5) & 3) == 2);

    /* pixels, against the PS1 formulas */
    CHECK(rb.vram.px[0 * 1024 + 100] == rgb15(0x30 >> 3, 0x20 >> 3, 0x10 >> 3));   /* fill */
    CHECK((rb.vram.px[16 * 1024 + 30] & 0x7fff) == 0x7fff);                       /* white tile */
    CHECK((rb.vram.px[20 * 1024 + 12] & 0x7fff) == 0x7fff);   /* white + 8 (add) clamps */
    CHECK((rb.vram.px[34 * 1024 + 10] & 0x7fff) == rgb15(0, 0, 0));   /* fill - 8: floor 0 */
    CHECK((rb.vram.px[40 * 1024 + 50] & 0x7fff) == rgb15(0, 0, 0));   /* fill 6,4,2 - 8 */
    CHECK((rb.vram.px[40 * 1024 + 100] & 0x7fff) == rgb15(6, 4, 2));  /* outside the quad */
    /* raw sprite = texels; neutral 0x80 = texels; 0x10 = texel * 16 / 128 */
    CHECK((rb.vram.px[0 * 1024 + 81] & 0x7fff) == rgb15(3, 28, 16));
    CHECK((rb.vram.px[4 * 1024 + 81] & 0x7fff) == rgb15(3, 28, 16));
    CHECK((rb.vram.px[8 * 1024 + 81] & 0x7fff) == rgb15(0, 3, 2));
    CHECK((rb.vram.px[8 * 1024 + 80] & 0x7fff) == rgb15(0, 3, 2));   /* 31*16>>7 = 3, not 4 */
    CHECK(rb.vram.px[0 * 1024 + 83] == rgb15(6, 4, 2));  /* texel 3 transparent: fill shows */
    /* 4-bit sprite: CLUT index = texel nibble */
    CHECK((rb.vram.px[12 * 1024 + 85] & 0x7fff) == rgb15(5, 10, 26));   /* CLUT[5] */
    /* clipped tile inside the moved drawing area */
    CHECK(rb.vram.px[100 * 1024 + 300] == rgb15(31, 0, 31) && rb.vram.px[99 * 1024 + 300] == 0 &&
          rb.vram.px[131 * 1024 + 331] == rgb15(31, 0, 31) && rb.vram.px[132 * 1024 + 331] == 0);
    /* the copy and the wrapped upload */
    CHECK(rb.vram.px[0 * 1024 + 230] == rb.vram.px[16 * 1024 + 30] &&   /* copy to 200,0 */
          rb.vram.px[4 * 1024 + 212] == rb.vram.px[20 * 1024 + 12]);
    CHECK(rb.vram.px[400 * 1024 + 1022] == 1 && rb.vram.px[400 * 1024 + 1023] == 2 &&
          rb.vram.px[400 * 1024 + 0] == 3 && rb.vram.px[400 * 1024 + 1] == 4);
    /* C0 readback through GPUREAD */
    CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP0, 0xC0000000u));
    CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP0, (400u << 16) | 1022u));
    CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP0, 0x00010004u));
    CHECK(bfm_gpu_bridge_read32(&b, BFM_GPU_BRIDGE_GP1, &v) && (v & (1u << 27)));
    CHECK(bfm_gpu_bridge_read32(&b, BFM_GPU_BRIDGE_GP0, &v) && v == 0x00020001u);
    CHECK(bfm_gpu_bridge_read32(&b, BFM_GPU_BRIDGE_GP0, &v) && v == 0x00040003u);
    CHECK(bfm_gpu_bridge_read32(&b, BFM_GPU_BRIDGE_GP1, &v) && !(v & (1u << 27)));
    /* GP1 10 info: draw offset */
    CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP1, 0x10000007u));
    CHECK(bfm_gpu_bridge_read32(&b, BFM_GPU_BRIDGE_GP0, &v) && v == 2);
    /* tee-mode VRAM reads: the renderer's VRAM, refreshed after a draw */
    {
        uint16_t px = 0;
        int downloads = rb.downloads;
        CHECK(bfm_gpu_bridge_read_vram(&b, 30, 16, &px) && px == rb.vram.px[16 * 1024 + 30]);
        CHECK(bfm_gpu_bridge_read_vram(&b, 1022, 400, &px) && px == 1);
        CHECK(rb.downloads == downloads + 1);            /* one snapshot for both */
        CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP0, 0x60FFFFFFu));   /* white tile */
        CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP0, (500u << 16) | 900u));
        CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP0, 0x00010001u));
        CHECK(bfm_gpu_bridge_read_vram(&b, 900, 500, &px) && (px & 0x7fff) == 0x7fff);
        CHECK(rb.downloads == downloads + 2);            /* the draw made it stale */
    }
    /* GP1 03: display enable */
    CHECK(!bfm_gpu_bridge_display_enabled(&b));
    CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP1, 0x03000000u));
    CHECK(bfm_gpu_bridge_display_enabled(&b));
    /* gouraud polyline: a vertex word shaped like the terminator is still
     * a vertex; the terminator counts in a colour position */
    {
        size_t before = rb.prims;
        static const uint32_t pl[] = {0x58000010u, (70u << 16) | 0u, 0x00000020u, 0x50105010u,
                                      0x00000030u, (72u << 16) | 5u, 0x55555555u};
        unsigned k;
        for (k = 0; k < sizeof pl / sizeof pl[0]; k++)
            CHECK(bfm_gpu_bridge_write32(&b, BFM_GPU_BRIDGE_GP0, pl[k]));
        CHECK(bfm_gpu_bridge_flush(&b) == BFM_PLAT_OK && rb.prims == before + 2);
        CHECK(b.have == 0);   /* the terminator closed it; not waiting for more */
    }
    /* vblank presents */
    CHECK(bfm_gpu_bridge_vblank(&b) == BFM_PLAT_OK && rb.presents == 1 && b.stats.frames == 1);
    printf("bridge: %llu GP0 words, %llu commands, %zu prims in %d submits, %d envs, %d uploads\n",
           (unsigned long long)b.stats.gp0_words, (unsigned long long)b.stats.commands, rb.prims,
           rb.submits, rb.envs, rb.uploads);

#ifdef BFM_BRIDGE_WITH_CONTROLLER
    run_controller();
    {
        /* need: 0 always, 1 semi-transparency, 2 raw-texture rects,
         * 3 rect modulation, 4 polylines; a region whose feature the
         * controller lacks is reported, not asserted */
        typedef struct Region { const char *name; int x, y, w, h, semi, need; } Region;
        /* gap: a known difference of native-lane's gpu_controller (reported,
         * not asserted): raw-texture rects 0x65/67/.. are not drawn;
         * textured-rect colour modulation is not applied; polylines are
         * unhandled */
        static const Region regions[] = {
            /* disjoint regions; semi = pixels produced by blending */
            {"fill", 32, 0, 48, 16, 0, 0},
            {"fill (right of sprites)", 96, 0, 32, 64, 0, 0},
            {"opaque tile", 0, 24, 32, 8, 0, 0},
            {"semi tile (abr 1)", 8, 16, 16, 8, 1, 0},
            {"fade quad 0x2A (abr 2)", 0, 32, 64, 16, 1, 0},
            {"neutral 0x64 sprite", 80, 4, 5, 2, 0, 0},
            {"VRAM copy (opaque part)", 200, 8, 32, 8, 0, 0},
            {"VRAM copy (blended part)", 200, 0, 32, 8, 1, 0},
            {"clipped tile", 296, 96, 40, 40, 0, 0},
            {"wrapped upload", 1020, 400, 4, 1, 0, 0},
            {"wrapped upload (wrap)", 0, 400, 2, 1, 0, 0},
            {"raw 0x65 sprite", 80, 0, 5, 2, 0, 2},
            {"raw 0x65 4-bit sprite", 80, 12, 8, 1, 0, 2},
            {"raw semi 0x67 sprite", 80, 20, 5, 2, 1, 2},
            {"modulated 0x64 0x101010", 80, 8, 5, 2, 0, 3},
            {"polyline 0x48", 0, 58, 24, 7, 0, 4},
        };
        int has[5], r;
        static const char *what[5] = {"", "semi-transparency", "raw-texture rects",
                                      "rect modulation", "polylines"};
        has[0] = 1;
        has[1] = controller_blends();
        has[2] = controller_raw_rects();
        has[3] = controller_modulates();
        has[4] = controller_polylines();
        printf("controller: semi-transparency %s, raw-texture rects %s, rect modulation %s, "
               "polylines %s\n", has[1] ? "yes" : "no", has[2] ? "yes" : "no",
               has[3] ? "yes" : "no", has[4] ? "yes" : "no");
        for (r = 0; r < (int)(sizeof regions / sizeof regions[0]); r++) {
            const Region *g = &regions[r];
            int d = compare_rect(g->name, g->x, g->y, g->w, g->h, 0);
            int ok = has[g->need] && (!g->semi || has[1]);
            char verdict[96];
            if (!d) snprintf(verdict, sizeof verdict, "match");
            else if (!ok)
                snprintf(verdict, sizeof verdict, "differ (controller lacks %s)",
                         !has[g->need] ? what[g->need] : what[1]);
            else snprintf(verdict, sizeof verdict, "DIFFER");
            printf("  %-26s %4d px differ  %s\n", g->name, d, verdict);
            if (ok) CHECK(d == 0);
        }
    }
#endif
    bfm_gpu_bridge_free(&b);
    bfm_plat_renderer_close();
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("ok bridge\n");
    return 0;
}
