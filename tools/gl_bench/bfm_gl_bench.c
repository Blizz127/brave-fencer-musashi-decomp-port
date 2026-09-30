/* Benchmark for the `gl` renderer: frame time of a synthetic heavy frame on
 * a headless OSMesa context (llvmpipe here; any OSMesa build works).
 * The default frame (4000 prims, ~1 Mpx of fill) is heavy but within what
 * the PS1 GPU itself could draw at 30 fps.
 *
 *   bfm_gl_bench LIBOSMESA [--prims N] [--frames N] [--scales 1,4]
 *                [--feedback] [--mask] [--size WxH]
 *
 * Each frame is a double-buffered 320x240 hor+ 16:9 frame (margin 53): a
 * clear, then N prims in a fixed mix modelled on a busy 3D scene (small
 * gouraud-textured quads from 4-bit pages, 8-bit triangles, flat/gouraud
 * polys, semi-transparent 15-bit quads, sprites, lines, semi tiles),
 * optionally with render feedback (4 off-screen render-to-texture passes)
 * and mask-check prims. Reported per scale: CPU batching, GPU execution
 * (run + wait) and present, mean and median over the frames after a
 * warm-up. Synthetic data only. */

#include "backends/gl/bfm_gl_core.h"
#include "backends/gl/bfm_gl_exec.h"

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOADFN(dst, obj)                                                      \
    do {                                                                      \
        void *o_ = (obj);                                                     \
        memcpy(&(dst), &o_, sizeof o_);                                       \
    } while (0)

typedef void *OSMesaContext;
typedef OSMesaContext (*CreateAttribsFn)(const int *, OSMesaContext);
typedef unsigned char (*MakeCurrentFn)(OSMesaContext, void *, unsigned, int, int);
typedef void *(*GetProcFn)(const char *);
static GetProcFn osmesa_getproc;
static void *getproc(const char *n) { return osmesa_getproc(n); }

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}
static int rr(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static uint16_t tp(int d, int abr, int x, int y) {
    return (uint16_t)(((d & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6));
}

static double cpu_ms(void) {   /* this process, all llvmpipe threads */
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* The frame's prims, generated once (same work every frame). */
static BfmPlatPrim *make_scene(int n, int mask) {
    BfmPlatPrim *p = (BfmPlatPrim *)calloc((size_t)n, sizeof *p);
    int i, j;
    for (i = 0; i < n; i++) {
        BfmPlatPrim *q = &p[i];
        int pick = rr(0, 99), cx = rr(-40, 360), cy = rr(-10, 250), s = rr(3, 12);
        int kind;
        if (pick < 50) kind = BFM_PRIM_POLY_GT4;
        else if (pick < 65) kind = BFM_PRIM_POLY_GT3;
        else if (pick < 75) kind = BFM_PRIM_POLY_G3;
        else if (pick < 85) kind = BFM_PRIM_POLY_FT4;
        else if (pick < 90) kind = BFM_PRIM_SPRITE;
        else if (pick < 95) kind = BFM_PRIM_LINE_G;
        else kind = BFM_PRIM_TILE;
        q->kind = (uint8_t)kind;
        for (j = 0; j < 4; j++) {
            q->v[j].x = (int16_t)(cx + rr(-s, s));
            q->v[j].y = (int16_t)(cy + rr(-s, s));
            q->v[j].r = (uint8_t)rr(64, 200);
            q->v[j].g = (uint8_t)rr(64, 200);
            q->v[j].b = (uint8_t)rr(64, 200);
            q->v[j].u = (uint8_t)rr(0, 63);
            q->v[j].v = (uint8_t)rr(0, 63);
        }
        switch (kind) {
        case BFM_PRIM_POLY_GT4:
            q->tpage = tp(0, 0, 640 + 64 * rr(0, 3), 0);
            q->clut = (uint16_t)((480 << 6) | rr(0, 15));
            break;
        case BFM_PRIM_POLY_GT3:
            q->tpage = tp(1, 0, 896, 256);
            q->clut = (uint16_t)(496 << 6);
            break;
        case BFM_PRIM_POLY_FT4:
            q->tpage = tp(2, rr(0, 1), 640, 256);
            q->flags = BFM_PRIM_FLAG_SEMI_TRANS;
            break;
        case BFM_PRIM_SPRITE:
            q->tpage = tp(2, 0, 640, 256);
            q->w = (uint16_t)rr(8, 24);
            q->h = (uint16_t)rr(8, 24);
            q->flags = BFM_PRIM_FLAG_RAW_TEXTURE;
            break;
        case BFM_PRIM_TILE:
            q->w = (uint16_t)rr(8, 32);
            q->h = (uint16_t)rr(8, 32);
            q->flags = BFM_PRIM_FLAG_SEMI_TRANS;
            q->tpage = tp(2, 1, 0, 0);
            break;
        default:
            break;
        }
        if (mask && rr(0, 9) == 0)
            q->flags |= (uint8_t)(rr(0, 1) ? BFM_PRIM_FLAG_MASK_CHECK : BFM_PRIM_FLAG_MASK_SET);
    }
    return p;
}

/* Pixels the frame asks the rasteriser for at 1x (polygon areas). */
static double scene_fill(const BfmPlatPrim *p, int n) {
    double a = 0;
    int i;
    for (i = 0; i < n; i++) {
        const BfmPlatVertex *v = p[i].v;
        switch (p[i].kind) {
        case BFM_PRIM_POLY_GT4: case BFM_PRIM_POLY_FT4:   /* 0 1 / 2 3 */
            a += 0.5 * fabs((double)(v[1].x - v[0].x) * (v[2].y - v[0].y) -
                            (double)(v[2].x - v[0].x) * (v[1].y - v[0].y));
            a += 0.5 * fabs((double)(v[3].x - v[1].x) * (v[2].y - v[1].y) -
                            (double)(v[2].x - v[1].x) * (v[3].y - v[1].y));
            break;
        case BFM_PRIM_POLY_GT3: case BFM_PRIM_POLY_G3:
            a += 0.5 * fabs((double)(v[1].x - v[0].x) * (v[2].y - v[0].y) -
                            (double)(v[2].x - v[0].x) * (v[1].y - v[0].y));
            break;
        case BFM_PRIM_SPRITE: case BFM_PRIM_TILE:
            a += (double)p[i].w * p[i].h;
            break;
        default:
            a += abs(v[1].x - v[0].x) + abs(v[1].y - v[0].y) + 1;
            break;
        }
    }
    return a;
}

static void fill_textures(BfmGlVram *v) {
    int x, y;
    for (y = 0; y < 512; y++)
        for (x = 512; x < 1024; x++)
            v->px[y * 1024 + x] = (uint16_t)(rnd() | (y >= 480 ? 1 : 0));
    bfm_gl_vram_mark(v, 512, 0, 512, 512);
}

int main(int argc, char **argv) {
    static const int attribs[] = {0x22, 0x1908, 0x30, 0, 0x31, 0, 0x33, 0x34,
                                  0x36, 3, 0x37, 3, 0};
    int prims = 4000, frames = 30, feedback = 0, mask = 0, opaque = 0, win_w = 1280, win_h = 720;
    int scales[8] = {1, 4}, nscales = 2, si, i, f;
    const char *lib;
    void *h;
    CreateAttribsFn create;
    MakeCurrentFn make;
    OSMesaContext ctx;
    uint8_t *winbuf;
    BfmPlatPrim *scene;
    static BfmGlVram vram;
    char err[512];
    if (argc < 2) {
        fprintf(stderr, "usage: %s LIBOSMESA [--prims N] [--frames N] [--scales 1,4] "
                        "[--feedback] [--mask] [--size WxH]\n", argv[0]);
        return 2;
    }
    lib = argv[1];
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--prims") && i + 1 < argc) prims = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--feedback")) feedback = 1;
        else if (!strcmp(argv[i], "--mask")) mask = 1;
        else if (!strcmp(argv[i], "--opaque")) opaque = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &win_w, &win_h);
        else if (!strcmp(argv[i], "--scales") && i + 1 < argc) {
            char *s = argv[++i];
            nscales = 0;
            while (*s && nscales < 8) {
                scales[nscales++] = (int)strtol(s, &s, 10);
                if (*s == ',') s++;
            }
        }
    }
    if (frames < 1) frames = 1;
    if (!(h = dlopen(lib, RTLD_NOW | RTLD_LOCAL))) {
        printf("skip: %s\n", dlerror());
        return 0;
    }
    LOADFN(create, dlsym(h, "OSMesaCreateContextAttribs"));
    LOADFN(make, dlsym(h, "OSMesaMakeCurrent"));
    LOADFN(osmesa_getproc, dlsym(h, "OSMesaGetProcAddress"));
    if (!create || !make || !osmesa_getproc || !(ctx = create(attribs, NULL))) {
        printf("skip: no OSMesa GL 3.3 core context\n");
        return 0;
    }
    winbuf = (uint8_t *)malloc((size_t)win_w * win_h * 4);
    if (!winbuf || !make(ctx, winbuf, 0x1401, win_w, win_h)) {
        printf("FAIL: OSMesaMakeCurrent\n");
        return 1;
    }
    {
        typedef const unsigned char *(*GetStringFn)(unsigned);
        GetStringFn gs;
        LOADFN(gs, getproc("glGetString"));
        printf("renderer: %s | %s\n", gs(0x1F01), gs(0x1F02));
    }
    {
        double l[3] = {0, 0, 0};
        FILE *la = fopen("/proc/loadavg", "r");
        if (la) {
            if (fscanf(la, "%lf %lf %lf", &l[0], &l[1], &l[2]) != 3) l[0] = 0;
            fclose(la);
        }
        printf("host load average %.1f (numbers are wall-clock; cpu ms is process CPU "
               "time incl. llvmpipe threads)\n", l[0]);
    }
    printf("frame: %d prims, 320x240 hor+ 16:9, window %dx%d, %d frames%s%s, LP_NUM_THREADS=%s\n",
           prims, win_w, win_h, frames, feedback ? ", feedback" : "", mask ? ", mask" : "",
           getenv("LP_NUM_THREADS") ? getenv("LP_NUM_THREADS") : "(default)");
    scene = make_scene(prims, mask);
    if (opaque)
        for (i = 0; i < prims; i++) scene[i].flags &= (uint8_t)~BFM_PRIM_FLAG_SEMI_TRANS;
    printf("fill: %.2f Mpx per frame at 1x (%.1fx the 320x240 screen; a PS1 GPU fills ~2 Mpx "
           "per 30 fps frame)\n", scene_fill(scene, prims) / 1e6,
           scene_fill(scene, prims) / (320.0 * 240.0));
    printf("%-5s %10s %10s %10s %10s %10s %8s %9s %8s %7s %7s\n", "scale", "batch ms", "gpu ms",
           "present ms", "total ms", "median ms", "fps", "cpu ms", "ns/px", "cmds", "draws");
    for (si = 0; si < nscales; si++) {
        BfmGlExec *x = bfm_gl_exec_create(getproc, scales[si], err, sizeof err);
        BfmGlBatch b, ov;
        BfmPlatRect disp = {0, 0, 320, 240};
        double *tot = (double *)calloc((size_t)frames, sizeof(double));
        double sb = 0, sg = 0, sp = 0, scpu = 0, fill = scene_fill(scene, prims);
        int warm = 3, cmds = 0, ndraws = 0;
        if (!x) {
            printf("FAIL: %s\n", err);
            return 1;
        }
        bfm_gl_vram_init(&vram);
        fill_textures(&vram);
        bfm_gl_batch_init(&b);
        bfm_gl_batch_init(&ov);
        ov.overlay = 1;
        b.vram = &vram;
        bfm_gl_batch_set_display(&b, &disp, 53);
        bfm_gl_batch_set_display(&ov, &disp, 0);
        for (f = -warm; f < frames; f++) {
            BfmPlatDrawEnv env;
            double t0, t1, t2, t3, c0 = cpu_ms();
            int buf = (f + warm) & 1;
            memset(&env, 0, sizeof env);
            env.clip.x = 0; env.clip.y = (int16_t)(buf * 240);
            env.clip.w = 320; env.clip.h = 240;
            env.offset_y = (int16_t)(buf * 240);
            env.clear_bg = 1;
            env.bg_b = 40;
            env.dither = 1;
            t0 = now_ms();
            if (feedback) {
                /* off-screen render-to-texture: draw 4 panels, sample each */
                BfmPlatDrawEnv off = env;
                BfmPlatPrim q[2];
                int k;
                off.clip.x = 512; off.clip.y = 256; off.clip.w = 128; off.clip.h = 128;
                off.offset_x = 512; off.offset_y = 256;
                off.clear_bg = 0;
                bfm_gl_batch_set_env(&b, &off);
                bfm_gl_batch_add(&b, scene, (size_t)(prims / 20));
                bfm_gl_batch_set_env(&b, &env);
                for (k = 0; k < 4; k++) {
                    memset(q, 0, sizeof q);
                    q[0].kind = BFM_PRIM_SPRITE;
                    q[0].tpage = tp(2, 0, 512, 256);
                    q[0].v[0].x = (int16_t)(20 + 70 * k); q[0].v[0].y = 150;
                    q[0].v[0].u = (uint8_t)(k * 16); q[0].v[0].v = (uint8_t)(k * 16);
                    q[0].v[0].r = q[0].v[0].g = q[0].v[0].b = 128;
                    q[0].w = 64; q[0].h = 64;
                    bfm_gl_batch_add(&b, q, 1);
                }
            } else {
                bfm_gl_batch_set_env(&b, &env);
            }
            bfm_gl_batch_add(&b, scene, (size_t)prims);
            if (f == 0) {
                size_t k, draws = 0;
                for (k = 0; k < b.nc; k++) draws += b.c[k].kind == BFM_GL_CMD_DRAW;
                cmds = (int)b.nc;
                ndraws = (int)draws;
            }
            t1 = now_ms();
            bfm_gl_exec_sync_vram(x, &vram);
            bfm_gl_exec_run(x, &b, 1);
            bfm_gl_exec_finish(x);
            t2 = now_ms();
            disp.y = (int16_t)(buf * 240);
            bfm_gl_batch_set_display(&b, &disp, 53);
            bfm_gl_exec_present(x, &b, &ov, win_w, win_h, 4.0 / 3.0 * 426 / 320, 1);
            bfm_gl_exec_finish(x);
            t3 = now_ms();
            bfm_gl_batch_clear(&b);
            if (f >= 0) {
                sb += t1 - t0;
                sg += t2 - t1;
                sp += t3 - t2;
                tot[f] = t3 - t0;
                scpu += cpu_ms() - c0;
            }
        }
        qsort(tot, (size_t)frames, sizeof *tot, cmp_d);
        printf("%-5d %10.2f %10.2f %10.2f %10.2f %10.2f %8.1f %9.1f %8.1f %7d %7d\n", scales[si],
               sb / frames, sg / frames, sp / frames, (sb + sg + sp) / frames, tot[frames / 2],
               1000.0 / ((sb + sg + sp) / frames), scpu / frames,
               sg / frames * 1e6 / (fill * scales[si] * scales[si]), cmds, ndraws);
        if (bfm_gl_exec_error(x)) printf("FAIL: GL error during the run\n");
        free(tot);
        bfm_gl_batch_free(&b);
        bfm_gl_batch_free(&ov);
        bfm_gl_exec_destroy(x);
    }
    free(scene);
    printf("ok bench\n");
    return 0;
}
