/* Minimal repro: does musashi_gpu_controller (pc_port/gpu_controller.c on
 * port/native-lane / main) apply PS1 semi-transparency?
 *
 *   cc -std=c99 -I<main>/pc_port/include gpu_semi_repro.c <main>/pc_port/gpu_controller.c
 *
 * For each abr mode (GP0 E1 bits 5-6) and each semi-transparent primitive
 * the BFM fades use (GP0 0x62 semi tile, 0x2A semi flat quad), draw an
 * opaque white 16x16 backdrop (0x60) and then the semi prim in grey
 * 0x404040 over it, and compare the stored pixel with the PS1 formula
 * (psx-spx "Semi Transparency": 0 B/2+F/2, 1 B+F, 2 B-F, 3 B+F/4, per
 * 5-bit channel, clamped). Exit 1 on any mismatch. Synthetic data only;
 * uses only the controller's public API and a trivial VRAM backend. */

#include "musashi_gpu_controller.h"

#include <stdio.h>
#include <string.h>

static uint16_t vram[512][1024];

static int ok(void *u) { (void)u; return 1; }
static int ok_word(void *u, uint32_t w) { (void)u; (void)w; return 1; }
static int ok_enable(void *u, int e) { (void)u; (void)e; return 1; }
static int ok_display(void *u, const MusashiGpuDisplayState *d) { (void)u; (void)d; return 1; }
static int fill(void *u, const MusashiGpuFill *f) {
    unsigned x, y;
    (void)u;
    for (y = 0; y < f->height; y++)
        for (x = 0; x < f->width; x++) vram[(f->y + y) & 511][(f->x + x) & 1023] = f->color;
    return 1;
}
static int store(void *u, uint16_t x, uint16_t y, uint16_t p) {
    (void)u;
    vram[y & 511][x & 1023] = p;
    return 1;
}
static int load(void *u, uint16_t x, uint16_t y, uint16_t *p) {
    (void)u;
    *p = vram[y & 511][x & 1023];
    return 1;
}

static uint16_t ps1(int mode, uint16_t b, uint16_t f) {
    uint16_t out = 0;
    int c;
    for (c = 0; c < 15; c += 5) {
        int bb = (b >> c) & 31, ff = (f >> c) & 31, o;
        o = mode == 0 ? (bb + ff) >> 1 : mode == 1 ? bb + ff : mode == 2 ? bb - ff : bb + (ff >> 2);
        if (o < 0) o = 0;
        if (o > 31) o = 31;
        out |= (uint16_t)(o << c);
    }
    return out;
}

int main(void) {
    static MusashiGpuController g;
    MusashiGpuBackend be;
    int mode, kind, bad = 0;
    memset(&be, 0, sizeof be);
    be.reset = ok; be.draw_mode = ok_word; be.display_enable = ok_enable;
    be.clear_fifo = ok; be.ready = ok; be.environment = ok_word; be.display = ok_display;
    be.fill_vram = fill; be.store_vram = store; be.read_vram = load;
    if (!musashi_gpu_controller_init(&g, &be)) { puts("init refused"); return 2; }
#define GP0(w) do { if (!musashi_gpu_controller_write32(&g, MUSASHI_GPU_GP0, (w))) \
                        { printf("GP0 %08x refused\n", (unsigned)(w)); return 2; } } while (0)
    GP0(0xE3000000u);             /* drawing area 0,0 */
    GP0(0xE4000000u | (511u << 10) | 1023u);
    GP0(0xE5000000u);             /* offset 0,0 */
    for (kind = 0; kind < 2; kind++)
        for (mode = 0; mode < 4; mode++) {
            uint16_t got, want;
            /* opaque white backdrop */
            GP0(0x60FFFFFFu); GP0(0x00000000u); GP0(0x00100010u);
            GP0(0xE1000000u | ((unsigned)mode << 5));
            if (kind == 0) {      /* 0x62: semi-transparent variable-size tile */
                GP0(0x62404040u); GP0(0x00000000u); GP0(0x00100010u);
            } else {              /* 0x2A: semi-transparent flat quad */
                GP0(0x2A404040u); GP0(0x00000000u); GP0(0x00000010u);
                GP0(0x00100000u); GP0(0x00100010u);
            }
            got = vram[4][4] & 0x7fff;
            want = ps1(mode, 0x7fff, 0x2108);   /* 0x404040 -> 8,8,8 */
            printf("%s abr=%d: stored %04x, PS1 %04x%s\n", kind ? "0x2A quad" : "0x62 tile",
                   mode, got, want, got == want ? "" : "   <-- semi-transparency not applied");
            bad += got != want;
        }
    printf("%d of 8 semi-transparent draws wrong\n", bad);
    return bad ? 1 : 0;
}
