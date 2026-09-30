/* Draw one flat quad and report the pixels that came back.
 *
 * This proves the path from decomp-owned C to actual pixels: ordering table,
 * primitive, DrawOTag, and a read of the rendered frame. It draws a single
 * frame and is not a game loop.
 *
 * The program only samples and prints. Judging the colours is left to the
 * harness, so the comparison logic stays unit-tested in tools/vram_pixel.py
 * rather than hiding in a binary that needs a display to run.
 *
 * `--screenshot PATH` additionally writes the frame as a BMP, for looking at
 * with human eyes. It is off by default so the automated check stays a pixel
 * comparison rather than something that quietly depends on file output.
 *
 * Two hazards this file had to learn the hard way, both recorded in
 * docs/PC-PORT.md: the ordering table must be OT_TAG[] (a tag is 12 bytes on
 * x86-64, not the one word PS1 source assumes), and the rendered frame cannot
 * be read through GR_ReadVRAM, whose backing array never receives GL output.
 * The frame is read from framebuffer 0 before PsyX_EndScene presents it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "PsyX/PsyX_public.h"
#include "psx/libgte.h"
#include "psx/libgpu.h"
#include "psx/libetc.h"
void musashi_psyx_shutdown(void); /* pc_port/psyx_shutdown.cpp */

#define SCREEN_W 320
#define SCREEN_H 240
#define OT_LENGTH 8

/* Quad corners, well inside the screen so the sample points are unambiguous. */
#define QUAD_X0 80
#define QUAD_Y0 60
#define QUAD_X1 240
#define QUAD_Y1 180

/* Red quad on a blue field. Blue rather than black, so a frame that was never
 * drawn at all cannot be mistaken for a correct clear. */
#define QUAD_R 248
#define QUAD_G 0
#define QUAD_B 0
#define CLEAR_R 0
#define CLEAR_G 0
#define CLEAR_B 255

#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

typedef void (*ReadPixelsFn)(int, int, int, int, unsigned, unsigned, void *);

static DISPENV g_disp;
static DRAWENV g_draw;
static OT_TAG g_ot[OT_LENGTH];
static POLY_F4 g_quad;

/* Pack an 8-bit GL sample the way PS1 VRAM stores it, so the harness judges
 * one format whether the pixel came from VRAM or from the framebuffer. */
static unsigned pack_vram(const unsigned char rgba[4]) {
    return (unsigned)(rgba[0] >> 3) | ((unsigned)(rgba[1] >> 3) << 5) | ((unsigned)(rgba[2] >> 3) << 10);
}

static int sample(ReadPixelsFn read_pixels, int x, int y, unsigned *out) {
    unsigned char rgba[4] = {0, 0, 0, 0};
    int window_w = 0;
    int window_h = 0;

    SDL_GetWindowSize(SDL_GL_GetCurrentWindow(), &window_w, &window_h);
    if (window_w <= 0 || window_h <= 0) {
        return 0;
    }
    /* PSX screen space to window pixels; GL's origin is the bottom-left. */
    read_pixels(x * window_w / SCREEN_W,
                window_h - 1 - (y * window_h / SCREEN_H),
                1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    *out = pack_vram(rgba);
    return 1;
}

/* Save the whole frame as a BMP. GL reads bottom-up, so rows are flipped. */
static int save_screenshot(ReadPixelsFn read_pixels, const char *path) {
    SDL_Surface *surface;
    unsigned char *frame;
    int window_w = 0;
    int window_h = 0;
    int row;

    SDL_GetWindowSize(SDL_GL_GetCurrentWindow(), &window_w, &window_h);
    if (window_w <= 0 || window_h <= 0) {
        return 0;
    }
    frame = (unsigned char *)malloc((size_t)window_w * window_h * 4);
    if (frame == 0) {
        return 0;
    }
    read_pixels(0, 0, window_w, window_h, GL_RGBA, GL_UNSIGNED_BYTE, frame);

    surface = SDL_CreateRGBSurfaceWithFormat(0, window_w, window_h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (surface == 0) {
        free(frame);
        return 0;
    }
    for (row = 0; row < window_h; row++) {
        memcpy((unsigned char *)surface->pixels + (size_t)row * surface->pitch,
               frame + (size_t)(window_h - 1 - row) * window_w * 4,
               (size_t)window_w * 4);
    }
    if (SDL_SaveBMP(surface, path) != 0) {
        SDL_FreeSurface(surface);
        free(frame);
        return 0;
    }
    SDL_FreeSurface(surface);
    free(frame);
    return 1;
}

int main(int argc, char **argv) {
    ReadPixelsFn read_pixels;
    const char *screenshot_path = 0;
    unsigned inside = 0;
    unsigned outside = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--screenshot PATH]\n", argv[0]);
            return 2;
        }
    }

    PsyX_Initialise("musashi_render_quad", SCREEN_W, SCREEN_H, 0);
    if (SDL_GL_GetCurrentWindow() == NULL) {
        fprintf(stderr, "musashi_render_quad: PsyX initialisation failed (no window), skipping\n");
        return 77;
    }

    ResetGraph(0);
    SetDefDispEnv(&g_disp, 0, 0, SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&g_draw, 0, 0, SCREEN_W, SCREEN_H);
    g_draw.isbg = 1;
    setRGB0(&g_draw, CLEAR_R, CLEAR_G, CLEAR_B);
    PutDispEnv(&g_disp);
    PutDrawEnv(&g_draw);
    SetDispMask(1);

    /* ClearOTagR chains ot[i] -> ot[i-1], so ot[0] is the terminator and the
     * head is the last entry. Drawing from ot[0] walks nothing. */
    ClearOTagR((u_long *)g_ot, OT_LENGTH);

    setPolyF4(&g_quad);
    setRGB0(&g_quad, QUAD_R, QUAD_G, QUAD_B);
    setXY4(&g_quad,
           QUAD_X0, QUAD_Y0,
           QUAD_X1, QUAD_Y0,
           QUAD_X0, QUAD_Y1,
           QUAD_X1, QUAD_Y1);
    addPrim(&g_ot[1], &g_quad);

    DrawOTag((u_long *)&g_ot[OT_LENGTH - 1]);
    DrawSync(0);

    read_pixels = (ReadPixelsFn)SDL_GL_GetProcAddress("glReadPixels");
    if (read_pixels == 0
        || !sample(read_pixels, (QUAD_X0 + QUAD_X1) / 2, (QUAD_Y0 + QUAD_Y1) / 2, &inside)
        || !sample(read_pixels, 8, 8, &outside)) {
        fputs("musashi_render_quad: could not read the framebuffer\n", stderr);
        PsyX_EndScene();
        musashi_psyx_shutdown();
        return 1;
    }

    if (screenshot_path != 0 && !save_screenshot(read_pixels, screenshot_path)) {
        fprintf(stderr, "musashi_render_quad: could not write %s\n", screenshot_path);
        PsyX_EndScene();
        musashi_psyx_shutdown();
        return 1;
    }

    printf("PIXEL inside 0x%04X\n", inside);
    printf("PIXEL outside 0x%04X\n", outside);
    fflush(stdout);

    PsyX_EndScene();
    musashi_psyx_shutdown();
    return 0;
}
