/* Load a TMD model, transform it through the GTE, and draw it with libgpu.
 *
 * This is the libgs-free path. No permissively licensed libgs exists, so the
 * model is walked here and emitted as ordinary POLY_G3 and POLY_G4 primitives
 * into an ordering table, which is what libgs would ultimately have done.
 *
 * Only the subset the target model uses is read: untextured polygons whose
 * flag bit 0x04 is clear, so each vertex costs one word holding a normal index
 * and a vertex index, after a single colour word. tools/tmd.py is the tested
 * reference for that layout and refuses everything outside it; this reader
 * refuses the same cases rather than drawing garbage.
 *
 * Lighting is not set up. Every vertex takes the primitive's own colour, so
 * the model renders flat in its authored colour rather than shaded. That is
 * enough to prove geometry reaches the screen, which is what this target is
 * for.
 *
 * As in render_quad.c: the ordering table is OT_TAG[] because a tag is 12
 * bytes on x86-64, ClearOTagR chains backwards so the head is the last entry,
 * and verification reads framebuffer 0 before PsyX_EndScene presents it.
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
#define OT_LENGTH 16

/* Models are not one size. The viewer measures each one's bounding box and
 * maps its largest extent onto FIT_EXTENT model units, centred on the origin,
 * so any TMD lands at the same place on screen at the same apparent size. */
#define FIT_EXTENT 200
#define MODEL_DISTANCE 600
#define PROJECTION_DISTANCE 320
#define YAW_STEP 24

#define CLEAR_R 0
#define CLEAR_G 0
#define CLEAR_B 255

#define TMD_ID 0x41
#define OBJECT_TABLE_OFFSET 12
#define OBJECT_ENTRY_SIZE 28
#define VECTOR_SIZE 8
#define MODE_QUAD 0x08
#define MODE_TEXTURED 0x04
#define FLAG_NO_LIGHT 0x04

#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

typedef void (*ReadPixelsFn)(int, int, int, int, unsigned, unsigned, void *);

static DISPENV g_disp;
static DRAWENV g_draw;
static OT_TAG g_ot[OT_LENGTH];

/* Primitives are emitted into a pool rather than reused, because each stays
 * linked in the ordering table until DrawOTag has walked it. */
#define MAX_PRIMS 256
static POLY_G4 g_prims[MAX_PRIMS];
static int g_prim_count;

static unsigned char *g_model;
static long g_model_size;

/* Fit derived from the vertex bounds: subtract the centre, then scale by
 * g_fit_num / g_fit_den. Integer throughout, as the GTE would want. */
static int g_center[3];
static int g_fit_num = 1;
static int g_fit_den = 1;

static unsigned read_u32(long offset) {
    const unsigned char *p = g_model + offset;
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int read_s16(long offset) {
    const unsigned char *p = g_model + offset;
    return (int)(short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static int load_model(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == 0) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    g_model_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (g_model_size < OBJECT_TABLE_OFFSET) {
        fclose(file);
        return 0;
    }
    g_model = (unsigned char *)malloc((size_t)g_model_size);
    if (g_model == 0 || fread(g_model, 1, (size_t)g_model_size, file) != (size_t)g_model_size) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return read_u32(0) == TMD_ID;
}

/* Measure every object's vertices and derive the centring and scale. */
static int fit_model(void) {
    const unsigned n_object = read_u32(8);
    int lo[3] = {32767, 32767, 32767};
    int hi[3] = {-32768, -32768, -32768};
    int extent = 0;
    unsigned object;
    int axis;

    for (object = 0; object < n_object; object++) {
        const long entry = OBJECT_TABLE_OFFSET + (long)object * OBJECT_ENTRY_SIZE;
        const long vertex_block = OBJECT_TABLE_OFFSET + (long)read_u32(entry + 0);
        const unsigned n_vert = read_u32(entry + 4);
        unsigned index;

        if (vertex_block + (long)n_vert * VECTOR_SIZE > g_model_size) {
            return 0;
        }
        for (index = 0; index < n_vert; index++) {
            for (axis = 0; axis < 3; axis++) {
                int value = read_s16(vertex_block + index * VECTOR_SIZE + axis * 2);
                if (value < lo[axis]) lo[axis] = value;
                if (value > hi[axis]) hi[axis] = value;
            }
        }
    }
    for (axis = 0; axis < 3; axis++) {
        if (hi[axis] < lo[axis]) {
            return 0;               /* no vertices at all */
        }
        g_center[axis] = (lo[axis] + hi[axis]) / 2;
        if (hi[axis] - lo[axis] > extent) {
            extent = hi[axis] - lo[axis];
        }
    }
    g_fit_num = FIT_EXTENT;
    g_fit_den = extent > 0 ? extent : 1;
    return 1;
}

/* Transform one model vertex to screen space.
 *
 * RotTransPers takes an int* for the packed screen coordinate, not a long*.
 * On x86-64 a long is eight bytes, so using one corrupts the neighbouring
 * entry. This is the same width hazard as the ordering table, in a third
 * guise. */
static void project(long vertex_block, int index, int *sxy) {
    SVECTOR v;
    long point;
    long flag;

    v.vx = (short)((read_s16(vertex_block + index * VECTOR_SIZE + 0) - g_center[0]) * g_fit_num / g_fit_den);
    v.vy = (short)((read_s16(vertex_block + index * VECTOR_SIZE + 2) - g_center[1]) * g_fit_num / g_fit_den);
    v.vz = (short)((read_s16(vertex_block + index * VECTOR_SIZE + 4) - g_center[2]) * g_fit_num / g_fit_den);
    v.pad = 0;

    RotTransPers(&v, sxy, &point, &flag);
}

/* Walk the object's primitives and add each as a POLY_G3 or POLY_G4. */
static int draw_object(long entry) {
    const unsigned vert_top = read_u32(entry + 0);
    const unsigned n_vert = read_u32(entry + 4);
    const unsigned n_normal = read_u32(entry + 12);
    const unsigned prim_top = read_u32(entry + 16);
    const unsigned n_prim = read_u32(entry + 20);
    /* Offsets resolve against the object table, not the file base. */
    const long vertex_block = OBJECT_TABLE_OFFSET + (long)vert_top;
    long offset = OBJECT_TABLE_OFFSET + (long)prim_top;
    unsigned index;

    for (index = 0; index < n_prim; index++) {
        unsigned char ilen = g_model[offset + 1];
        unsigned char flag = g_model[offset + 2];
        unsigned char mode = g_model[offset + 3];
        int corners = (mode & MODE_QUAD) ? 4 : 3;
        unsigned char red, green, blue;
        int sxy[4];
        int corner;
        POLY_G4 *prim;

        if ((mode >> 5) != 1 || (mode & MODE_TEXTURED) || (flag & FLAG_NO_LIGHT)) {
            fprintf(stderr, "render_tmd: primitive %u uses an unsupported layout "
                            "(mode 0x%02X flag 0x%02X)\n", index, mode, flag);
            return 0;
        }
        if (ilen != (unsigned char)(1 + corners)) {
            fprintf(stderr, "render_tmd: primitive %u has ilen %u, expected %d\n",
                    index, ilen, 1 + corners);
            return 0;
        }
        if (g_prim_count + 1 > MAX_PRIMS) {
            fprintf(stderr, "render_tmd: more than %d primitives\n", MAX_PRIMS);
            return 0;
        }

        red = g_model[offset + 4];
        green = g_model[offset + 5];
        blue = g_model[offset + 6];

        for (corner = 0; corner < corners; corner++) {
            long pair = offset + 8 + corner * 4;
            unsigned normal_index = (unsigned)g_model[pair] | ((unsigned)g_model[pair + 1] << 8);
            unsigned vertex_index = (unsigned)g_model[pair + 2] | ((unsigned)g_model[pair + 3] << 8);
            if (vertex_index >= n_vert || normal_index >= n_normal) {
                fprintf(stderr, "render_tmd: primitive %u index out of range\n", index);
                return 0;
            }
            project(vertex_block, (int)vertex_index, &sxy[corner]);
        }

        prim = &g_prims[g_prim_count++];
        /* No lighting is set up, so every corner takes the primitive's colour. */
        if (corners == 4) {
            POLY_G4 *quad = prim;
            setPolyG4(quad);
            setRGB0(quad, red, green, blue);
            setRGB1(quad, red, green, blue);
            setRGB2(quad, red, green, blue);
            setRGB3(quad, red, green, blue);
            setXY4(quad,
                   (short)(sxy[0] & 0xFFFF), (short)(sxy[0] >> 16),
                   (short)(sxy[1] & 0xFFFF), (short)(sxy[1] >> 16),
                   (short)(sxy[2] & 0xFFFF), (short)(sxy[2] >> 16),
                   (short)(sxy[3] & 0xFFFF), (short)(sxy[3] >> 16));
            addPrim(&g_ot[1], quad);
        } else {
            POLY_G3 *tri = (POLY_G3 *)prim;
            setPolyG3(tri);
            setRGB0(tri, red, green, blue);
            setRGB1(tri, red, green, blue);
            setRGB2(tri, red, green, blue);
            setXY3(tri,
                   (short)(sxy[0] & 0xFFFF), (short)(sxy[0] >> 16),
                   (short)(sxy[1] & 0xFFFF), (short)(sxy[1] >> 16),
                   (short)(sxy[2] & 0xFFFF), (short)(sxy[2] >> 16));
            addPrim(&g_ot[1], tri);
        }
        offset += 4 + (long)ilen * 4;
    }
    return 1;
}

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
    read_pixels(x * window_w / SCREEN_W, window_h - 1 - (y * window_h / SCREEN_H),
                1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    *out = pack_vram(rgba);
    return 1;
}

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
               frame + (size_t)(window_h - 1 - row) * window_w * 4, (size_t)window_w * 4);
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
    const char *model_path = 0;
    const char *screenshot_path = 0;
    int frames = 1;
    int frame;
    ReadPixelsFn read_pixels;
    MATRIX rotation;
    SVECTOR angle;
    VECTOR translation;
    unsigned inside = 0;
    unsigned outside = 0;
    unsigned n_object;
    unsigned object;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
            if (frames < 1) {
                fprintf(stderr, "render_tmd: --frames must be at least 1\n");
                return 2;
            }
        } else if (model_path == 0) {
            model_path = argv[i];
        } else {
            fprintf(stderr, "usage: %s MODEL.tmd [--frames N] [--screenshot PATH]\n", argv[0]);
            return 2;
        }
    }
    if (model_path == 0) {
        fprintf(stderr, "usage: %s MODEL.tmd [--frames N] [--screenshot PATH]\n", argv[0]);
        return 2;
    }
    if (!load_model(model_path)) {
        fprintf(stderr, "render_tmd: %s is not a readable TMD\n", model_path);
        return 1;
    }
    if (!fit_model()) {
        fprintf(stderr, "render_tmd: %s has no vertices to fit\n", model_path);
        return 1;
    }

    PsyX_Initialise("musashi_render_tmd", SCREEN_W, SCREEN_H, 0);
    if (SDL_GL_GetCurrentWindow() == NULL) {
        fprintf(stderr, "musashi_render_tmd: PsyX initialisation failed (no window), skipping\n");
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

    InitGeom();
    SetGeomOffset(SCREEN_W / 2, SCREEN_H / 2);
    SetGeomScreen(PROJECTION_DISTANCE);

    read_pixels = (ReadPixelsFn)SDL_GL_GetProcAddress("glReadPixels");
    n_object = read_u32(8);

    /* The model is tilted a little and turns YAW_STEP each frame, so a run
     * with --frames shows every side. Each frame rebuilds the ordering table
     * from scratch; primitives are not reusable once linked into it. */
    for (frame = 0; frame < frames; frame++) {
        angle.vx = 256;
        angle.vy = (short)(512 + frame * YAW_STEP);
        angle.vz = 0;
        angle.pad = 0;
        RotMatrix(&angle, &rotation);
        translation.vx = 0;
        translation.vy = 0;
        translation.vz = MODEL_DISTANCE;
        TransMatrix(&rotation, &translation);
        SetRotMatrix(&rotation);
        SetTransMatrix(&rotation);

        ClearOTagR((u_long *)g_ot, OT_LENGTH);
        g_prim_count = 0;
        for (object = 0; object < n_object; object++) {
            if (!draw_object(OBJECT_TABLE_OFFSET + (long)object * OBJECT_ENTRY_SIZE)) {
                musashi_psyx_shutdown();
                return 1;
            }
        }
        DrawOTag((u_long *)&g_ot[OT_LENGTH - 1]);
        DrawSync(0);
        if (frame + 1 < frames) {
            PsyX_EndScene();
        }
    }
    printf("PRIMITIVES %d\n", g_prim_count);

    if (read_pixels == 0
        || !sample(read_pixels, SCREEN_W / 2, SCREEN_H / 2, &inside)
        || !sample(read_pixels, 8, 8, &outside)) {
        fputs("render_tmd: could not read the framebuffer\n", stderr);
        PsyX_EndScene();
        musashi_psyx_shutdown();
        return 1;
    }
    if (screenshot_path != 0 && !save_screenshot(read_pixels, screenshot_path)) {
        fprintf(stderr, "render_tmd: could not write %s\n", screenshot_path);
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
