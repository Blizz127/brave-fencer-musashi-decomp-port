/* `gl` renderer backend: OpenGL 3.3 core through SDL2, for the HD/restyle
 * path. The PsyCross backend stays the PS1 reference.
 *
 * Built with BFM_PLAT_WITH_GL (see docs/ARCHITECTURE-PORT.md, "gl
 * renderer"). GL entry points come from SDL_GL_GetProcAddress; no loader
 * library. Needs a display; headless runs fall back to "null" (renderer
 * open does that when init fails). The CPU side lives in gl/bfm_gl_core.c,
 * the GL side in gl/bfm_gl_exec.c, both tested without a window.
 *
 * Features: 1024x512 VRAM texture + CPU mirror (Load/Store/MoveImage),
 * batched flat/gouraud/textured prims with 4/8/15-bit CLUT decode in the
 * shader, the four semi-transparency modes (STP-split for textured prims),
 * the E6 mask bit (alpha = bit 15; stencil for check-mask), render
 * feedback (textures sampling drawn VRAM: GPU snapshots for 15-bit pages,
 * readback for CLUT pages), PS1 dithering (video.dither), internal
 * resolution 1x-8x
 * (video.internal_scale), hor+ widescreen from the projection/widescreen
 * state, HD texture replacement (upload_replacement), overlay text. */

#include "../bfm_plat_renderer.h"
#include "../bfm_plat.h"
#include "../bfm_plat_config.h"
#include "../bfm_plat_font.h"
#include "../bfm_plat_widescreen.h"
#include "gl/bfm_gl_core.h"
#include "gl/bfm_gl_exec.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLUSH_VERTS (1u << 20)

typedef struct GlRenderer {
    SDL_Window *win;
    SDL_GLContext ctx;
    BfmGlExec *x;
    BfmGlVram vram;
    BfmGlBatch batch, overlay;
    BfmGlReplacements repl;
    BfmPlatOutput out;
    BfmPlatRect disp;
    int rgb24;                   /* 24-bit display (movies) */
    uint8_t *rgb24_buf;
    double image_aspect;
    char err[512];
} GlRenderer;

static GlRenderer state;

static void *get_proc(const char *name) { return SDL_GL_GetProcAddress(name); }

static int dither_on(const GlRenderer *s) {
    const BfmPlatConfig *c = bfm_plat_config();
    int mode = c ? c->dither : BFM_DITHER_AUTO;
    if (mode == BFM_DITHER_AUTO) return bfm_gl_exec_scale(s->x) == 1;
    return mode == BFM_DITHER_ON;
}

/* hor+ margin and displayed image aspect for the current display. */
static void layout(GlRenderer *s) {
    const BfmPlatConfig *c = bfm_plat_config();
    BfmPlatWidescreen ws;
    int mode = c ? c->widescreen_mode : BFM_WIDESCREEN_HOR_PLUS;
    int margin = 0;
    double a = 4.0 / 3.0;
    if (bfm_plat_widescreen_compute(s->out.aspect_num, s->out.aspect_den, mode,
                                    s->disp.w, s->disp.h, &ws) == BFM_PLAT_OK &&
        ws.active) {
        if (mode == BFM_WIDESCREEN_ANAMORPHIC) {
            a = (double)s->out.aspect_num / s->out.aspect_den;
        } else {
            margin = -ws.visible_x0;
            a = 4.0 / 3.0 * (double)(s->disp.w + 2 * margin) / s->disp.w;
        }
    }
    s->image_aspect = a;
    bfm_gl_batch_set_display(&s->batch, &s->disp, margin);
    {
        BfmPlatRect o = s->disp;
        o.x = o.y = 0;
        bfm_gl_batch_set_display(&s->overlay, &o, 0);
    }
}

static int flush(GlRenderer *s) {
    int r;
    if (!s->x) return BFM_PLAT_NOT_READY;
    bfm_gl_exec_sync_vram(s->x, &s->vram);
    r = bfm_gl_exec_run(s->x, &s->batch, dither_on(s));
    bfm_gl_batch_clear(&s->batch);
    return r;
}

static int make_window(GlRenderer *s, const BfmPlatOutput *o) {
    BfmPlatWidescreen ws;
    int w = o->window_width > 0 ? o->window_width : 960;
    int h = o->window_height > 0 ? o->window_height : 720;
    if (bfm_plat_widescreen_compute(o->aspect_num, o->aspect_den,
                                    BFM_WIDESCREEN_HOR_PLUS, 320, 240, &ws) == BFM_PLAT_OK)
        bfm_plat_widescreen_window(&ws, h, &w);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    s->win = SDL_CreateWindow("Brave Fencer Musashi", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, w, h,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                  (o->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!s->win) return BFM_PLAT_ERROR;
    if (!(s->ctx = SDL_GL_CreateContext(s->win))) return BFM_PLAT_ERROR;
    SDL_GL_SetSwapInterval(o->vsync ? 1 : 0);
    return BFM_PLAT_OK;
}

static void gl_shutdown(void *self);

static int gl_init(void *self, const BfmPlatOutput *o) {
    GlRenderer *s = (GlRenderer *)self;
    memset(s, 0, sizeof *s);
    if (!o) return BFM_PLAT_INVALID;
    s->out = *o;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[gl] SDL video init failed: %s\n", SDL_GetError());
        return BFM_PLAT_UNSUPPORTED;
    }
    if (make_window(s, o) != BFM_PLAT_OK) {
        fprintf(stderr, "[gl] no GL 3.3 core context: %s\n", SDL_GetError());
        gl_shutdown(s);
        return BFM_PLAT_UNSUPPORTED;
    }
    s->x = bfm_gl_exec_create(get_proc, o->internal_scale, s->err, sizeof s->err);
    if (!s->x) {
        fprintf(stderr, "[gl] executor: %s\n", s->err);
        gl_shutdown(s);
        return BFM_PLAT_UNSUPPORTED;
    }
    bfm_gl_vram_init(&s->vram);
    bfm_gl_batch_init(&s->batch);
    bfm_gl_batch_init(&s->overlay);
    s->batch.vram = &s->vram;
    s->batch.repl = &s->repl;
    s->overlay.overlay = 1;
    s->disp.w = 320;
    s->disp.h = 240;
    layout(s);
    return BFM_PLAT_OK;
}

static void gl_shutdown(void *self) {
    GlRenderer *s = (GlRenderer *)self;
    int i;
    if (s->x) {
        for (i = 0; i < BFM_GL_MAX_REPLACEMENTS; i++)
            if (s->repl.r[i].live) bfm_gl_exec_texture_free(s->x, s->repl.r[i].id);
        bfm_gl_exec_destroy(s->x);
    }
    bfm_gl_batch_free(&s->batch);
    bfm_gl_batch_free(&s->overlay);
    free(s->rgb24_buf);
    if (s->ctx) SDL_GL_DeleteContext(s->ctx);
    if (s->win) SDL_DestroyWindow(s->win);
    if (s->win || s->ctx || s->x) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    memset(s, 0, sizeof *s);
}

static int gl_output(void *self, const BfmPlatOutput *o) {
    GlRenderer *s = (GlRenderer *)self;
    if (!s->x || !o) return BFM_PLAT_NOT_READY;
    flush(s);
    if (o->internal_scale != bfm_gl_exec_scale(s->x)) {
        BfmPlatRect all = {0, 0, BFM_GL_VRAM_W, BFM_GL_VRAM_H};
        if (bfm_gl_exec_set_scale(s->x, o->internal_scale) != BFM_PLAT_OK)
            return BFM_PLAT_ERROR;
        /* targets were reallocated: rendered pixels are gone, repaint what
         * the mirror holds */
        memset(s->batch.owner, 0, sizeof s->batch.owner);
        bfm_gl_batch_add_copy(&s->batch, &all);
    }
    if (o->fullscreen != s->out.fullscreen)
        SDL_SetWindowFullscreen(s->win, o->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (o->vsync != s->out.vsync) SDL_GL_SetSwapInterval(o->vsync ? 1 : 0);
    s->out = *o;
    layout(s);
    return BFM_PLAT_OK;
}

static int gl_draw_env(void *self, const BfmPlatDrawEnv *e) {
    GlRenderer *s = (GlRenderer *)self;
    bfm_gl_batch_set_env(&s->batch, e);
    return BFM_PLAT_OK;
}

static int gl_disp_env(void *self, const BfmPlatDispEnv *e) {
    GlRenderer *s = (GlRenderer *)self;
    if (!bfm_plat_rect_valid(&e->disp)) return BFM_PLAT_INVALID;
    s->disp = e->disp;
    s->rgb24 = e->rgb24;
    layout(s);
    return BFM_PLAT_OK;
}

static int gl_submit(void *self, const BfmPlatPrim *p, size_t n) {
    GlRenderer *s = (GlRenderer *)self;
    bfm_gl_batch_add(&s->batch, p, n);
    if (s->batch.oom) {
        s->batch.oom = 0;
        return BFM_PLAT_ERROR;
    }
    if (s->batch.nv >= FLUSH_VERTS) return flush(s);
    return BFM_PLAT_OK;
}

static void kill_replacements(GlRenderer *s, const BfmPlatRect *r) {
    uint32_t dead[BFM_GL_MAX_REPLACEMENTS];
    int i, n = bfm_gl_repl_invalidate(&s->repl, r, dead, BFM_GL_MAX_REPLACEMENTS);
    for (i = 0; i < n && i < BFM_GL_MAX_REPLACEMENTS; i++)
        bfm_gl_exec_texture_free(s->x, dead[i]);
}

static int gl_upload(void *self, const BfmPlatRect *r, const uint16_t *px) {
    GlRenderer *s = (GlRenderer *)self;
    int rc;
    flush(s);    /* queued prims still sample the old texels */
    if ((rc = bfm_gl_vram_upload(&s->vram, r, px)) != BFM_PLAT_OK) return rc;
    kill_replacements(s, r);
    bfm_gl_batch_add_copy(&s->batch, r);
    return BFM_PLAT_OK;
}

static int gl_upload_replacement(void *self, const BfmPlatRect *r,
                                 const uint16_t *orig, const BfmPlatImage *img) {
    GlRenderer *s = (GlRenderer *)self;
    uint32_t id, old = 0;
    int rc = gl_upload(self, r, orig);
    if (rc != BFM_PLAT_OK || !img || !img->rgba) return rc;
    if (!(id = bfm_gl_exec_texture(s->x, img->width, img->height, img->rgba)))
        return BFM_PLAT_OK;   /* VRAM stays consistent; shown unreplaced */
    if (bfm_gl_repl_add(&s->repl, r, img->width, img->height, id, &old) < 0) {
        bfm_gl_exec_texture_free(s->x, id);
        return BFM_PLAT_OK;
    }
    if (old && old != id) bfm_gl_exec_texture_free(s->x, old);
    return BFM_PLAT_OK;
}

static int gl_download(void *self, const BfmPlatRect *r, uint16_t *px) {
    GlRenderer *s = (GlRenderer *)self;
    if (!s->x) return BFM_PLAT_NOT_READY;
    /* rendered tiles in r are read back into the mirror first, so
     * StoreImage/MoveImage of a frame buffer or render target see the
     * drawn pixels, mask bits included */
    bfm_gl_batch_add_resolve(&s->batch, r);
    flush(s);
    return bfm_gl_vram_download(&s->vram, r, px);
}

static int gl_present(void *self) {
    GlRenderer *s = (GlRenderer *)self;
    int w = 0, h = 0, rc;
    if (!s->x) return BFM_PLAT_NOT_READY;
    flush(s);
    SDL_GL_GetDrawableSize(s->win, &w, &h);
    if (s->rgb24) {
        /* movie frames are uploaded packed; show them from the mirror, 4:3 */
        size_t need = (size_t)s->disp.w * s->disp.h * 4u;
        uint8_t *buf = (uint8_t *)realloc(s->rgb24_buf, need);
        if (!buf) return BFM_PLAT_ERROR;
        s->rgb24_buf = buf;
        bfm_gl_decode_rgb24(&s->vram, &s->disp, buf);
        rc = bfm_gl_exec_present_image(s->x, s->disp.w, s->disp.h, buf, &s->overlay,
                                       w, h, 4.0 / 3.0);
    } else {
        rc = bfm_gl_exec_present(s->x, &s->batch, &s->overlay, w, h, s->image_aspect,
                                 dither_on(s));
    }
    bfm_gl_batch_clear(&s->overlay);
    SDL_GL_SwapWindow(s->win);
    return rc;
}

/* Overlay text in display pixels over the 4:3 area, drawn after the frame
 * is composed, so it never touches game VRAM. */
static int gl_overlay_text(void *self, int x, int y, const char *text) {
    GlRenderer *s = (GlRenderer *)self;
    BfmPlatPrim stack[512];
    BfmPlatPrim *prims = stack;
    size_t need = bfm_plat_font_text_prims(x, y, text, 1, 255, 255, 255, 1, NULL, 0);
    if (need > sizeof stack / sizeof stack[0]) {
        prims = (BfmPlatPrim *)malloc(need * sizeof *prims);
        if (!prims) return BFM_PLAT_ERROR;
    }
    bfm_plat_font_text_prims(x, y, text, 1, 255, 255, 255, 1, prims, need);
    bfm_gl_batch_add(&s->overlay, prims, need);
    if (prims != stack) free(prims);
    return BFM_PLAT_OK;
}

static const BfmPlatRendererBackend gl_backend = {
    "gl", gl_init, gl_shutdown, gl_output, NULL, gl_draw_env, gl_disp_env,
    gl_submit, gl_upload, gl_upload_replacement, gl_download, gl_present,
    gl_overlay_text, &state
};

int bfm_plat_backend_gl_register(void);
int bfm_plat_backend_gl_register(void) {
    return bfm_plat_renderer_register(&gl_backend);
}
