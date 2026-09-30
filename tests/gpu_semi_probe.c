/* PS1 semi-transparency in the GPU controller rasterizer (psx-spx "GPU
 * Semi-Transparency"): four abr modes on flat polygons and rectangles, the
 * draw-mode (E1) vs polygon-texpage source of abr, and STP gating for
 * textured primitives. Synthetic VRAM backend; no retail data. */
#include "musashi_gpu_controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t vram[512][1024];
static int ok1(void *u) { (void)u; return 1; }
static int ok_word(void *u, uint32_t w) { (void)u; (void)w; return 1; }
static int ok_int(void *u, int v) { (void)u; (void)v; return 1; }
static int ok_disp(void *u, const MusashiGpuDisplayState *d) { (void)u; (void)d; return 1; }
static int ok_fill(void *u, const MusashiGpuFill *f) { (void)u; (void)f; return 1; }
static int store(void *u, uint16_t x, uint16_t y, uint16_t p) { (void)u; vram[y][x] = p; return 1; }
static int readv(void *u, uint16_t x, uint16_t y, uint16_t *p) { (void)u; *p = vram[y][x]; return 1; }

static MusashiGpuController gpu;
static void gp0(uint32_t w) { assert(musashi_gpu_controller_write32(&gpu, MUSASHI_GPU_GP0, w)); }
static uint16_t rgb15(unsigned r, unsigned g, unsigned b) { return (uint16_t)(r | (g << 5) | (b << 10)); }
static uint32_t cmd(uint32_t op, unsigned r, unsigned g, unsigned b) {
    return (op << 24) | ((b << 3) << 16) | ((g << 3) << 8) | (r << 3);
}
static uint16_t expect(uint16_t back, uint16_t front, unsigned mode) {
    uint16_t out = 0;
    for (unsigned s = 0; s < 15; s += 5) {
        int b = (back >> s) & 31, f = (front >> s) & 31, c =
            mode == 0 ? (b + f) / 2 : mode == 1 ? b + f : mode == 2 ? b - f : b + f / 4;
        if (c < 0) c = 0;
        if (c > 31) c = 31;
        out |= (uint16_t)(c << s);
    }
    return out;
}
static void back(uint16_t color) {
    for (int y = 0; y < 64; ++y) for (int x = 100; x < 164; ++x) vram[y][x] = color;
}

int main(void) {
    const MusashiGpuBackend backend = {NULL, ok1, ok_word, ok_int, ok1, ok1, ok_word, ok_disp,
                                       ok_fill, store, readv};
    const uint16_t B = rgb15(20, 10, 4), F = rgb15(8, 16, 30);
    unsigned mode;
    assert(musashi_gpu_controller_init(&gpu, &backend));
    gp0(0xe3000000u);                                  /* drawing area 0,0 */
    gp0(0xe4000000u | (511u << 10) | 1023u);           /* .. 1023,511 */
    gp0(0xe5000000u);                                  /* offset 0,0 */
    for (mode = 0; mode < 4; ++mode) {
        gp0(0xe1000000u | (mode << 5));
        /* Semi flat rectangle 0x62 at (100,0) 4x4. */
        back(B);
        gp0(cmd(0x62, 8, 16, 30)); gp0((0u << 16) | 100u); gp0((4u << 16) | 4u);
        assert(vram[1][101] == expect(B, F, mode));
        /* Opaque rectangle 0x60 stores the front colour. */
        back(B);
        gp0(cmd(0x60, 8, 16, 30)); gp0((0u << 16) | 100u); gp0((4u << 16) | 4u);
        assert(vram[1][101] == F);
        /* Semi flat triangle 0x22 covering (110,10). */
        back(B);
        gp0(cmd(0x22, 8, 16, 30));
        gp0((0u << 16) | 100u); gp0((0u << 16) | 140u); gp0((40u << 16) | 100u);
        assert(vram[10][110] == expect(B, F, mode));
    }
    /* Textured semi rectangle 0x66 (15-bit texture page at 0,0 via E1 bits
     * 7-8 = 2, abr from E1): only texels with STP (bit 15) blend. */
    for (mode = 0; mode < 4; ++mode) {
        gp0(0xe1000000u | (2u << 7) | (mode << 5));
        vram[0][0] = (uint16_t)(F | 0x8000u);   /* STP set: blended */
        vram[0][1] = F;                          /* STP clear: opaque */
        back(B);
        gp0(0x66808080u); gp0((0u << 16) | 100u); gp0(0u); gp0((1u << 16) | 2u);
        assert(vram[0][100] == (uint16_t)(expect(B, F, mode) | 0x8000u)); /* keeps STP */
        assert(vram[0][101] == F);
    }
    /* E6 mask: check (bit 1) leaves pixels with bit 15 alone; set (bit 0)
     * forces bit 15 on what is drawn. Textured pixels keep their STP bit. */
    gp0(0xe1000000u);
    back(B);
    vram[2][102] = (uint16_t)(B | 0x8000u);
    gp0(0xe6000002u);
    gp0(cmd(0x60, 8, 16, 30)); gp0((0u << 16) | 100u); gp0((4u << 16) | 4u);
    assert(vram[2][102] == (uint16_t)(B | 0x8000u) && vram[1][101] == F);
    gp0(0xe6000001u);
    gp0(cmd(0x60, 8, 16, 30)); gp0((0u << 16) | 100u); gp0((4u << 16) | 4u);
    assert(vram[1][101] == (uint16_t)(F | 0x8000u));
    gp0(0xe6000000u);
    gp0(0xe1000000u | (2u << 7));
    vram[0][0] = (uint16_t)(F | 0x8000u);
    back(B);
    gp0(0x65808080u); gp0((0u << 16) | 100u); gp0(0u); gp0((1u << 16) | 1u);
    assert(vram[0][100] == (uint16_t)(F | 0x8000u));
    /* Non-raw textured rectangle (0x64) modulates: 0x404040 halves. */
    vram[0][0] = rgb15(20, 10, 4);
    gp0(0x64404040u); gp0((0u << 16) | 100u); gp0(0u); gp0((1u << 16) | 1u);
    assert(vram[0][100] == rgb15(10, 5, 2));
    puts("GPU_SEMI_PASS modes=4 prims=rect,poly,textured-rect stp_gate=1 mask=1 modulate=1");
    return 0;
}
