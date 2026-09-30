/* Tests for the `gl` renderer backend.
 *
 *   bfm_gl_probe core   CPU side only (bfm_gl_core.c): CLUT decode, sampler,
 *                       modulation, PS1 blend formulas vs the GL blend
 *                       state, dither, batching, hor+ routing, HD
 *                       replacement lookup, VRAM mirror. Headless-safe.
 *   bfm_gl_probe exec LIB
 *                       The real GL executor (bfm_gl_exec.c) on an OSMesa
 *                       GL 3.3 core context (LIB = libOSMesa path, dlopened),
 *                       read back and compared with the CPU reference. Built
 *                       only with -DBFM_GL_PROBE_EXEC; needs no display.
 * Synthetic data only. */

#include "backends/gl/bfm_gl_core.h"
#ifdef BFM_GL_PROBE_EXEC
#include "backends/gl/bfm_gl_exec.h"
#include <dlfcn.h>
#endif
#include "bfm_plat_font.h"

#include <math.h>
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

static BfmGlVram vram;

static uint16_t rgb15(int r, int g, int b) {
    return (uint16_t)(r | (g << 5) | (b << 10));
}
static uint16_t tp(int depth_code, int abr, int x, int y) {
    return (uint16_t)(((depth_code & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) |
                      ((x & 0x3FF) >> 6));
}
static uint16_t clut_of(int x, int y) { return (uint16_t)((y << 6) | (x >> 4)); }
static void poke(int x, int y, uint16_t v) { vram.px[y * BFM_GL_VRAM_W + x] = v; }
static BfmPlatRect R(int x, int y, int w, int h) {
    BfmPlatRect r;
    r.x = (int16_t)x; r.y = (int16_t)y; r.w = (int16_t)w; r.h = (int16_t)h;
    return r;
}
static int rect_is(const BfmPlatRect *r, int x, int y, int w, int h) {
    return r->x == x && r->y == y && r->w == w && r->h == h;
}
static BfmPlatPrim prim(int kind) {
    BfmPlatPrim p;
    memset(&p, 0, sizeof p);
    p.kind = (uint8_t)kind;
    return p;
}

/* ---------------------------------------------------------------- core */

static void test_decode(void) {
    BfmGlTpage t = bfm_gl_decode_tpage(tp(2, 1, 640, 256));
    int x, y;
    CHECK(tp(2, 1, 640, 256) == 0x13A);
    CHECK(t.base_x == 640 && t.base_y == 256 && t.depth == 15 && t.abr == 1);
    t = bfm_gl_decode_tpage(0);
    CHECK(t.base_x == 0 && t.base_y == 0 && t.depth == 4 && t.abr == 0);
    t = bfm_gl_decode_tpage(tp(1, 3, 960, 0));
    CHECK(t.base_x == 960 && t.depth == 8 && t.abr == 3);
    CHECK(clut_of(320, 480) == 0x7814);
    bfm_gl_decode_clut(0x7814, &x, &y);
    CHECK(x == 320 && y == 480);
}

static void test_sampler(void) {
    uint16_t t4 = tp(0, 0, 192, 0), t8 = tp(1, 0, 256, 0), t15 = tp(2, 0, 320, 0);
    uint16_t cl = clut_of(320, 480);
    BfmPlatRect win = R(32, 0, 16, 0);
    int i;
    bfm_gl_vram_init(&vram);
    /* 4-bit: word 0xFEDC at page x 1 -> u 4..7 = C D E F */
    poke(192 + 1, 5, 0xFEDC);
    for (i = 0; i < 16; i++) poke(320 + i, 480, (uint16_t)(0x8000 | (i * 0x421)));
    CHECK(bfm_gl_sample(&vram, t4, cl, 4, 5, NULL) == (0x8000 | 0xC * 0x421));
    CHECK(bfm_gl_sample(&vram, t4, cl, 5, 5, NULL) == (0x8000 | 0xD * 0x421));
    CHECK(bfm_gl_sample(&vram, t4, cl, 7, 5, NULL) == (0x8000 | 0xF * 0x421));
    CHECK(bfm_gl_sample(&vram, t4, cl, 4 + 256, 5 + 256, NULL) == (0x8000 | 0xC * 0x421));
    /* 8-bit: word 0xABCD at page x 3 -> u 6 = CD, u 7 = AB */
    poke(256 + 3, 7, 0xABCD);
    poke(320 + 0xCD, 480, 0x1234);
    poke(320 + 0xAB, 480, 0x0777);
    CHECK(bfm_gl_sample(&vram, t8, cl, 6, 7, NULL) == 0x1234);
    CHECK(bfm_gl_sample(&vram, t8, cl, 7, 7, NULL) == 0x0777);
    /* 15-bit direct, and the texture window */
    poke(320 + 37, 9, 0x4321);
    CHECK(bfm_gl_sample(&vram, t15, 0, 37, 9, NULL) == 0x4321);
    CHECK(bfm_gl_window(5, 32, 16) == 37 && bfm_gl_window(21, 32, 16) == 37);
    CHECK(bfm_gl_window(200, 32, 0) == 200);
    CHECK(bfm_gl_sample(&vram, t15, 0, 21, 9, &win) == 0x4321);
    CHECK(bfm_gl_sample(&vram, t15, 0, 22, 9, NULL) == 0);   /* transparent */
    printf("sampler 4/8/15-bit ok\n");
}

static void test_modulate(void) {
    uint8_t o[3];
    int t, c, worst = 0, exact = 0, n = 0;
    bfm_gl_modulate(rgb15(31, 16, 0), 128, 128, 128, 0, o);
    CHECK(o[0] == 255 && o[1] == 132 && o[2] == 0);
    bfm_gl_modulate(rgb15(31, 16, 0), 255, 255, 64, 0, o);
    CHECK(o[0] == 255 && o[1] == 255 && o[2] == 0);
    bfm_gl_modulate(rgb15(31, 16, 2), 0, 0, 0, 1, o);
    CHECK(o[0] == 255 && o[1] == 132 && o[2] == 16);
    /* vs the PS1 (t * c) >> 7 in 5-bit units */
    for (t = 0; t < 32; t++)
        for (c = 0; c < 256; c++) {
            int ps1 = (t * c) >> 7, d;
            if (ps1 > 31) ps1 = 31;
            bfm_gl_modulate((uint16_t)t, c, 0, 0, 0, o);
            d = abs((o[0] >> 3) - ps1);
            if (d > worst) worst = d;
            exact += d == 0;
            n++;
        }
    CHECK(worst <= 1);
    printf("modulate vs PS1: %d/%d exact, worst %d\n", exact, n, worst);
}

static void test_blend(void) {
    BfmGlBlendState s;
    int m, b, f;
    CHECK(bfm_gl_blend_ref(0, rgb15(10, 0, 31), rgb15(21, 0, 31)) == rgb15(15, 0, 31));
    CHECK(bfm_gl_blend_ref(1, rgb15(20, 3, 0), rgb15(20, 4, 0)) == rgb15(31, 7, 0));
    CHECK(bfm_gl_blend_ref(2, rgb15(5, 20, 9), rgb15(9, 9, 9)) == rgb15(0, 11, 0));
    CHECK(bfm_gl_blend_ref(3, rgb15(10, 30, 0), rgb15(13, 31, 3)) == rgb15(13, 31, 0));
    s = bfm_gl_blend_state(0);
    CHECK(s.equation == BFM_GL_FUNC_ADD && s.src == BFM_GL_CONSTANT_COLOR &&
          s.dst == BFM_GL_CONSTANT_COLOR && s.constant == 0.5f);
    s = bfm_gl_blend_state(1);
    CHECK(s.equation == BFM_GL_FUNC_ADD && s.src == BFM_GL_ONE && s.dst == BFM_GL_ONE);
    s = bfm_gl_blend_state(2);
    CHECK(s.equation == BFM_GL_FUNC_REVERSE_SUBTRACT && s.src == BFM_GL_ONE &&
          s.dst == BFM_GL_ONE);
    s = bfm_gl_blend_state(3);
    CHECK(s.equation == BFM_GL_FUNC_ADD && s.src == BFM_GL_CONSTANT_COLOR &&
          s.constant == 0.25f && s.dst == BFM_GL_ONE);
    /* the GL state, evaluated, against the PS1 formula for every 5-bit pair */
    for (m = 0; m < 4; m++) {
        int exact = 0, worst = 0;
        s = bfm_gl_blend_state(m);
        for (b = 0; b < 32; b++)
            for (f = 0; f < 32; f++) {
                uint16_t bb = rgb15(b, 31 - b, b), ff = rgb15(f, f, 31 - f);
                uint16_t ref = bfm_gl_blend_ref(m, bb, ff);
                uint16_t gl = bfm_gl_blend_emulate(&s, bb, ff);
                int ch, d = 0;
                for (ch = 0; ch < 3; ch++) {
                    int e = abs(((ref >> (5 * ch)) & 31) - ((gl >> (5 * ch)) & 31));
                    if (e > d) d = e;
                }
                exact += d == 0;
                if (d > worst) worst = d;
            }
        /* add/subtract are exact; the halving/quartering modes keep the
         * fraction the PS1 truncates, so they may round up by one step */
        CHECK(worst <= 1);
        if (m == 1 || m == 2) CHECK(exact == 1024);
        printf("blend mode %d: %d/1024 exact vs PS1, worst %d\n", m, exact, worst);
    }
}

static void test_dither(void) {
    int sum = 0, i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) sum += bfm_gl_dither_matrix[i][j];
    CHECK(sum == -8);
    CHECK(bfm_gl_dither_matrix[0][0] == -4 && bfm_gl_dither_matrix[1][2] == 3 &&
          bfm_gl_dither_matrix[3][0] == 3 && bfm_gl_dither_matrix[2][2] == -4);
    CHECK(bfm_gl_dither5(100, 0, 0) == 12);   /* 96  */
    CHECK(bfm_gl_dither5(104, 0, 0) == 12);   /* 100 */
    CHECK(bfm_gl_dither5(104, 2, 1) == 13);   /* 107 */
    CHECK(bfm_gl_dither5(2, 0, 0) == 0);      /* clamps at 0 */
    CHECK(bfm_gl_dither5(254, 2, 1) == 31);   /* clamps at 255 */
    CHECK(bfm_gl_dither5(104, 6, 5) == bfm_gl_dither5(104, 2, 1));  /* 4x4 tile */
}

static void test_vram(void) {
    static uint16_t in[64], out[64];
    BfmPlatRect r = R(100, 200, 8, 8), row = R(10, 0, 8, 1);
    int i;
    bfm_gl_vram_init(&vram);
    for (i = 0; i < 64; i++) in[i] = (uint16_t)(i * 97);
    CHECK(bfm_gl_vram_upload(&vram, &r, in) == BFM_PLAT_OK);
    CHECK(vram.dirty && vram.dx0 == 100 && vram.dy0 == 200 && vram.dx1 == 108 && vram.dy1 == 208);
    CHECK(bfm_gl_vram_download(&vram, &r, out) == BFM_PLAT_OK && memcmp(in, out, sizeof in) == 0);
    r = R(1020, 0, 8, 1);
    CHECK(bfm_gl_vram_upload(&vram, &r, in) == BFM_PLAT_INVALID);   /* outside */
    /* overlapping move reads the whole source first */
    for (i = 0; i < 8; i++) poke(10 + i, 0, (uint16_t)(i + 1));
    CHECK(bfm_gl_vram_move(&vram, &row, 11, 0) == BFM_PLAT_OK);
    CHECK(vram.px[10] == 1 && vram.px[11] == 1 && vram.px[12] == 2 && vram.px[18] == 8);
    /* move wraps at the right edge */
    CHECK(bfm_gl_vram_move(&vram, &row, 1020, 3) == BFM_PLAT_OK);
    /* source row is now 1 1 2 3 4 5 6 7 */
    CHECK(vram.px[3 * 1024 + 1020] == 1 && vram.px[3 * 1024 + 1023] == 3 &&
          vram.px[3 * 1024 + 0] == 4 && vram.px[3 * 1024 + 3] == 7);
    r = R(0, 100, 4, 2);
    CHECK(bfm_gl_vram_fill(&vram, &r, 0x7FFF) == BFM_PLAT_OK);
    CHECK(vram.px[101 * 1024 + 3] == 0x7FFF && vram.px[101 * 1024 + 4] == 0);
}

static void test_rgb24(void) {
    /* two pixels (R,G,B) = (0x11,0x22,0x33), (0x44,0x55,0x66) packed into
     * halfwords 0x2211 0x4433 0x6655 at (100, 7) */
    BfmPlatRect d = R(100, 7, 2, 1);
    uint8_t out[8];
    bfm_gl_vram_init(&vram);
    poke(100, 7, 0x2211);
    poke(101, 7, 0x4433);
    poke(102, 7, 0x6655);
    bfm_gl_decode_rgb24(&vram, &d, out);
    CHECK(out[0] == 0x11 && out[1] == 0x22 && out[2] == 0x33 && out[3] == 255);
    CHECK(out[4] == 0x44 && out[5] == 0x55 && out[6] == 0x66 && out[7] == 255);
}

static void test_batch(void) {
    BfmGlBatch b;
    BfmPlatDrawEnv env;
    BfmPlatPrim p[8];
    const BfmGlVertex *v;
    size_t tris;
    bfm_gl_vram_init(&vram);
    bfm_gl_batch_init(&b);
    b.vram = &vram;
    memset(&env, 0, sizeof env);
    env.clip = R(0, 0, 1024, 512);
    env.offset_x = 5;
    env.offset_y = 6;
    env.dither = 1;
    bfm_gl_batch_set_env(&b, &env);
    CHECK(b.target == BFM_GL_TARGET_VRAM);

    p[0] = prim(BFM_PRIM_POLY_F4);
    p[0].v[0].r = 10; p[0].v[1].r = 99;   /* flat: v0's colour everywhere */
    p[1] = prim(BFM_PRIM_POLY_G3);
    p[1].v[1].g = 77;
    tris = bfm_gl_batch_add(&b, p, 2);
    CHECK(tris == 3 && b.nv == 9 && b.nc == 1);           /* merged */
    CHECK(b.v[1].r == 10 && b.v[4].r == 10);             /* flat colour */
    CHECK(b.v[7].g == 77);                               /* gouraud kept */
    CHECK(!(b.v[0].flags & BFM_GLV_DITHER) && (b.v[6].flags & BFM_GLV_DITHER));
    CHECK(b.v[0].x == 5 && b.v[0].y == 6);               /* env offset */

    p[0] = prim(BFM_PRIM_POLY_F3);
    p[0].flags = BFM_PRIM_FLAG_SEMI_TRANS;
    p[0].tpage = tp(2, 1, 0, 0);
    p[1] = prim(BFM_PRIM_POLY_FT4);
    p[1].flags = BFM_PRIM_FLAG_SEMI_TRANS;
    p[1].tpage = tp(0, 2, 640, 0);
    p[1].clut = clut_of(0, 480);
    tris = bfm_gl_batch_add(&b, p, 2);
    /* opaque and semi mode 1 share a draw (dual-source factors per
     * fragment); mode 2 needs its own, split by STP when textured */
    CHECK(tris == 3 && b.nc == 2);
    CHECK(b.c[0].blend == BFM_GL_BLEND_DUAL && b.c[0].count == 12 && !b.c[0].split_stp);
    CHECK(b.v[9].flags == (BFM_GLV_SEMI | (1u << BFM_GLV_MODE_SHIFT)));
    CHECK(!(b.v[0].flags & BFM_GLV_SEMI));
    CHECK(b.c[1].blend == 2 && b.c[1].split_stp);
    CHECK((b.v[9 + 3].flags & BFM_GLV_TEXTURED) && (b.v[9 + 3].flags & BFM_GLV_DITHER));
    CHECK(b.v[12].tpage == p[1].tpage && b.v[12].clut == p[1].clut);

    /* sprite: rect, uv span, raw, never dithered */
    p[0] = prim(BFM_PRIM_SPRITE);
    p[0].flags = BFM_PRIM_FLAG_RAW_TEXTURE;
    p[0].v[0].x = 100; p[0].v[0].y = 50; p[0].v[0].u = 10; p[0].v[0].v = 20;
    p[0].w = 16; p[0].h = 8;
    bfm_gl_batch_add(&b, p, 1);
    v = &b.v[b.nv - 6];
    CHECK(b.c[b.nc - 1].blend == BFM_GL_BLEND_DUAL && b.c[b.nc - 1].count == 6);
    CHECK(v[0].x == 105 && v[0].y == 56 && v[0].u == 10 && v[0].v == 20);
    CHECK(v[1].x == 121 && v[1].u == 26 && v[2].y == 64 && v[2].v == 28);
    CHECK((v[0].flags & BFM_GLV_RAW) && !(v[0].flags & BFM_GLV_DITHER));

    /* lines become one-pixel quads, endpoints inclusive */
    env.offset_x = env.offset_y = 0;
    bfm_gl_batch_set_env(&b, &env);
    p[0] = prim(BFM_PRIM_LINE_F);
    p[0].v[0].x = 9; p[0].v[1].x = 0;   /* drawn right to left */
    p[0].v[0].r = 200;
    bfm_gl_batch_add(&b, p, 1);
    v = &b.v[b.nv - 6];
    CHECK(v[0].x == 0 && v[0].y == 0 && v[1].x == 10 && v[1].y == 0 && v[2].y == 1);
    CHECK(v[0].r == 200 && v[1].r == 200);
    p[0] = prim(BFM_PRIM_LINE_G);
    p[0].v[0].x = 3; p[0].v[0].y = 2; p[0].v[0].g = 50;
    p[0].v[1].x = 4; p[0].v[1].y = 12; p[0].v[1].g = 250;
    bfm_gl_batch_add(&b, p, 1);
    v = &b.v[b.nv - 6];
    CHECK(v[0].x == 3 && v[0].y == 2 && v[1].x == 4 && v[1].g == 50);
    CHECK(v[4].x == 5 && v[4].y == 13 && v[4].g == 250 && v[5].x == 4);  /* k1 k3 k2 */

    /* fill: mirror + a FILL command, not subject to the env */
    p[0] = prim(BFM_PRIM_FILL);
    p[0].v[0].x = 16; p[0].v[0].y = 300; p[0].w = 4; p[0].h = 2;
    p[0].v[0].r = 0xF8; p[0].v[0].g = 0x08; p[0].v[0].b = 0x80;
    bfm_gl_batch_add(&b, p, 1);
    CHECK(b.c[b.nc - 1].kind == BFM_GL_CMD_FILL && b.c[b.nc - 1].rgb == 0x8008F8);
    CHECK(rect_is(&b.c[b.nc - 1].rect, 16, 300, 4, 2));
    CHECK(vram.px[301 * 1024 + 19] == rgb15(31, 1, 16));

    bfm_gl_batch_clear(&b);
    CHECK(b.nv == 0 && b.nc == 0);
    bfm_gl_batch_free(&b);
    printf("batching ok\n");
}

static void test_scenes(void) {
    BfmGlBatch b;
    BfmPlatDrawEnv env;
    BfmPlatRect disp = R(0, 0, 320, 240);
    BfmPlatPrim p = prim(BFM_PRIM_TILE);
    int w, h, i, fills = 0;
    bfm_gl_vram_init(&vram);
    bfm_gl_batch_init(&b);
    b.vram = &vram;
    bfm_gl_batch_set_display(&b, &disp, 53);
    CHECK(bfm_gl_batch_scene_at(&b, 0, 0) == 0);
    bfm_gl_batch_target_size(&b, BFM_GL_TARGET_SCENE0, &w, &h);
    CHECK(w == 426 && h == 240);

    memset(&env, 0, sizeof env);
    env.clip = R(0, 240, 320, 240);     /* the back buffer */
    bfm_gl_batch_set_env(&b, &env);
    CHECK(b.target == BFM_GL_TARGET_SCENE1 && bfm_gl_batch_scene_at(&b, 0, 240) == 1);
    CHECK(b.org_x == -53 && b.org_y == 240);
    CHECK(rect_is(&b.scissor, 0, 0, 426, 240));   /* widened both sides */
    env.offset_y = 240;
    bfm_gl_batch_set_env(&b, &env);
    p.v[0].x = -40; p.v[0].y = 10; p.w = 20; p.h = 5;   /* left of the 4:3 edge */
    bfm_gl_batch_add(&b, &p, 1);
    CHECK(b.v[0].x == 13 && b.v[0].y == 10);

    env.clip = R(10, 250, 100, 50);     /* a viewport inside the buffer */
    bfm_gl_batch_set_env(&b, &env);
    CHECK(b.target == BFM_GL_TARGET_SCENE1 && rect_is(&b.scissor, 63, 10, 100, 50));
    env.clip = R(0, 240, 100, 50);      /* touching the left edge */
    bfm_gl_batch_set_env(&b, &env);
    CHECK(rect_is(&b.scissor, 0, 0, 153, 50));
    env.clip = R(220, 240, 100, 50);    /* touching the right edge */
    bfm_gl_batch_set_env(&b, &env);
    CHECK(rect_is(&b.scissor, 273, 0, 153, 50));

    /* clear_bg fills the whole wide buffer and the mirror */
    bfm_gl_batch_clear(&b);
    env.clip = R(0, 240, 320, 240);
    env.clear_bg = 1;
    env.bg_r = 8; env.bg_g = 16; env.bg_b = 24;
    bfm_gl_batch_set_env(&b, &env);
    for (i = 0; i < (int)b.nc; i++) {
        if (b.c[i].kind != BFM_GL_CMD_FILL) continue;
        fills++;
        if (b.c[i].target == BFM_GL_TARGET_SCENE1)
            CHECK(rect_is(&b.c[i].rect, 0, 0, 426, 240));
        if (b.c[i].target == BFM_GL_TARGET_VRAM)
            CHECK(rect_is(&b.c[i].rect, 0, 240, 320, 240));
    }
    CHECK(fills == 2 && vram.px[300 * 1024 + 5] == rgb15(1, 2, 3));
    env.clear_bg = 0;

    /* off-screen draw areas go to the VRAM target */
    env.clip = R(512, 0, 256, 256);
    bfm_gl_batch_set_env(&b, &env);
    CHECK(b.target == BFM_GL_TARGET_VRAM && rect_is(&b.scissor, 512, 0, 256, 256));

    /* a copy lands in VRAM and in every buffer it overlaps */
    bfm_gl_batch_clear(&b);
    {
        BfmPlatRect up = R(300, 200, 40, 60);   /* straddles both buffers */
        int vr = 0, s0 = 0, s1 = 0;
        bfm_gl_batch_add_copy(&b, &up);
        for (i = 0; i < (int)b.nc; i++) {
            CHECK(b.c[i].copy && b.c[i].blend == -1);
            vr += b.c[i].target == BFM_GL_TARGET_VRAM;
            s0 += b.c[i].target == BFM_GL_TARGET_SCENE0;
            s1 += b.c[i].target == BFM_GL_TARGET_SCENE1;
            if (b.c[i].target != BFM_GL_TARGET_VRAM)
                CHECK(rect_is(&b.c[i].rect, 53, 0, 320, 240));
        }
        CHECK(vr == 1 && s0 == 1 && s1 == 1 && b.nv == 18);
        CHECK((b.v[0].flags & BFM_GLV_ABS) && b.v[0].u == 300 && b.v[0].v == 200);
        CHECK(b.v[6].x == 353 && b.v[6].y == 200);           /* scene 0 local */
        CHECK(b.v[12].x == 353 && b.v[12].y == -40);         /* scene 1 local */
    }

    /* a third display-sized buffer evicts the least recently used slot */
    env.clip = R(640, 0, 320, 240);
    bfm_gl_batch_set_env(&b, &env);
    CHECK(bfm_gl_batch_scene_at(&b, 640, 0) == 0 && bfm_gl_batch_scene_at(&b, 0, 0) < 0);
    /* a new display size forgets the slots */
    disp = R(0, 0, 256, 240);
    bfm_gl_batch_set_display(&b, &disp, 43);
    CHECK(bfm_gl_batch_scene_at(&b, 640, 0) < 0 && bfm_gl_batch_scene_at(&b, 0, 0) == 0);
    bfm_gl_batch_free(&b);
    printf("hor+ scene routing ok\n");
}

static void test_replacements(void) {
    static BfmGlReplacements set;
    BfmGlBatch b;
    BfmPlatRect r = R(640, 0, 16, 64), other = R(700, 0, 8, 8), hit = R(650, 10, 1, 1);
    BfmPlatPrim p = prim(BFM_PRIM_SPRITE);
    uint32_t old = 1, dead[4];
    float s, t;
    int i;
    memset(&set, 0, sizeof set);
    CHECK(bfm_gl_repl_add(&set, &r, 64, 256, 11, &old) == 0 && old == 0);
    CHECK(bfm_gl_repl_add(&set, &other, 32, 32, 12, &old) == 1);
    CHECK(bfm_gl_repl_add(&set, &r, 128, 512, 13, &old) == 0 && old == 11);
    /* 15-bit page at 640: u 0..16 fits, 0..17 does not */
    p.tpage = tp(2, 0, 640, 0);
    p.w = 16; p.h = 64;
    CHECK(bfm_gl_repl_find(&set, &p) == 0);
    p.w = 17;
    CHECK(bfm_gl_repl_find(&set, &p) < 0);
    /* 4-bit page: 64 texels = 16 VRAM columns */
    p.tpage = tp(0, 0, 640, 0);
    p.w = 64;
    CHECK(bfm_gl_repl_find(&set, &p) == 0);
    p.w = 65;
    CHECK(bfm_gl_repl_find(&set, &p) < 0);
    p.kind = BFM_PRIM_POLY_F4;
    CHECK(bfm_gl_repl_find(&set, &p) < 0);     /* untextured */
    bfm_gl_repl_coords(&set.r[0], tp(0, 0, 640, 0), 32, 16, &s, &t);
    CHECK(fabsf(s - 0.5f) < 1e-6f && fabsf(t - 0.25f) < 1e-6f);
    bfm_gl_repl_coords(&set.r[0], tp(2, 0, 640, 0), 8, 64, &s, &t);
    CHECK(fabsf(s - 0.5f) < 1e-6f && fabsf(t - 1.0f) < 1e-6f);

    /* batched: the replacement rides on the command, STP split is off */
    bfm_gl_batch_init(&b);
    b.repl = &set;
    p = prim(BFM_PRIM_SPRITE);
    p.tpage = tp(0, 0, 640, 0);
    p.flags = BFM_PRIM_FLAG_SEMI_TRANS;
    p.w = 64; p.h = 64;
    bfm_gl_batch_add(&b, &p, 1);
    CHECK(b.nc == 1 && b.c[0].repl == 1 && !b.c[0].split_stp &&
          b.c[0].blend == BFM_GL_BLEND_DUAL && (b.v[0].flags & BFM_GLV_SEMI));
    CHECK(b.v[0].repl == 1 && b.v[1].s == 1.0f && b.v[2].t == 1.0f);
    bfm_gl_batch_free(&b);

    /* a plain upload over it kills it */
    CHECK(bfm_gl_repl_invalidate(&set, &hit, dead, 4) == 1 && dead[0] == 13);
    CHECK(!set.r[0].live && set.r[1].live);
    p.kind = BFM_PRIM_SPRITE;
    CHECK(bfm_gl_repl_find(&set, &p) < 0);
    for (i = 2; i < BFM_GL_MAX_REPLACEMENTS + 1; i++) {
        BfmPlatRect q = R(i % 60 * 16, 256 + i / 60, 1, 1);
        (void)q;
    }
    printf("replacements ok\n");
}

static int count_kind(const BfmGlBatch *b, int kind) {
    size_t i;
    int n = 0;
    for (i = 0; i < b->nc; i++) n += b->c[i].kind == kind;
    return n;
}

static void test_ref_mask(void) {
    BfmPlatDrawEnv env;
    BfmPlatPrim p = prim(BFM_PRIM_TILE);
    uint16_t tex[4] = {0x001F, 0x83E0, 0x0000, 0xFC00};
    BfmPlatRect tr = R(640, 0, 4, 1);
    bfm_gl_vram_init(&vram);
    memset(&env, 0, sizeof env);
    env.clip = R(0, 0, 1024, 512);
    /* mask-set tile */
    p.v[0].x = 0; p.v[0].y = 0; p.w = 4; p.h = 1;
    p.v[0].r = 0xF8;
    p.flags = BFM_PRIM_FLAG_MASK_SET;
    CHECK(bfm_gl_ref_draw(&vram, &env, &p) == 4 && vram.px[0] == 0x801F);
    /* check: protected pixels untouched */
    p.v[0].x = 2; p.w = 4; p.v[0].r = 0; p.v[0].g = 0xF8;
    p.flags = BFM_PRIM_FLAG_MASK_CHECK;
    CHECK(bfm_gl_ref_draw(&vram, &env, &p) == 2);
    CHECK(vram.px[2] == 0x801F && vram.px[3] == 0x801F && vram.px[4] == 0x03E0);
    /* textured semi: only STP texels blend (mode 1), 0 is transparent,
     * written bit 15 = texel STP */
    bfm_gl_vram_upload(&vram, &tr, tex);
    p = prim(BFM_PRIM_SPRITE);
    p.v[0].x = 10; p.v[0].y = 0; p.w = 4; p.h = 1;
    p.tpage = tp(2, 1, 640, 0);
    p.flags = BFM_PRIM_FLAG_SEMI_TRANS | BFM_PRIM_FLAG_RAW_TEXTURE;
    vram.px[10] = vram.px[11] = vram.px[12] = vram.px[13] = rgb15(1, 1, 1);
    CHECK(bfm_gl_ref_draw(&vram, &env, &p) == 3);
    CHECK(vram.px[10] == 0x001F);                     /* opaque, no STP */
    CHECK(vram.px[11] == (0x8000 | rgb15(1, 31, 1))); /* blended, STP kept */
    CHECK(vram.px[12] == rgb15(1, 1, 1));             /* transparent */
    CHECK(vram.px[13] == (0x8000 | rgb15(1, 1, 31)));
    /* fills clear bit 15 */
    p = prim(BFM_PRIM_FILL);
    p.w = 16; p.h = 1;
    bfm_gl_ref_draw(&vram, &env, &p);
    CHECK(vram.px[0] == 0 && vram.px[11] == 0);
    printf("reference rasteriser mask/STP ok\n");
}

static void test_feedback_core(void) {
    BfmGlBatch b;
    BfmPlatDrawEnv env;
    BfmPlatPrim p;
    BfmPlatRect disp = R(0, 0, 320, 240), r;
    size_t i;
    bfm_gl_vram_init(&vram);
    bfm_gl_batch_init(&b);
    b.vram = &vram;
    memset(&env, 0, sizeof env);
    env.clip = R(0, 0, 1024, 512);
    bfm_gl_batch_set_env(&b, &env);
    CHECK(bfm_gl_batch_owner(&b, 520, 260) == 0);
    /* a tile into off-screen VRAM: its tiles now live in the VRAM target */
    p = prim(BFM_PRIM_TILE);
    p.v[0].x = 512; p.v[0].y = 256; p.w = 40; p.h = 20;
    bfm_gl_batch_add(&b, &p, 1);
    CHECK(bfm_gl_batch_owner(&b, 512, 256) == 1 + BFM_GL_TARGET_VRAM);
    CHECK(bfm_gl_batch_owner(&b, 551, 275) == 1 && bfm_gl_batch_owner(&b, 560, 256) == 0);
    /* 15-bit sampling of it: one SNAPSHOT (tile-aligned), reused next time */
    p = prim(BFM_PRIM_SPRITE);
    p.tpage = tp(2, 0, 512, 256);
    p.v[0].u = 4; p.v[0].v = 2; p.w = 20; p.h = 10;
    p.v[0].x = 0; p.v[0].y = 400;
    bfm_gl_batch_add(&b, &p, 1);
    CHECK(count_kind(&b, BFM_GL_CMD_SNAPSHOT) == 1 && b.feedback_snaps == 1);
    for (i = 0; i < b.nc; i++)
        if (b.c[i].kind == BFM_GL_CMD_SNAPSHOT)
            CHECK(rect_is(&b.c[i].vrect, 512, 256, 32, 16) && b.c[i].snap == 1);
    CHECK((b.v[b.nv - 1].flags & BFM_GLV_SNAP) && b.c[b.nc - 1].snap == 1);
    CHECK(fabsf(b.v[b.nv - 6].s - 4.0f / 32.0f) < 1e-6f && fabsf(b.v[b.nv - 6].t - 2.0f / 16.0f) < 1e-6f);
    p.v[0].x = 100;
    bfm_gl_batch_add(&b, &p, 1);
    CHECK(count_kind(&b, BFM_GL_CMD_SNAPSHOT) == 1);   /* reused */
    /* drawing into it again invalidates the snapshot */
    {
        BfmPlatPrim t = prim(BFM_PRIM_TILE);
        t.v[0].x = 512; t.v[0].y = 256; t.w = 4; t.h = 4;
        bfm_gl_batch_add(&b, &t, 1);
    }
    bfm_gl_batch_add(&b, &p, 1);
    CHECK(count_kind(&b, BFM_GL_CMD_SNAPSHOT) == 2);
    /* 4-bit sampling needs the 16-bit words: RESOLVE, tiles become clean */
    p.tpage = tp(0, 0, 512, 256);
    p.clut = clut_of(0, 490);
    bfm_gl_batch_add(&b, &p, 1);
    CHECK(count_kind(&b, BFM_GL_CMD_RESOLVE) >= 1 && bfm_gl_batch_owner(&b, 512, 256) == 0);
    /* a GPU-drawn CLUT is resolved too */
    {
        BfmPlatPrim t = prim(BFM_PRIM_TILE);
        int before;
        t.v[0].x = 0; t.v[0].y = 490; t.w = 16; t.h = 1;
        bfm_gl_batch_add(&b, &t, 1);
        before = count_kind(&b, BFM_GL_CMD_RESOLVE);
        p.tpage = tp(0, 0, 640, 0);
        bfm_gl_batch_add(&b, &p, 1);
        CHECK(count_kind(&b, BFM_GL_CMD_RESOLVE) == before + 1);
        for (i = 0; i < b.nc; i++)
            if (b.c[i].kind == BFM_GL_CMD_RESOLVE && b.c[i].vrect.y == 480)
                CHECK(rect_is(&b.c[i].vrect, 0, 480, 16, 16));
    }
    /* a texture window forces the resolve path even for 15-bit */
    env.texture_window = R(0, 0, 16, 16);
    bfm_gl_batch_set_env(&b, &env);
    {
        BfmPlatPrim t = prim(BFM_PRIM_TILE);
        int before;
        t.v[0].x = 768; t.v[0].y = 0; t.w = 16; t.h = 16;
        bfm_gl_batch_add(&b, &t, 1);
        before = b.feedback_resolves;
        p.tpage = tp(2, 0, 768, 0);
        p.v[0].u = 0; p.v[0].v = 0;
        bfm_gl_batch_add(&b, &p, 1);
        CHECK((int)b.feedback_resolves > before);
    }
    env.texture_window = R(0, 0, 0, 0);
    bfm_gl_batch_set_env(&b, &env);
    /* uploads clean the tiles they fully cover */
    {
        BfmPlatPrim t = prim(BFM_PRIM_TILE);
        t.v[0].x = 0; t.v[0].y = 300; t.w = 40; t.h = 40;
        bfm_gl_batch_add(&b, &t, 1);
        r = R(0, 300, 40, 40);
        bfm_gl_batch_add_copy(&b, &r);
        CHECK(bfm_gl_batch_owner(&b, 20, 320) == 0);          /* fully covered */
        CHECK(bfm_gl_batch_owner(&b, 0, 300) == 1);           /* tile 288..304 partial */
    }
    /* StoreImage: add_resolve emits per-owner runs */
    bfm_gl_batch_clear(&b);
    r = R(0, 288, 64, 64);
    bfm_gl_batch_add_resolve(&b, &r);
    CHECK(count_kind(&b, BFM_GL_CMD_RESOLVE) >= 1 && bfm_gl_batch_owner(&b, 0, 300) == 0);
    /* scene targets: evicting a slot forgets its tiles */
    bfm_gl_batch_set_display(&b, &disp, 0);
    env.clip = disp;
    bfm_gl_batch_set_env(&b, &env);
    p = prim(BFM_PRIM_TILE);
    p.v[0].x = 0; p.v[0].y = 0; p.w = 16; p.h = 16;
    bfm_gl_batch_add(&b, &p, 1);
    CHECK(bfm_gl_batch_owner(&b, 0, 0) == 1 + BFM_GL_TARGET_SCENE0);
    disp.w = 256;
    bfm_gl_batch_set_display(&b, &disp, 0);
    CHECK(bfm_gl_batch_owner(&b, 0, 0) == 0);
    bfm_gl_batch_free(&b);
    printf("render feedback tile map ok\n");
}

static void test_font_overlay(void) {
    BfmGlBatch b;
    BfmPlatPrim prims[256];
    BfmPlatRect disp = R(0, 0, 320, 240);
    size_t n = bfm_plat_font_text_prims(4, 4, "HI", 1, 255, 255, 255, 1, prims, 256);
    bfm_gl_batch_init(&b);
    b.overlay = 1;
    bfm_gl_batch_set_display(&b, &disp, 0);
    bfm_gl_batch_add(&b, prims, n);
    CHECK(b.nc >= 1 && b.c[0].target == BFM_GL_TARGET_WINDOW &&
          b.c[0].blend == BFM_GL_BLEND_DUAL && (b.v[0].flags & BFM_GLV_SEMI));
    CHECK(bfm_gl_batch_scene_at(&b, 0, 0) < 0);   /* overlay owns no buffers */
    bfm_gl_batch_free(&b);
}

static int run_core(void) {
    test_decode();
    test_sampler();
    test_modulate();
    test_blend();
    test_dither();
    test_vram();
    test_rgb24();
    test_batch();
    test_scenes();
    test_replacements();
    test_ref_mask();
    test_feedback_core();
    test_font_overlay();
    return fails;
}

/* ---------------------------------------------------------------- exec */
#ifdef BFM_GL_PROBE_EXEC

/* object -> function pointer without an ISO C cast */
#define LOADFN(dst, obj)                                                      \
    do {                                                                      \
        void *o_ = (obj);                                                     \
        memcpy(&(dst), &o_, sizeof o_);                                       \
    } while (0)

typedef void *OSMesaContext;
typedef OSMesaContext (*CreateAttribsFn)(const int *, OSMesaContext);
typedef unsigned char (*MakeCurrentFn)(OSMesaContext, void *, unsigned, int, int);
typedef void *(*GetProcFn)(const char *);
typedef void (*DestroyFn)(OSMesaContext);
static GetProcFn osmesa_getproc;
static void *getproc(const char *n) { return osmesa_getproc(n); }

#define WIN_W 640
#define WIN_H 480
static uint8_t winbuf[WIN_W * WIN_H * 4];

typedef void (*FinishFn)(void);

static BfmGlExec *X;
static BfmGlBatch B;
static BfmGlReplacements REPL;

static void flush_all(void) {
    bfm_gl_exec_sync_vram(X, &vram);
    CHECK(bfm_gl_exec_run(X, &B, 1) == BFM_PLAT_OK);
    bfm_gl_batch_clear(&B);
}

static void upload(int x, int y, int w, int h, const uint16_t *px) {
    BfmPlatRect r = R(x, y, w, h);
    flush_all();
    CHECK(bfm_gl_vram_upload(&vram, &r, px) == BFM_PLAT_OK);
    bfm_gl_batch_add_copy(&B, &r);
}

static void read_target(int target, int x, int y, int w, int h, uint16_t *out) {
    BfmPlatRect r = R(x, y, w, h);
    flush_all();
    CHECK(bfm_gl_exec_readback(X, &B, target, &r, out) == BFM_PLAT_OK);
}

static int chan_diff(uint16_t a, uint16_t b) {
    int ch, d = 0;
    for (ch = 0; ch < 3; ch++) {
        int e = abs(((a >> (5 * ch)) & 31) - ((b >> (5 * ch)) & 31));
        if (e > d) d = e;
    }
    return d;
}

static void vram_env(int dither) {
    BfmPlatDrawEnv env;
    memset(&env, 0, sizeof env);
    env.clip = R(0, 0, 1024, 512);
    env.dither = (uint8_t)dither;
    bfm_gl_batch_set_env(&B, &env);
}

static void sprite(int x, int y, int w, int h, uint16_t tpage, uint16_t clut, int u,
                   int v, int flags, int r, int g, int b) {
    BfmPlatPrim p = prim(BFM_PRIM_SPRITE);
    p.v[0].x = (int16_t)x; p.v[0].y = (int16_t)y;
    p.v[0].u = (uint8_t)u; p.v[0].v = (uint8_t)v;
    p.v[0].r = (uint8_t)r; p.v[0].g = (uint8_t)g; p.v[0].b = (uint8_t)b;
    p.w = (uint16_t)w; p.h = (uint16_t)h;
    p.tpage = tpage; p.clut = clut;
    p.flags = (uint8_t)flags;
    bfm_gl_batch_add(&B, &p, 1);
}

/* Sprites through each depth vs bfm_gl_sample + bfm_gl_modulate. */
static void exec_textures(int scale) {
    static uint16_t tex[64 * 16], out[64 * 32];
    uint16_t t4 = tp(0, 0, 640, 0), t8 = tp(1, 0, 704, 0), t15 = tp(2, 0, 768, 0);
    uint16_t cl = clut_of(0, 500);
    int i, u, v, mism = 0, near = 0, total = 0;
    CHECK(bfm_gl_exec_set_scale(X, scale) == BFM_PLAT_OK);
    bfm_gl_vram_init(&vram);
    /* pseudo-random texel data and a 256-entry CLUT (index 0 transparent) */
    for (i = 0; i < 64 * 16; i++) tex[i] = (uint16_t)((i * 2654435761u) >> 7);
    upload(640, 0, 16, 16, tex);          /* 4-bit: 64x16 texels */
    upload(704, 0, 32, 16, tex);          /* 8-bit: 64x16 texels */
    upload(768, 0, 64, 16, tex);          /* 15-bit */
    for (i = 0; i < 256; i++) tex[i] = i == 0 ? 0 : (uint16_t)(((i * 40503u) & 0x7FFF) | 1);
    upload(0, 500, 256, 1, tex);
    vram_env(0);
    for (i = 0; i < 64 * 32; i++) out[i] = 0x1234;
    {
        /* a known backdrop so transparent texels are visible */
        BfmPlatRect bg = R(0, 0, 64, 96);
        BfmPlatPrim f = prim(BFM_PRIM_FILL);
        f.v[0].x = bg.x; f.v[0].y = bg.y; f.w = bg.w; f.h = bg.h;
        f.v[0].r = 0x10; f.v[0].g = 0x20; f.v[0].b = 0x30;
        bfm_gl_batch_add(&B, &f, 1);
    }
    sprite(0, 0, 64, 16, t4, cl, 0, 0, BFM_PRIM_FLAG_RAW_TEXTURE, 0, 0, 0);
    sprite(0, 32, 64, 16, t8, cl, 0, 0, BFM_PRIM_FLAG_RAW_TEXTURE, 0, 0, 0);
    sprite(0, 64, 64, 16, t15, 0, 0, 0, 0, 90, 200, 128);   /* modulated */
    for (i = 0; i < 3; i++) {
        uint16_t page = i == 0 ? t4 : i == 1 ? t8 : t15;
        read_target(BFM_GL_TARGET_VRAM, 0, i * 32, 64, 16, out);
        for (v = 0; v < 16; v++)
            for (u = 0; u < 64; u++) {
                uint16_t texel = bfm_gl_sample(&vram, page, cl, u, v, NULL), want;
                uint8_t m[3];
                if (texel == 0) {
                    want = rgb15(0x10 >> 3, 0x20 >> 3, 0x30 >> 3);
                } else {
                    bfm_gl_modulate(texel, 90, 200, 128, i < 2, m);
                    /* bit 15 of the written pixel is the texel's STP */
                    want = (uint16_t)(rgb15(m[0] >> 3, m[1] >> 3, m[2] >> 3) | (texel & 0x8000));
                }
                total++;
                if (out[v * 64 + u] != want) {
                    if (chan_diff(out[v * 64 + u], want) <= 1) near++;
                    else if (mism++ < 4)
                        printf("  depth %d (%d,%d): got %04x want %04x\n",
                               i == 0 ? 4 : i == 1 ? 8 : 15, u, v, out[v * 64 + u], want);
                }
            }
    }
    CHECK(mism == 0);
    printf("gl %dx textures 4/8/15-bit: %d texels, %d exact, %d off by one step\n",
           scale, total, total - near - mism, near);
}

/* Each semi mode over a copied backdrop, textured with STP texels, and the
 * STP split (texels without STP stay opaque). */
static void exec_blend(void) {
    static uint16_t back[32 * 32], front[32 * 32], out[32 * 32];
    int m, i, x, y;
    CHECK(bfm_gl_exec_set_scale(X, 1) == BFM_PLAT_OK);
    bfm_gl_vram_init(&vram);
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++) {
            back[y * 32 + x] = rgb15(x, 31 - x, x);
            /* STP on every texel except column 31 */
            front[y * 32 + x] = (uint16_t)(rgb15(y, y, 31 - y) | (x == 31 ? 0 : 0x8000));
        }
    upload(640, 256, 32, 32, front);
    for (m = 0; m < 4; m++) {
        BfmGlBlendState s = bfm_gl_blend_state(m);
        int exact_ref = 0, worst_ref = 0, exact_gl = 0, worst_gl = 0;
        upload(m * 32, 300, 32, 32, back);
        vram_env(0);
        sprite(m * 32, 300, 32, 32, tp(2, m, 640, 256), 0, 0, 0,
               BFM_PRIM_FLAG_SEMI_TRANS | BFM_PRIM_FLAG_RAW_TEXTURE, 0, 0, 0);
        read_target(BFM_GL_TARGET_VRAM, m * 32, 300, 32, 32, out);
        for (y = 0; y < 32; y++)
            for (x = 0; x < 31; x++) {
                uint16_t b = back[y * 32 + x], f = front[y * 32 + x] & 0x7FFF;
                int dr = chan_diff(out[y * 32 + x], bfm_gl_blend_ref(m, b, f));
                int dg = chan_diff(out[y * 32 + x], bfm_gl_blend_emulate(&s, b, f));
                exact_ref += dr == 0;
                exact_gl += dg == 0;
                if (dr > worst_ref) worst_ref = dr;
                if (dg > worst_gl) worst_gl = dg;
            }
        for (y = 0; y < 32; y++)   /* no STP: plain front colour */
            CHECK(out[y * 32 + 31] == (front[y * 32 + 31] & 0x7FFF));
        CHECK(worst_ref <= 1 && worst_gl <= 1);
        if (m == 1 || m == 2) CHECK(exact_ref == 32 * 31);
        printf("gl blend mode %d: vs PS1 %d/%d exact (worst %d), vs GL model %d exact (worst %d)\n",
               m, exact_ref, 32 * 31, worst_ref, exact_gl, worst_gl);
    }
    /* untextured semi tile: every pixel blends */
    {
        BfmPlatPrim t = prim(BFM_PRIM_TILE);
        uint16_t bb[4], want;
        for (i = 0; i < 4; i++) bb[i] = rgb15(20, 10, 4);
        upload(200, 400, 2, 2, bb);
        vram_env(0);
        t.v[0].x = 200; t.v[0].y = 400; t.w = 2; t.h = 2;
        t.v[0].r = 12 * 255 / 31; t.v[0].g = 30 * 255 / 31; t.v[0].b = 8 * 255 / 31;
        t.flags = BFM_PRIM_FLAG_SEMI_TRANS;
        t.tpage = tp(2, 1, 0, 0);
        bfm_gl_batch_add(&B, &t, 1);
        read_target(BFM_GL_TARGET_VRAM, 200, 400, 2, 2, out);
        want = bfm_gl_blend_ref(1, rgb15(20, 10, 4), rgb15(11, 29, 7));
        for (i = 0; i < 4; i++) CHECK(chan_diff(out[i], want) <= 1);
    }
}

static void exec_dither(void) {
    static uint16_t out[16 * 8];
    BfmPlatPrim g = prim(BFM_PRIM_POLY_G4);
    int x, y, on, bad = 0;
    for (on = 0; on < 2; on++) {
        CHECK(bfm_gl_exec_set_scale(X, on ? 2 : 1) == BFM_PLAT_OK);
        vram_env(1);
        for (x = 0; x < 4; x++) {
            g.v[x].x = (int16_t)(100 + (x & 1) * 16);
            g.v[x].y = (int16_t)(100 + (x >> 1) * 8);
            g.v[x].r = 101; g.v[x].g = 52; g.v[x].b = 3;
        }
        bfm_gl_batch_add(&B, &g, 1);
        bfm_gl_exec_sync_vram(X, &vram);
        CHECK(bfm_gl_exec_run(X, &B, on) == BFM_PLAT_OK);
        bfm_gl_batch_clear(&B);
        {
            BfmPlatRect r = R(100, 100, 16, 8);
            CHECK(bfm_gl_exec_readback(X, &B, BFM_GL_TARGET_VRAM, &r, out) == BFM_PLAT_OK);
        }
        for (y = 0; y < 8; y++)
            for (x = 0; x < 16; x++) {
                uint16_t want = on ? rgb15(bfm_gl_dither5(101, 100 + x, 100 + y),
                                           bfm_gl_dither5(52, 100 + x, 100 + y),
                                           bfm_gl_dither5(3, 100 + x, 100 + y))
                                   : rgb15(101 >> 3, 52 >> 3, 3 >> 3);
                if (out[y * 16 + x] != want && bad++ < 4)
                    printf("  dither %d (%d,%d): got %04x want %04x\n", on, x, y,
                           out[y * 16 + x], want);
            }
    }
    CHECK(bad == 0);
    printf("gl dither (native 4x4 matrix at 2x, off at 1x) ok\n");
}

/* hor+: geometry left of the 4:3 edge survives in the wide scene target;
 * present letterboxes and the overlay lands on the 4:3 area. */
static void exec_scene_present(void) {
    static uint16_t out[426 * 4];
    BfmPlatRect disp = R(0, 0, 320, 240);
    BfmPlatDrawEnv env;
    BfmPlatPrim t = prim(BFM_PRIM_TILE);
    BfmGlBatch ov;
    BfmPlatPrim txt[256];
    size_t n;
    FinishFn finish;
    int x, lit = 0;
    LOADFN(finish, getproc("glFinish"));
    CHECK(bfm_gl_exec_set_scale(X, 2) == BFM_PLAT_OK);
    bfm_gl_batch_set_display(&B, &disp, 53);
    memset(&env, 0, sizeof env);
    env.clip = disp;
    env.clear_bg = 1;
    env.bg_b = 255;
    bfm_gl_batch_set_env(&B, &env);
    t.v[0].x = -40; t.v[0].y = 0; t.w = 20; t.h = 240;    /* x -40..-20 */
    t.v[0].r = 255;
    bfm_gl_batch_add(&B, &t, 1);
    t.v[0].x = 300; t.w = 60; t.v[0].g = 255;            /* crosses the right edge */
    bfm_gl_batch_add(&B, &t, 1);
    read_target(BFM_GL_TARGET_SCENE0, 0, 100, 426, 1, out);
    CHECK(out[5] == rgb15(0, 0, 31));        /* cleared margin */
    CHECK(out[13] == rgb15(31, 0, 0) && out[32] == rgb15(31, 0, 0) && out[33] == rgb15(0, 0, 31));
    CHECK(out[353] == rgb15(31, 31, 0) && out[412] == rgb15(31, 31, 0) &&
          out[413] == rgb15(0, 0, 31));      /* 353 + 60 = 413 */

    bfm_gl_batch_init(&ov);
    ov.overlay = 1;
    bfm_gl_batch_set_display(&ov, &disp, 0);
    n = bfm_plat_font_text_prims(0, 0, "#", 1, 255, 255, 255, 0, txt, 256);
    bfm_gl_batch_add(&ov, txt, n);
    CHECK(bfm_gl_exec_present(X, &B, &ov, WIN_W, WIN_H, 4.0 / 3.0 * 426 / 320, 0) == BFM_PLAT_OK);
    finish();
    /* 640x480 window, 1.775 image -> 640x361 centred: bars top and bottom */
    {
        const uint8_t *top = &winbuf[((size_t)(WIN_H - 10) * WIN_W + 320) * 4];
        const uint8_t *mid = &winbuf[((size_t)(WIN_H / 2) * WIN_W + 320) * 4];
        const uint8_t *left = &winbuf[((size_t)(WIN_H / 2) * WIN_W + 45) * 4];
        CHECK(top[0] == 0 && top[1] == 0 && top[2] == 0);
        CHECK(mid[2] == 255 && mid[0] == 0);                 /* blue backdrop */
        CHECK(left[0] > 200 && left[2] < 60);                /* the red strip */
    }
    /* the '#' glyph: the 4:3 area starts at x = 640 * 53 / 426 = 80 */
    for (x = 70; x < 110; x++) {
        const uint8_t *p = &winbuf[((size_t)(WIN_H - 60 - 3) * WIN_W + x) * 4];
        lit += p[0] > 200 && p[1] > 200 && p[2] > 200;
    }
    CHECK(lit > 0);
    /* a 24-bit movie frame shown from a CPU image, 4:3 pillarboxed */
    {
        static uint8_t img[32 * 24 * 4];
        const uint8_t *mid = &winbuf[((size_t)(WIN_H / 2) * WIN_W + 320) * 4];
        for (x = 0; x < 32 * 24; x++) {
            img[x * 4] = 0x40; img[x * 4 + 1] = 0x80; img[x * 4 + 2] = 0xC0; img[x * 4 + 3] = 255;
        }
        CHECK(bfm_gl_exec_present_image(X, 32, 24, img, NULL, WIN_W, WIN_H, 4.0 / 3.0) ==
              BFM_PLAT_OK);
        finish();
        CHECK(mid[0] == 0x40 && mid[1] == 0x80 && mid[2] == 0xC0);
    }
    bfm_gl_batch_free(&ov);
    printf("gl hor+ scene + present + overlay ok (%d overlay pixels on a row)\n", lit);
}

static void exec_replacement(void) {
    static uint8_t rgba[64 * 64 * 4];
    static uint16_t tex[16 * 16], out[64 * 16];
    BfmPlatRect r = R(832, 256, 16, 16);
    uint32_t id, old;
    int x, y, ok = 1;
    CHECK(bfm_gl_exec_set_scale(X, 1) == BFM_PLAT_OK);
    B.repl = &REPL;
    for (x = 0; x < 16 * 16; x++) tex[x] = 0x1111;
    upload(832, 256, 16, 16, tex);
    /* 64x64 HD image for a 4-bit 64x16-texel texture: 4 vertical bands */
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            uint8_t *p = &rgba[(y * 64 + x) * 4];
            int band = x / 16;
            p[0] = band == 0 ? 255 : 0;
            p[1] = band == 1 ? 255 : 0;
            p[2] = band == 2 ? 255 : 0;
            p[3] = band == 3 ? 0 : 255;    /* transparent band */
        }
    CHECK((id = bfm_gl_exec_texture(X, 64, 64, rgba)) != 0);
    CHECK(bfm_gl_repl_add(&REPL, &r, 64, 64, id, &old) == 0);
    vram_env(0);
    {
        BfmPlatPrim f = prim(BFM_PRIM_FILL);
        f.v[0].x = 0; f.v[0].y = 450; f.w = 64; f.h = 16;
        f.v[0].r = f.v[0].g = f.v[0].b = 0x80;
        bfm_gl_batch_add(&B, &f, 1);
    }
    sprite(0, 450, 64, 16, tp(0, 0, 832, 256), clut_of(0, 500), 0, 0,
           BFM_PRIM_FLAG_RAW_TEXTURE, 0, 0, 0);
    read_target(BFM_GL_TARGET_VRAM, 0, 450, 64, 16, out);
    for (y = 0; y < 16; y++) {
        ok &= out[y * 64 + 5] == rgb15(31, 0, 0);
        ok &= out[y * 64 + 21] == rgb15(0, 31, 0);
        ok &= out[y * 64 + 40] == rgb15(0, 0, 31);
        ok &= out[y * 64 + 60] == rgb15(16, 16, 16);  /* transparent -> backdrop */
    }
    CHECK(ok);
    bfm_gl_exec_texture_free(X, id);
    B.repl = NULL;
    printf("gl HD replacement ok\n");
}

/* ---- mask bit and render feedback, bit-exact vs bfm_gl_ref_draw ---- */

static BfmGlVram ref;
static BfmPlatDrawEnv REF_ENV;

/* 8-bit channel whose GL store reads back as the 5-bit value c */
static int c8of(int c) { return (c * 255 + 15) / 31; }

static void both_env(const BfmPlatDrawEnv *e) {
    REF_ENV = *e;
    bfm_gl_batch_set_env(&B, e);
}

static void both(const BfmPlatPrim *p) {
    bfm_gl_batch_add(&B, p, 1);
    bfm_gl_ref_draw(&ref, &REF_ENV, p);
}

static void both_upload(int x, int y, int w, int h, const uint16_t *px) {
    BfmPlatRect r = R(x, y, w, h);
    upload(x, y, w, h, px);
    bfm_gl_vram_upload(&ref, &r, px);
}

static BfmPlatPrim tile(int x, int y, int w, int h, int r5, int g5, int b5, int flags,
                        uint16_t tpage) {
    BfmPlatPrim p = prim(BFM_PRIM_TILE);
    p.v[0].x = (int16_t)x; p.v[0].y = (int16_t)y;
    p.w = (uint16_t)w; p.h = (uint16_t)h;
    p.v[0].r = (uint8_t)c8of(r5); p.v[0].g = (uint8_t)c8of(g5); p.v[0].b = (uint8_t)c8of(b5);
    p.flags = (uint8_t)flags;
    p.tpage = tpage;
    return p;
}

static BfmPlatPrim spr(int x, int y, int w, int h, uint16_t tpage, uint16_t clut, int u,
                       int v, int flags, int r, int g, int b) {
    BfmPlatPrim p = prim(BFM_PRIM_SPRITE);
    p.v[0].x = (int16_t)x; p.v[0].y = (int16_t)y;
    p.v[0].u = (uint8_t)u; p.v[0].v = (uint8_t)v;
    p.v[0].r = (uint8_t)r; p.v[0].g = (uint8_t)g; p.v[0].b = (uint8_t)b;
    p.w = (uint16_t)w; p.h = (uint16_t)h;
    p.tpage = tpage; p.clut = clut;
    p.flags = (uint8_t)flags;
    return p;
}

static BfmPlatPrim fillp(int x, int y, int w, int h, int r5, int g5, int b5) {
    BfmPlatPrim p = tile(x, y, w, h, r5, g5, b5, 0, 0);
    p.kind = BFM_PRIM_FILL;
    return p;
}

/* Everything the GPU drew resolved into the mirror, compared with the
 * reference over a VRAM rect, all 16 bits. */
static int compare_ref(const char *what, int x0, int y0, int w, int h) {
    BfmPlatRect r = R(x0, y0, w, h);
    int x, y, bad = 0, masked = 0;
    bfm_gl_batch_add_resolve(&B, &r);
    flush_all();
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) {
            uint16_t g = vram.px[y * 1024 + x], e = ref.px[y * 1024 + x];
            masked += (e & 0x8000) != 0;
            if (g != e && bad++ < 5)
                printf("  %s (%d,%d): gl %04x ref %04x\n", what, x, y, g, e);
        }
    CHECK(bad == 0);
    printf("gl %s: %d px bit-exact vs reference (%d with bit 15)\n", what,
           w * h - bad, masked);
    return masked;
}

static void reset_both(int scale) {
    BfmPlatDrawEnv env;
    CHECK(bfm_gl_exec_set_scale(X, scale) == BFM_PLAT_OK);
    bfm_gl_batch_free(&B);
    bfm_gl_batch_init(&B);
    B.vram = &vram;
    bfm_gl_vram_init(&vram);
    bfm_gl_vram_init(&ref);
    memset(&env, 0, sizeof env);
    env.clip = R(0, 0, 1024, 512);
    both_env(&env);
    {   /* targets start black with bit 15 clear, like the zeroed mirrors */
        BfmPlatPrim f = fillp(0, 0, 1024, 512, 0, 0, 0);
        both(&f);
    }
}

static void mask_texture(void) {
    static uint16_t tex[32 * 32];
    int x, y;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
            tex[y * 32 + x] = x == y ? 0 : (uint16_t)(rgb15(x, y, 31 - x) |
                                                      (((x + y) & 1) ? 0x8000 : 0));
    both_upload(640, 0, 32, 32, tex);
}

static void exec_mask(int scale) {
    const int MS = BFM_PRIM_FLAG_MASK_SET, MC = BFM_PRIM_FLAG_MASK_CHECK;
    const int SEMI = BFM_PRIM_FLAG_SEMI_TRANS, RAW = BFM_PRIM_FLAG_RAW_TEXTURE;
    uint16_t t15 = tp(2, 0, 640, 0), t15m1 = tp(2, 1, 640, 0), t15m2 = tp(2, 2, 640, 0);
    BfmPlatPrim p;
    int masked;
    reset_both(scale);
    mask_texture();
    p = fillp(0, 300, 96, 64, 4, 8, 12);                     both(&p);
    p = tile(0, 300, 32, 32, 31, 0, 0, MS, 0);              both(&p);   /* sets bit 15 */
    flush_all();
    /* first check on this target: the stencil is built from alpha */
    p = spr(16, 316, 32, 32, t15, 0, 0, 0, RAW | MC, 0, 0, 0);  both(&p);
    p = spr(32, 332, 32, 32, t15m1, 0, 0, 0, RAW | SEMI, 0, 0, 0); both(&p);
    p = tile(8, 340, 40, 16, 3, 20, 9, SEMI | MC | MS, tp(2, 2, 0, 0)); both(&p);
    p = tile(20, 310, 30, 30, 10, 10, 10, SEMI | MC, tp(2, 1, 0, 0)); both(&p);
    p = spr(64, 300, 32, 32, t15m2, 0, 0, 0, RAW | SEMI | MS, 0, 0, 0); both(&p);
    p = spr(60, 330, 32, 32, t15, 0, 0, 0, MC, 100, 200, 50);   both(&p);  /* modulated */
    p = tile(70, 320, 8, 30, 31, 31, 0, MC, 0);             both(&p);
    /* with the stencil live: an untextured semi tile and a textured sprite
     * in one command (each fragment must be drawn once and write its own
     * bit), then a check over both */
    p = tile(0, 350, 30, 10, 5, 5, 5, SEMI, tp(2, 1, 0, 0)); both(&p);
    p = spr(30, 350, 16, 10, t15, 0, 0, 0, RAW, 0, 0, 0);   both(&p);
    p = tile(0, 352, 50, 4, 31, 0, 31, MC, 0);              both(&p);
    masked = compare_ref(scale == 1 ? "mask 1x" : "mask 3x", 0, 300, 96, 64);
    CHECK(masked > 500 && masked < 96 * 64);
}

static void exec_feedback(int scale) {
    const int MS = BFM_PRIM_FLAG_MASK_SET, SEMI = BFM_PRIM_FLAG_SEMI_TRANS;
    const int RAW = BFM_PRIM_FLAG_RAW_TEXTURE;
    BfmPlatPrim p;
    BfmPlatDrawEnv env;
    BfmPlatRect disp = R(0, 0, 320, 240);
    uint32_t snaps, resolves;
    int k;
    reset_both(scale);
    mask_texture();

    /* (a) render into off-screen VRAM, then use it as a 15-bit texture
     *     (semi mode 1 so the rendered bit 15 decides what blends) */
    p = fillp(512, 256, 64, 64, 2, 4, 6);                    both(&p);
    p = tile(512, 256, 40, 20, 30, 1, 5, MS, 0);             both(&p);
    p = spr(520, 270, 32, 32, tp(2, 0, 640, 0), 0, 0, 0, RAW, 0, 0, 0); both(&p);
    p = fillp(0, 400, 64, 64, 9, 9, 9);                      both(&p);
    snaps = B.feedback_snaps;
    p = spr(0, 400, 64, 64, tp(2, 1, 512, 256), 0, 0, 0, RAW | SEMI, 0, 0, 0); both(&p);
    CHECK(B.feedback_snaps == snaps + 1);                     /* GPU snapshot path */
    compare_ref("feedback 15-bit", 0, 400, 64, 64);

    /* HD detail survives: a triangle edge rendered at scale S, copied 1:1
     * through the snapshot, matches at device resolution */
    if (scale > 1) {
        static uint8_t a[48 * 48 * 16 * 4], c[48 * 48 * 16 * 4];
        BfmPlatRect ra = R(704, 256, 16, 16), rc = R(704, 320, 16, 16);
        BfmPlatPrim t3 = prim(BFM_PRIM_POLY_F3);
        size_t n = (size_t)16 * scale * 16 * scale * 4;
        t3.v[0].x = 704; t3.v[0].y = 256; t3.v[1].x = 720; t3.v[1].y = 259;
        t3.v[2].x = 707; t3.v[2].y = 272; t3.v[0].r = 250; t3.v[0].g = 100;
        bfm_gl_batch_add(&B, &t3, 1);
        p = spr(704, 320, 16, 16, tp(2, 0, 704, 256), 0, 0, 0, RAW, 0, 0, 0);
        bfm_gl_batch_add(&B, &p, 1);
        flush_all();
        CHECK(bfm_gl_exec_readback_rgba(X, &B, BFM_GL_TARGET_VRAM, &ra, a) == BFM_PLAT_OK);
        CHECK(bfm_gl_exec_readback_rgba(X, &B, BFM_GL_TARGET_VRAM, &rc, c) == BFM_PLAT_OK);
        {
            size_t i, diff = 0, edge = 0;
            for (i = 0; i < n; i += 4) {
                diff += memcmp(a + i, c + i, 3) != 0;
                edge += a[i] > 200;
            }
            CHECK(diff == 0 && edge > 0);
            printf("gl feedback %dx: %zu device pixels copied exactly (%zu lit)\n", scale,
                   n / 4, edge);
        }
        /* keep the reference in step: redo both in the reference by
         * resolving what the GPU drew there */
        {
            BfmPlatRect all = R(704, 256, 16, 80);
            uint16_t tmp[16 * 80];
            bfm_gl_batch_add_resolve(&B, &all);
            flush_all();
            bfm_gl_vram_download(&vram, &all, tmp);
            bfm_gl_vram_upload(&ref, &all, tmp);
        }
    }

    /* (b) the GPU draws 4-bit index data and its CLUT; a sprite then
     *     samples them (readback path) */
    for (k = 0; k < 16; k++) {   /* index words: nibbles k, k+1, k+2, k+3 */
        int w = (k & 15) | (((k + 1) & 15) << 4) | (((k + 2) & 15) << 8) | (((k + 3) & 7) << 12);
        p = tile(576 + k, 256, 1, 16, w & 31, (w >> 5) & 31, (w >> 10) & 31,
                 (k + 3) & 8 ? MS : 0, 0);
        both(&p);
    }
    for (k = 0; k < 16; k++) {   /* CLUT at (0, 490); index 5 stays transparent */
        p = tile(k, 490, 1, 1, k == 5 ? 0 : 31 - k, k * 2, 7, k & 1 ? MS : 0, 0);
        both(&p);
    }
    resolves = B.feedback_resolves;
    p = fillp(100, 400, 64, 16, 1, 2, 3);                   both(&p);
    p = spr(100, 400, 64, 16, tp(0, 0, 576, 256), clut_of(0, 490), 0, 0, RAW, 0, 0, 0);
    both(&p);
    CHECK(B.feedback_resolves > resolves);
    compare_ref("feedback 4-bit + CLUT", 100, 400, 64, 16);

    /* (c) a frame buffer sampled while being drawn (hor+ scene target) */
    bfm_gl_batch_set_display(&B, &disp, 53);
    memset(&env, 0, sizeof env);
    env.clip = disp;
    both_env(&env);
    p = fillp(0, 0, 320, 240, 0, 0, 10);                     both(&p);
    p = tile(0, 0, 64, 64, 20, 5, 25, 0, 0);                 both(&p);
    p = spr(8, 8, 32, 32, tp(2, 0, 640, 0), 0, 0, 0, RAW, 0, 0, 0); both(&p);
    snaps = B.feedback_snaps;
    p = spr(200, 100, 32, 32, tp(2, 0, 0, 0), 0, 16, 16, RAW, 0, 0, 0); both(&p);
    CHECK(B.feedback_snaps == snaps + 1);
    compare_ref("feedback frame buffer", 0, 0, 320, 240);

    /* everything, everywhere: the resolved mirror equals the reference */
    memset(&env, 0, sizeof env);
    env.clip = R(0, 0, 1024, 512);
    both_env(&env);
    compare_ref(scale == 1 ? "whole VRAM 1x" : "whole VRAM 3x", 0, 0, 1024, 512);
}

static int run_exec(const char *lib) {
    static const int attribs[] = {0x22, 0x1908 /* FORMAT RGBA */, 0x30, 0, 0x31, 0,
                                  0x33, 0x34 /* core */, 0x36, 3, 0x37, 3, 0};
    void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    CreateAttribsFn create;
    MakeCurrentFn make;
    DestroyFn destroy;
    OSMesaContext ctx;
    char err[512];
    if (!h) {
        printf("skip exec: %s\n", dlerror());
        return 0;
    }
    LOADFN(create, dlsym(h, "OSMesaCreateContextAttribs"));
    LOADFN(make, dlsym(h, "OSMesaMakeCurrent"));
    LOADFN(destroy, dlsym(h, "OSMesaDestroyContext"));
    LOADFN(osmesa_getproc, dlsym(h, "OSMesaGetProcAddress"));
    if (!create || !make || !destroy || !osmesa_getproc) {
        printf("skip exec: not an OSMesa library\n");
        return 0;
    }
    if (!(ctx = create(attribs, NULL))) {
        printf("skip exec: no GL 3.3 core OSMesa context\n");
        return 0;
    }
    if (!make(ctx, winbuf, 0x1401 /* GL_UNSIGNED_BYTE */, WIN_W, WIN_H)) {
        printf("FAIL OSMesaMakeCurrent\n");
        return 1;
    }
    {
        typedef const unsigned char *(*GetStringFn)(unsigned);
        GetStringFn gs;
        LOADFN(gs, getproc("glGetString"));
        printf("GL: %s | %s\n", gs(0x1F02), gs(0x1F01));
    }
    X = bfm_gl_exec_create(getproc, 1, err, sizeof err);
    if (!X) {
        printf("FAIL exec create: %s\n", err);
        return 1;
    }
    bfm_gl_batch_init(&B);
    B.vram = &vram;
    exec_textures(1);
    exec_textures(3);
    exec_blend();
    exec_dither();
    exec_replacement();
    exec_mask(1);
    exec_mask(3);
    exec_feedback(1);
    exec_feedback(3);
    bfm_gl_batch_free(&B);
    bfm_gl_batch_init(&B);
    B.vram = &vram;
    exec_scene_present();
    CHECK(bfm_gl_exec_error(X) == 0);
    bfm_gl_batch_free(&B);
    bfm_gl_exec_destroy(X);
    destroy(ctx);
    return fails;
}
#endif

int main(int argc, char **argv) {
    const char *g = argc > 1 ? argv[1] : "core";
    int r;
    if (strcmp(g, "core") == 0) r = run_core();
#ifdef BFM_GL_PROBE_EXEC
    else if (strcmp(g, "exec") == 0 && argc > 2) r = run_exec(argv[2]);
#endif
    else {
        printf("usage: bfm_gl_probe core | exec LIBOSMESA\n");
        return 2;
    }
    if (r) {
        printf("%d failure(s)\n", r);
        return 1;
    }
    printf("ok %s\n", g);
    return 0;
}
