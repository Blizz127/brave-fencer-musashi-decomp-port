/* LD_PRELOAD capture shim for headless runs of musashi_native_boot (or any
 * SDL2/GL program): hooks SDL_GL_SwapWindow and, per presented frame,
 * counts non-black pixels of the framebuffer about to be shown.
 *
 *   BFM_CAPTURE_DIR          output directory (required; the shim is inert
 *                            without it)
 *   BFM_CAPTURE_EVERY        also write PNGs every N frames (default 600)
 *   BFM_CAPTURE_VRAM_OFFSET  hex offset of PsyCross's `vram` array (1024x512
 *                            16-bit) in the executable (from `nm`); when set,
 *                            the full VRAM is written next to each frame PNG
 *   BFM_CAPTURE_OT_HEADS     comma-separated guest addresses of GPU display
 *                            lists (DMA2 linked-list heads, e.g. 800ba0f4);
 *                            walked in guest RAM at host 0x80000000 (the
 *                            native-lane window) on each present in
 *   BFM_CAPTURE_OT_FRAMES    frame windows "36-50,336-368"; every packet is
 *                            logged to <dir>/ot.log with its GP0 words and a
 *                            decode (opcode, semi bit, texpage abr, colour,
 *                            rect). Read-only. HEADS=auto finds the lists
 *                            itself: full-screen tile packets (GP0 60-63,
 *                            w >= 256, h >= 200) found in guest RAM, followed
 *                            back through the tag words to the list head.
 *   BFM_CAPTURE_GPU          "GPU,STORE,PRIM,MODE" decimal offsets (native-lane
 *                            build): NativeBoot.gpu, and in the controller
 *                            backend.store_vram, prim_data, draw_mode.
 *                            Diagnostic: wraps the controller's store_vram
 *                            callback (forwarding every call unchanged) and
 *                            logs to <dir>/prims.log, in the frame windows,
 *                            each run of pixels with the GP0 words being
 *                            committed, the draw mode, bbox and pixel count.
 *                            Offsets come from offsetof() against the same
 *                            build's headers.
 *
 * Writes <dir>/frames.log ("frame seconds nonblack width height", one line
 * per frame, flushed), frame_NNNNNN.png at the first frame with non-black
 * pixels, whenever the display turns black/non-black, and every N frames;
 * vram_NNNNNN.png beside them. PNGs use the platform layer's writer
 * (bfm_plat_image.c). Output only goes to BFM_CAPTURE_DIR. It changes no
 * guest state: it reads the back buffer before the real swap. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bfm_plat_image.h"

typedef struct SDL_Window SDL_Window;
typedef void (*SwapFn)(SDL_Window *);
typedef void *(*GetProcFn)(const char *);
typedef void (*DrawableFn)(SDL_Window *, int *, int *);
typedef void (*ReadPixelsFn)(int, int, int, int, unsigned, unsigned, void *);
typedef void (*GetIntegervFn)(unsigned, int *);
typedef void (*BindFbFn)(unsigned, unsigned);
typedef void (*PixelStoreFn)(unsigned, int);

#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_PACK_ALIGNMENT 0x0D05

#define LOADSYM(dst, obj)                                                     \
    do {                                                                      \
        void *o_ = (obj);                                                     \
        memcpy(&(dst), &o_, sizeof o_);                                       \
    } while (0)

static SwapFn real_swap;
static GetProcFn get_proc;
static DrawableFn drawable;
static ReadPixelsFn read_pixels;
static GetIntegervFn get_integerv;
static BindFbFn bind_fb;
static PixelStoreFn pixel_store;
static const char *dir;
static FILE *log_file;
static unsigned long frame, every = 600;
static uintptr_t vram_addr;
static int last_lit = -1, ready;
static struct timespec t0;
static uint32_t ot_heads[8];
static int n_heads, ram_ok, auto_heads;
static unsigned long win_lo[8], win_hi[8];
static int n_wins;
static FILE *ot_log;
typedef int (*StoreFn)(void *, uint16_t, uint16_t, uint16_t);
static StoreFn real_store;
static uint8_t *gpu_base;
static size_t off_prim, off_mode;
static FILE *prim_log;
static uint32_t run_words[12], run_mode;
static unsigned long run_pixels;
static int run_x0, run_y0, run_x1, run_y1, run_open;
static uint16_t run_color;

static int first_object(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;   /* main program first */
    return 1;
}

static void init(void) {
    const char *e;
    ready = -1;
    if (!(dir = getenv("BFM_CAPTURE_DIR")) || !dir[0]) return;
    if ((e = getenv("BFM_CAPTURE_EVERY")) && atol(e) > 0) every = (unsigned long)atol(e);
    if ((e = getenv("BFM_CAPTURE_VRAM_OFFSET")) && e[0]) {
        uintptr_t base = 0;
        dl_iterate_phdr(first_object, &base);
        vram_addr = base + (uintptr_t)strtoull(e, NULL, 16);
    }
    LOADSYM(real_swap, dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
    LOADSYM(get_proc, dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress"));
    LOADSYM(drawable, dlsym(RTLD_NEXT, "SDL_GL_GetDrawableSize"));
    if (!real_swap || !get_proc || !drawable) return;
    {
        char path[1024];
        snprintf(path, sizeof path, "%s/frames.log", dir);
        log_file = fopen(path, "w");
    }
    if ((e = getenv("BFM_CAPTURE_OT_HEADS")) && !strcmp(e, "auto")) {
        auto_heads = 1;
        n_heads = 1;   /* enables the RAM check and ot.log */
    } else if ((e = getenv("BFM_CAPTURE_OT_HEADS")) && e[0]) {
        const char *q = e;
        while (*q && n_heads < 8) {
            char *end;
            unsigned long v = strtoul(q, &end, 16);
            if (end == q) break;
            ot_heads[n_heads++] = (uint32_t)v;
            q = *end == ',' ? end + 1 : end;
        }
    }
    if ((e = getenv("BFM_CAPTURE_OT_FRAMES")) && e[0]) {
        const char *q = e;
        while (*q && n_wins < 8) {
            char *end;
            unsigned long a = strtoul(q, &end, 10), b = a;
            if (end == q) break;
            if (*end == '-') b = strtoul(end + 1, &end, 10);
            win_lo[n_wins] = a; win_hi[n_wins++] = b;
            q = *end == ',' ? end + 1 : end;
        }
    }
    if (n_heads || getenv("BFM_CAPTURE_GPU")) {   /* guest RAM at 0x80000000 (lane build) */
        FILE *m = fopen("/proc/self/maps", "r");
        char line[512];
        while (m && fgets(line, sizeof line, m)) {
            unsigned long lo, hi;
            if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && lo <= 0x80000000ul &&
                hi >= 0x80200000ul)
                ram_ok = 1;
        }
        if (m) fclose(m);
        if (n_heads) {
            char path[1024];
            snprintf(path, sizeof path, "%s/ot.log", dir);
            ot_log = fopen(path, "w");
            if (ot_log && !ram_ok)
                fprintf(ot_log, "# guest RAM not mapped at 0x80000000 (not a native-lane build?)\n");
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ready = 1;
}

static int in_window(unsigned long f);
static int in_window_now(void) { return in_window(frame); }

static void run_flush(void) {
    if (!run_open || !prim_log) return;
    {
        int k;
        fprintf(prim_log, "frame %lu mode %08x abr=%u op=%02x semi=%d pixels %lu bbox %d,%d-%d,%d "
                          "first_color %04x words",
                frame, run_mode, (run_mode >> 5) & 3, run_words[0] >> 24,
                (int)((run_words[0] >> 25) & 1), run_pixels, run_x0, run_y0, run_x1, run_y1,
                run_color);
        for (k = 0; k < 12; k++) fprintf(prim_log, " %08x", run_words[k]);
        fputc('\n', prim_log);
    }
    run_open = 0;
}

static int store_wrap(void *userdata, uint16_t x, uint16_t y, uint16_t color) {
    if (prim_log && in_window_now()) {
        const uint32_t *w = (const uint32_t *)(gpu_base + off_prim);
        uint32_t mode = *(const uint32_t *)(gpu_base + off_mode);
        if (!run_open || memcmp(w, run_words, sizeof run_words) || mode != run_mode) {
            run_flush();
            memcpy(run_words, w, sizeof run_words);
            run_mode = mode;
            run_pixels = 0;
            run_x0 = run_x1 = x; run_y0 = run_y1 = y;
            run_color = color;
            run_open = 1;
        }
        run_pixels++;
        if (x < run_x0) run_x0 = x;
        if (x > run_x1) run_x1 = x;
        if (y < run_y0) run_y0 = y;
        if (y > run_y1) run_y1 = y;
    }
    return real_store(userdata, x, y, color);
}

static void install_gpu_hook(void) {
    const char *e = getenv("BFM_CAPTURE_GPU");
    unsigned long g, st, pr, md;
    char path[1024];
    StoreFn *slot;
    if (!e || sscanf(e, "%lu,%lu,%lu,%lu", &g, &st, &pr, &md) != 4 || !ram_ok) return;
    gpu_base = (uint8_t *)(uintptr_t)(0x80000000ul + g);
    slot = (StoreFn *)(void *)(gpu_base + st);
    if (!*slot || *slot == store_wrap) return;
    real_store = *slot;
    *slot = store_wrap;
    off_prim = pr;
    off_mode = md;
    snprintf(path, sizeof path, "%s/prims.log", dir);
    prim_log = fopen(path, "w");
}

static uint32_t guest32(uint32_t addr) {
    const uint8_t *p = (const uint8_t *)(uintptr_t)(0x80000000u | (addr & 0x1ffffcu));
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void decode(FILE *f, const uint32_t *w, unsigned n, unsigned *texpage) {
    uint32_t c = w[0];
    unsigned op = c >> 24;
    if (op >= 0x20 && op < 0x80) {
        const char *kind = op < 0x40 ? "POLY" : op < 0x60 ? "LINE" : "RECT";
        int textured = (op < 0x40 || op >= 0x60) && (op & 4);
        fprintf(f, "  %s op=%02x semi=%d %s%s abr=%u rgb=%06x", kind, op, (op >> 1) & 1,
                textured ? "textured " : "", textured && (op & 1) ? "raw " : "",
                (*texpage >> 5) & 3, c & 0xffffff);
        if (op >= 0x60 && n >= 2) {
            int x = (int16_t)(w[1] & 0xffff), y = (int16_t)(w[1] >> 16);
            unsigned size = (op >> 3) & 3, wh = textured ? 3 : 2;
            if (size == 0 && n > wh)
                fprintf(f, " xy=%d,%d wh=%u,%u", x, y, w[wh] & 0x3ff, (w[wh] >> 16) & 0x1ff);
            else
                fprintf(f, " xy=%d,%d wh=%u", x, y, size == 1 ? 1 : size == 2 ? 8 : 16);
        } else if (op < 0x40 && n >= 2) {
            fprintf(f, " v0=%d,%d", (int16_t)(w[1] & 0xffff), (int16_t)(w[1] >> 16));
        }
        fputc('\n', f);
    } else if (op == 0xe1) {
        *texpage = c & 0x3fff;
        fprintf(f, "  E1 texpage=%04x abr=%u depth=%u\n", c & 0x3fff, (c >> 5) & 3, (c >> 7) & 3);
    } else if (op == 0xe6) {
        fprintf(f, "  E6 mask set=%u check=%u\n", c & 1, (c >> 1) & 1);
    } else if (op == 0x02 && n >= 3) {
        fprintf(f, "  FILL rgb=%06x xy=%u,%u wh=%u,%u\n", c & 0xffffff, w[1] & 0xffff,
                w[1] >> 16, w[2] & 0xffff, w[2] >> 16);
    } else if (op >= 0xe2 && op <= 0xe5) {
        fprintf(f, "  E%x %06x\n", op & 0xf, c & 0xffffff);
    }
}

static int in_window(unsigned long f) {
    int i;
    for (i = 0; i < n_wins; i++)
        if (f >= win_lo[i] && f <= win_hi[i]) return 1;
    return n_wins == 0;
}

/* The node whose tag links to `node` (a word with low 24 bits == node and a
 * plausible length), or 0xffffffff. */
static uint32_t predecessor(uint32_t node) {
    uint32_t a;
    for (a = 0; a < 0x200000u; a += 4) {
        uint32_t w = guest32(a);
        if ((w & 0xffffff) == node && (w >> 24) <= 16 && a != node) return a;
    }
    return 0xffffffffu;
}

/* Heads of lists holding a full-screen tile. */
static int find_heads(uint32_t *out, int max) {
    uint32_t a;
    int n = 0, i;
    for (a = 4; a + 8 < 0x200000u && n < max; a += 4) {
        uint32_t c = guest32(a), op = c >> 24, tag, wh, head, steps = 0, p;
        if (op < 0x60 || op > 0x63) continue;
        tag = guest32(a - 4);
        if ((tag >> 24) < 3 || (tag >> 24) > 16) continue;
        wh = guest32(a + 8);
        if ((wh & 0x3ff) < 256 || ((wh >> 16) & 0x1ff) < 200) continue;
        head = a - 4;
        while (steps++ < 64 && (p = predecessor(head)) != 0xffffffffu) head = p;
        for (i = 0; i < n; i++)
            if (out[i] == head) break;
        if (i == n) {
            fprintf(ot_log, "frame %lu full-screen tile packet at %06x (head %06x, %u steps back)\n",
                    frame, a - 4, head, steps - 1);
            out[n++] = head;
        }
    }
    return n;
}

static void walk_ots(void) {
    int h, nh;
    uint32_t heads[8];
    if (!ot_log || !ram_ok || !in_window(frame)) return;
    if (auto_heads) {
        nh = find_heads(heads, 8);
        if (!nh) fprintf(ot_log, "frame %lu no full-screen tile in guest RAM\n", frame);
    } else {
        nh = n_heads;
        memcpy(heads, ot_heads, sizeof heads);
    }
    for (h = 0; h < nh; h++) {
        uint32_t addr = heads[h] & 0xffffff, nodes = 0;
        unsigned texpage = 0;
        fprintf(ot_log, "frame %lu head %08x\n", frame, heads[h] | 0x80000000u);
        while (addr != 0xffffff && nodes++ < 8192) {
            uint32_t tag = guest32(addr), len = tag >> 24, k, words[64];
            if ((addr & 0xffffff) >= 0x200000) { fprintf(ot_log, "  bad node %06x\n", addr); break; }
            if (len) {
                fprintf(ot_log, " node %06x len %u:", addr, len);
                for (k = 0; k < len && k < 64; k++) {
                    words[k] = guest32(addr + 4 + 4 * k);
                    fprintf(ot_log, " %08x", words[k]);
                }
                fputc('\n', ot_log);
                /* one packet may hold several commands; decode the first
                 * and any E1/E6 words the packet starts with */
                {
                    unsigned at = 0;
                    while (at < len && at < 64) {
                        unsigned op = words[at] >> 24, sz = 1;
                        decode(ot_log, words + at, len - at, &texpage);
                        if (op >= 0x20 && op < 0x80) break;
                        if (op == 0x02) sz = 3;
                        at += sz;
                    }
                }
            }
            addr = tag & 0xffffff;
        }
    }
    fflush(ot_log);
}

static void write_vram(unsigned long n) {
    static uint8_t rgb[1024 * 512 * 3];
    char path[1024];
    bfm_plat_vram15_to_rgb((const uint16_t *)vram_addr, 1024u * 512u, rgb);
    snprintf(path, sizeof path, "%s/vram_%06lu.png", dir, n);
    bfm_plat_png_write(path, 1024, 512, 3, rgb);
}

static void capture(SDL_Window *w) {
    int width = 0, height = 0, prev = 0, x, y;
    uint8_t *px, *flip;
    unsigned long lit = 0;
    struct timespec t;
    if (!read_pixels) {
        LOADSYM(read_pixels, get_proc("glReadPixels"));
        LOADSYM(get_integerv, get_proc("glGetIntegerv"));
        LOADSYM(bind_fb, get_proc("glBindFramebuffer"));
        LOADSYM(pixel_store, get_proc("glPixelStorei"));
        if (!read_pixels || !get_integerv || !bind_fb || !pixel_store) { ready = -1; return; }
    }
    drawable(w, &width, &height);
    if (width <= 0 || height <= 0) return;
    if (!(px = (uint8_t *)malloc((size_t)width * height * 4))) return;
    get_integerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
    bind_fb(GL_READ_FRAMEBUFFER, 0);
    pixel_store(GL_PACK_ALIGNMENT, 1);
    read_pixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, px);
    bind_fb(GL_READ_FRAMEBUFFER, (unsigned)prev);
    for (x = 0; x < width * height; x++)
        lit += (px[x * 4] | px[x * 4 + 1] | px[x * 4 + 2]) > 8;
    clock_gettime(CLOCK_MONOTONIC, &t);
    if (log_file) {
        fprintf(log_file, "%lu %.2f %lu %d %d\n", frame,
                (double)(t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9, lit, width, height);
        fflush(log_file);
    }
    if ((int)(lit > 0) != last_lit || frame % every == 0) {
        char path[1024];
        /* GL rows are bottom-up; PNG wants top-down, RGB */
        if ((flip = (uint8_t *)malloc((size_t)width * height * 3)) != NULL) {
            for (y = 0; y < height; y++)
                for (x = 0; x < width; x++) {
                    const uint8_t *s = &px[((size_t)(height - 1 - y) * width + x) * 4];
                    uint8_t *d = &flip[((size_t)y * width + x) * 3];
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                }
            snprintf(path, sizeof path, "%s/frame_%06lu.png", dir, frame);
            bfm_plat_png_write(path, (uint32_t)width, (uint32_t)height, 3, flip);
            free(flip);
        }
        if (vram_addr) write_vram(frame);
        last_lit = lit > 0;
    }
    free(px);
}

void SDL_GL_SwapWindow(SDL_Window *w) {
    if (!ready) init();
    if (ready > 0 && !prim_log) install_gpu_hook();
    if (prim_log) { run_flush(); fflush(prim_log); }
    if (ready > 0) walk_ots();
    if (ready > 0) capture(w);
    frame++;
    if (real_swap) real_swap(w);
    else {
        SwapFn f;
        LOADSYM(f, dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
        if (f) f(w);
    }
}
