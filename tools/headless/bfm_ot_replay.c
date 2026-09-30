/* Replays a GPU display list captured by bfm_swap_capture (ot.log) over a
 * captured VRAM image, through the platform's PS1 reference rasteriser and
 * the gl renderer (OSMesa, headless), plus an "opaque" variant with every
 * semi-transparency bit cleared (what a rasteriser that ignores GP0 bit 1
 * produces). Used to diagnose the headless-boot fade.
 *
 *   bfm_ot_replay LIBOSMESA VRAM.png LIST FRAME OUTDIR
 *                 [clip_x clip_y clip_w clip_h [offset_x offset_y [semi-only]]]
 *
 * LIST is an ot.log (display-list walk) or a prims.log (the controller's
 * committed prims, BFM_CAPTURE_GPU); "semi-only" replays just the
 * semi-transparent prims of the frame over VRAM.png (to isolate a fade).
 * The reference rasteriser draws rects; an axis-aligned F4 quad is given to
 * it as the equivalent TILE (the gl backend always gets the real prim).
 *
 * VRAM.png is a 1024x512 capture (vram_NNNNNN.png); OUTDIR gets before.png,
 * ref.png, gl.png, opaque.png of the clip rect. Prints the decoded prims,
 * how many pixels of each result differ from its most common colour
 * (i.e. what survives of the splash text), and ref-vs-gl agreement. */

#include "backends/gl/bfm_gl_core.h"
#include "backends/gl/bfm_gl_exec.h"
#include "bfm_plat_image.h"
#include "psyq/bfm_psyq_compat.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bfm_plat_texture_png_decode_memory(const uint8_t *data, size_t size, uint32_t *w,
                                       uint32_t *h, uint8_t **rgba);

#define LOADFN(dst, obj)                                                      \
    do {                                                                      \
        void *o_ = (obj);                                                     \
        memcpy(&(dst), &o_, sizeof o_);                                       \
    } while (0)
typedef void *(*GetProcFn)(const char *);
static GetProcFn osm_getproc;
static void *getproc(const char *n) { return osm_getproc(n); }

static BfmGlVram input, ref, opaque, glv;

static int load_vram(const char *path) {
    FILE *f = fopen(path, "rb");
    uint8_t *data, *rgba = NULL;
    long n;
    uint32_t w, h, i;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t *)malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 0; }
    fclose(f);
    if (bfm_plat_texture_png_decode_memory(data, (size_t)n, &w, &h, &rgba) != 0 ||
        w != 1024 || h != 512) return 0;
    for (i = 0; i < w * h; i++)   /* inverse of bfm_plat_vram15_to_rgb */
        input.px[i] = (uint16_t)((rgba[i * 4] >> 3) | ((rgba[i * 4 + 1] >> 3) << 5) |
                                 ((rgba[i * 4 + 2] >> 3) << 10));
    free(rgba);
    free(data);
    return 1;
}

/* Words of the first list logged for `frame`. */
static size_t load_list(const char *path, unsigned long frame, uint32_t *words, size_t max,
                        size_t *node_start, size_t *nodes) {
    FILE *f = fopen(path, "r");
    char line[4096];
    int in = 0;
    size_t n = 0;
    *nodes = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long fr;
        unsigned head;
        if (sscanf(line, "frame %lu head %x", &fr, &head) == 2) {
            if (in) break;
            in = fr == frame;
            continue;
        }
        if (in && !strncmp(line, " node ", 6)) {
            char *p = strchr(line, ':');
            node_start[(*nodes)++] = n;
            while (p && *++p) {
                char *end;
                unsigned long v = strtoul(p, &end, 16);
                if (end == p) break;
                if (n < max) words[n++] = (uint32_t)v;
                p = end - 1;
            }
        }
    }
    fclose(f);
    return n;
}

/* prims.log: "frame N mode M abr=.. op=OP ... words w0 .. w11" */
static size_t load_prims(const char *path, unsigned long frame, BfmPlatPrim *out, size_t max,
                         int semi_only) {
    FILE *f = fopen(path, "r");
    char line[4096];
    size_t n = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f) && n < max) {
        unsigned long fr;
        unsigned mode, op;
        char *w;
        uint32_t words[12];
        int k;
        uint16_t state;
        if (sscanf(line, "frame %lu mode %x", &fr, &mode) != 2 || fr != frame) continue;
        if (!(w = strstr(line, " op=")) || sscanf(w, " op=%x", &op) != 1 || op < 0x20) continue;
        if (!(w = strstr(line, " words "))) continue;
        w += 7;
        for (k = 0; k < 12; k++) {
            char *end;
            words[k] = (uint32_t)strtoul(w, &end, 16);
            w = end;
        }
        state = (uint16_t)(mode & 0x3fff);
        if (bfm_psyq_decode_packet(words, 12, &state, &out[n], 1) != 1) continue;
        if (semi_only && !(out[n].flags & BFM_PRIM_FLAG_SEMI_TRANS)) continue;
        n++;
    }
    fclose(f);
    return n;
}

/* An axis-aligned quad as the TILE with the same pixels (reference only). */
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

static void save(const char *dir, const char *name, const BfmGlVram *v, const BfmPlatRect *r) {
    char path[1024];
    uint8_t *rgb = (uint8_t *)malloc((size_t)r->w * r->h * 3);
    uint16_t *row = (uint16_t *)malloc((size_t)r->w * 2);
    int y;
    for (y = 0; y < r->h; y++) {
        memcpy(row, &v->px[(size_t)(r->y + y) * 1024 + r->x], (size_t)r->w * 2);
        bfm_plat_vram15_to_rgb(row, (size_t)r->w, rgb + (size_t)y * r->w * 3);
    }
    snprintf(path, sizeof path, "%s/%s", dir, name);
    bfm_plat_png_write(path, (uint32_t)r->w, (uint32_t)r->h, 3, rgb);
    free(rgb);
    free(row);
}

/* Pixels differing from the rect's most common colour; distinct colours. */
static void stats(const char *what, const BfmGlVram *v, const BfmPlatRect *r) {
    static uint32_t hist[32768];
    int x, y, best = 0, distinct = 0;
    long other;
    memset(hist, 0, sizeof hist);
    for (y = 0; y < r->h; y++)
        for (x = 0; x < r->w; x++) hist[v->px[(size_t)(r->y + y) * 1024 + r->x + x] & 0x7fff]++;
    for (x = 0; x < 32768; x++) {
        if (hist[x]) distinct++;
        if (hist[x] > hist[best]) best = x;
    }
    other = (long)r->w * r->h - hist[best];
    printf("%-8s most common %04x (%u px), %ld px differ from it, %d distinct colours\n", what,
           best, hist[best], other, distinct);
}

int main(int argc, char **argv) {
    static uint32_t words[65536];
    static size_t starts[16384];
    static BfmPlatPrim prims[4096];
    static const int attribs[] = {0x22, 0x1908, 0x30, 0, 0x31, 0, 0x33, 0x34, 0x36, 3, 0x37, 3, 0};
    static uint8_t winbuf[64 * 64 * 4];
    BfmPlatDrawEnv env;
    BfmPlatRect clip = {0, 0, 640, 480}, all = {0, 0, 1024, 512};
    size_t nw, nn, np = 0, i;
    uint16_t state = 0;
    unsigned long frame;
    int k, diff = 0, semi = 0;
    if (argc < 6) {
        fprintf(stderr, "usage: %s LIBOSMESA VRAM.png OT.log FRAME OUTDIR [cx cy cw ch [ox oy]]\n", argv[0]);
        return 2;
    }
    frame = strtoul(argv[4], NULL, 10);
    if (argc >= 10) {
        clip.x = (int16_t)atoi(argv[6]); clip.y = (int16_t)atoi(argv[7]);
        clip.w = (int16_t)atoi(argv[8]); clip.h = (int16_t)atoi(argv[9]);
    }
    memset(&env, 0, sizeof env);
    env.clip = clip;
    if (argc >= 12) { env.offset_x = (int16_t)atoi(argv[10]); env.offset_y = (int16_t)atoi(argv[11]); }
    if (!load_vram(argv[2])) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    if (strstr(argv[3], "prims")) {
        np = load_prims(argv[3], frame, prims, 4096, argc >= 13 && !strcmp(argv[12], "semi-only"));
        nn = nw = 0;
    } else {
        nw = load_list(argv[3], frame, words, 65536, starts, &nn);
        for (i = 0; i < nn; i++) {
            size_t end = i + 1 < nn ? starts[i + 1] : nw;
            np += bfm_psyq_decode_packet(words + starts[i], end - starts[i], &state, prims + np,
                                         4096 - np);
        }
    }
    if (!np) { fprintf(stderr, "frame %lu: no prims in %s\n", frame, argv[3]); return 1; }
    printf("frame %lu: %zu nodes, %zu words, %zu prims\n", frame, nn, nw, np);
    for (i = 0; i < np; i++) {
        const BfmPlatPrim *p = &prims[i];
        semi += (p->flags & BFM_PRIM_FLAG_SEMI_TRANS) != 0;
        printf("  prim %zu kind %d flags %x abr %d rgb %02x%02x%02x xy %d,%d wh %u,%u\n", i, p->kind,
               p->flags, (p->tpage >> 5) & 3, p->v[0].r, p->v[0].g, p->v[0].b, p->v[0].x, p->v[0].y,
               p->w, p->h);
    }
    ref = input;
    opaque = input;
    for (i = 0; i < np; i++) {
        BfmPlatPrim q = prims[i], t;
        if (quad_as_tile(&q, &t)) {
            printf("  prim %zu: axis-aligned F4 given to the reference as TILE %ux%u\n", i, t.w, t.h);
            q = t;
        } else if (q.kind != BFM_PRIM_TILE && q.kind != BFM_PRIM_SPRITE && q.kind != BFM_PRIM_FILL) {
            printf("  prim %zu: kind %d not in the reference rasteriser (gl only)\n", i, q.kind);
            continue;
        }
        bfm_gl_ref_draw(&ref, &env, &q);
        q.flags &= (uint8_t)~BFM_PRIM_FLAG_SEMI_TRANS;
        bfm_gl_ref_draw(&opaque, &env, &q);
    }
    /* the gl backend on OSMesa */
    {
        void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        void *(*create)(const int *, void *);
        unsigned char (*make)(void *, void *, unsigned, int, int);
        void *ctx;
        char err[256];
        BfmGlExec *x;
        BfmGlBatch b;
        if (!h) { fprintf(stderr, "no OSMesa: %s\n", dlerror()); return 1; }
        LOADFN(create, dlsym(h, "OSMesaCreateContextAttribs"));
        LOADFN(make, dlsym(h, "OSMesaMakeCurrent"));
        LOADFN(osm_getproc, dlsym(h, "OSMesaGetProcAddress"));
        if (!create || !make || !osm_getproc || !(ctx = create(attribs, NULL)) ||
            !make(ctx, winbuf, 0x1401, 64, 64)) { fprintf(stderr, "no GL context\n"); return 1; }
        if (!(x = bfm_gl_exec_create(getproc, 1, err, sizeof err))) { fprintf(stderr, "%s\n", err); return 1; }
        bfm_gl_vram_init(&glv);
        bfm_gl_batch_init(&b);
        b.vram = &glv;
        bfm_gl_vram_upload(&glv, &all, input.px);
        bfm_gl_batch_add_copy(&b, &all);
        bfm_gl_batch_set_env(&b, &env);
        bfm_gl_batch_add(&b, prims, np);
        bfm_gl_batch_add_resolve(&b, &clip);
        bfm_gl_exec_sync_vram(x, &glv);
        bfm_gl_exec_run(x, &b, 0);
        bfm_gl_batch_clear(&b);
        bfm_gl_exec_sync_vram(x, &glv);
        for (k = 0; k < clip.h; k++) {
            int xx;
            for (xx = 0; xx < clip.w; xx++) {
                size_t o = (size_t)(clip.y + k) * 1024 + clip.x + xx;
                diff += (glv.px[o] & 0x7fff) != (ref.px[o] & 0x7fff);
            }
        }
        bfm_gl_batch_free(&b);
        bfm_gl_exec_destroy(x);
    }
    save(argv[5], "before.png", &input, &clip);
    save(argv[5], "ref.png", &ref, &clip);
    save(argv[5], "gl.png", &glv, &clip);
    save(argv[5], "opaque.png", &opaque, &clip);
    stats("before", &input, &clip);
    stats("ref", &ref, &clip);
    stats("gl", &glv, &clip);
    stats("opaque", &opaque, &clip);
    printf("semi-transparent prims: %d of %zu; gl vs reference: %d of %d px differ (colour)\n",
           semi, np, diff, clip.w * clip.h);
    return 0;
}
