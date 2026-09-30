/* "psycross" renderer backend: bfm_plat renderer -> PsyCross libgpu (MIT,
 * tools/third_party/psycross, fetched at a pinned commit, not committed).
 *
 * Only PsyCross's public Psy-Q-shaped API is used (DrawPrim, LoadImage,
 * StoreImage, PutDrawEnv, PutDispEnv, ClearImage, PsyX scene calls); no Sony
 * code is involved. The PsyCross window/GL context is owned by the runtime
 * (port/native-lane); set BFM_PLAT_PSYCROSS_OWNS_WINDOW to let this backend
 * create it instead.
 *
 * Build: C11 (PsyCross headers use static_assert), -include assert.h,
 * -I tools/third_party/psycross/include, -DBFM_PLAT_WITH_PSYCROSS.
 * Status: compile-checked against the PsyCross headers; first run waits for
 * the SDL2/OpenAL/GL dev libs. */
#include "../bfm_plat.h"
#include "../bfm_plat_font.h"

#include <psx/types.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>
#include <PsyX/PsyX_public.h>

#include <stdlib.h>
#include <string.h>

typedef struct PsyCrossRenderer {
    int in_scene;
    uint16_t tpage;
} PsyCrossRenderer;

static PsyCrossRenderer state;

static int pc_init(void *self, const BfmPlatOutput *o) {
    PsyCrossRenderer *s = (PsyCrossRenderer *)self;
    memset(s, 0, sizeof *s);
#ifdef BFM_PLAT_PSYCROSS_OWNS_WINDOW
    {
        /* Wider render target: the window takes the configured aspect at
         * the configured height. PsyCross (PGXP) maps the 4:3 display into
         * it unstretched, which is hor+ widescreen. */
        BfmPlatWidescreen ws;
        int w = o->window_width;
        if (bfm_plat_widescreen_compute(o->aspect_num, o->aspect_den,
                                        BFM_WIDESCREEN_HOR_PLUS, 320, 240, &ws) == BFM_PLAT_OK)
            bfm_plat_widescreen_window(&ws, o->window_height, &w);
        PsyX_Initialise((char *)"Brave Fencer Musashi", w, o->window_height,
                        o->fullscreen);
    }
#else
    (void)o;
#endif
    ResetGraph(0);
    return BFM_PLAT_OK;
}

static void pc_shutdown(void *self) {
    (void)self;
#ifdef BFM_PLAT_PSYCROSS_OWNS_WINDOW
    PsyX_Shutdown();
#endif
}

static int pc_output(void *self, const BfmPlatOutput *o) {
    (void)self;
    /* PsyCross fixes its window at PsyX_Initialise and always maps the 4:3
     * display unstretched (hor+). Anamorphic output needs a stretching
     * renderer, so it is refused here and the caller keeps hor+. */
    if (o->aspect_num * 3 != o->aspect_den * 4 &&
        bfm_plat_config()->widescreen_mode == BFM_WIDESCREEN_ANAMORPHIC)
        return BFM_PLAT_UNSUPPORTED;
    return BFM_PLAT_OK;
}

static int pc_begin(void *self) {
    PsyCrossRenderer *s = (PsyCrossRenderer *)self;
    if (!s->in_scene) s->in_scene = PsyX_BeginScene() != 0;
    return BFM_PLAT_OK;
}

static void to_rect(RECT16 *d, const BfmPlatRect *r) {
    d->x = r->x; d->y = r->y; d->w = r->w; d->h = r->h;
}

static int pc_draw_env(void *self, const BfmPlatDrawEnv *e) {
    DRAWENV env;
    (void)self;
    memset(&env, 0, sizeof env);
    to_rect(&env.clip, &e->clip);
    env.ofs[0] = e->offset_x;
    env.ofs[1] = e->offset_y;
    to_rect(&env.tw, &e->texture_window);
    env.tpage = e->tpage;
    env.dtd = e->dither;
    env.dfe = e->draw_on_display;
    env.isbg = e->clear_bg;
    env.r0 = e->bg_r; env.g0 = e->bg_g; env.b0 = e->bg_b;
    PutDrawEnv(&env);
    ((PsyCrossRenderer *)self)->tpage = e->tpage;
    return BFM_PLAT_OK;
}

static int pc_disp_env(void *self, const BfmPlatDispEnv *e) {
    DISPENV env;
    (void)self;
    memset(&env, 0, sizeof env);
    to_rect(&env.disp, &e->disp);
    to_rect(&env.screen, &e->screen);
    env.isinter = e->interlaced;
    env.isrgb24 = e->rgb24;
    PutDispEnv(&env);
    return BFM_PLAT_OK;
}

#define FLAGS(p, f) do { \
    if ((f) & BFM_PRIM_FLAG_SEMI_TRANS) setSemiTrans(p, 1); \
    if ((f) & BFM_PRIM_FLAG_RAW_TEXTURE) setShadeTex(p, 1); } while (0)
#define V(i) q->v[i]

static void draw_one(const BfmPlatPrim *q) {
    /* Untextured semi-transparent prims blend with the texpage in effect
     * (its abr): hand PsyCross the prim's own, as a textured one carries it. */
    if ((q->flags & BFM_PRIM_FLAG_SEMI_TRANS) &&
        (q->kind == BFM_PRIM_POLY_F3 || q->kind == BFM_PRIM_POLY_F4 ||
         q->kind == BFM_PRIM_POLY_G3 || q->kind == BFM_PRIM_POLY_G4 ||
         q->kind == BFM_PRIM_LINE_F || q->kind == BFM_PRIM_LINE_G || q->kind == BFM_PRIM_TILE)) {
        DR_TPAGE t;
        setDrawTPage(&t, 1, 0, q->tpage);
        termPrim(&t);
        DrawPrim(&t);
    }
    switch (q->kind) {
    case BFM_PRIM_POLY_F3: { POLY_F3 p; setPolyF3(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b);
        setXY3(&p, V(0).x, V(0).y, V(1).x, V(1).y, V(2).x, V(2).y);
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_POLY_F4: { POLY_F4 p; setPolyF4(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b);
        setXY4(&p, V(0).x, V(0).y, V(1).x, V(1).y, V(2).x, V(2).y, V(3).x, V(3).y);
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_POLY_G3: { POLY_G3 p; setPolyG3(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b); setRGB1(&p, V(1).r, V(1).g, V(1).b);
        setRGB2(&p, V(2).r, V(2).g, V(2).b);
        setXY3(&p, V(0).x, V(0).y, V(1).x, V(1).y, V(2).x, V(2).y);
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_POLY_G4: { POLY_G4 p; setPolyG4(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b); setRGB1(&p, V(1).r, V(1).g, V(1).b);
        setRGB2(&p, V(2).r, V(2).g, V(2).b); setRGB3(&p, V(3).r, V(3).g, V(3).b);
        setXY4(&p, V(0).x, V(0).y, V(1).x, V(1).y, V(2).x, V(2).y, V(3).x, V(3).y);
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_POLY_FT3: { POLY_FT3 p; setPolyFT3(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b);
        setXY3(&p, V(0).x, V(0).y, V(1).x, V(1).y, V(2).x, V(2).y);
        setUV3(&p, V(0).u, V(0).v, V(1).u, V(1).v, V(2).u, V(2).v);
        p.tpage = q->tpage; p.clut = q->clut;
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_POLY_FT4: { POLY_FT4 p; setPolyFT4(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b);
        setXY4(&p, V(0).x, V(0).y, V(1).x, V(1).y, V(2).x, V(2).y, V(3).x, V(3).y);
        setUV4(&p, V(0).u, V(0).v, V(1).u, V(1).v, V(2).u, V(2).v, V(3).u, V(3).v);
        p.tpage = q->tpage; p.clut = q->clut;
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_POLY_GT3: { POLY_GT3 p; setPolyGT3(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b); setRGB1(&p, V(1).r, V(1).g, V(1).b);
        setRGB2(&p, V(2).r, V(2).g, V(2).b);
        setXY3(&p, V(0).x, V(0).y, V(1).x, V(1).y, V(2).x, V(2).y);
        setUV3(&p, V(0).u, V(0).v, V(1).u, V(1).v, V(2).u, V(2).v);
        p.tpage = q->tpage; p.clut = q->clut;
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_POLY_GT4: { POLY_GT4 p; setPolyGT4(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b); setRGB1(&p, V(1).r, V(1).g, V(1).b);
        setRGB2(&p, V(2).r, V(2).g, V(2).b); setRGB3(&p, V(3).r, V(3).g, V(3).b);
        setXY4(&p, V(0).x, V(0).y, V(1).x, V(1).y, V(2).x, V(2).y, V(3).x, V(3).y);
        setUV4(&p, V(0).u, V(0).v, V(1).u, V(1).v, V(2).u, V(2).v, V(3).u, V(3).v);
        p.tpage = q->tpage; p.clut = q->clut;
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_LINE_F: { LINE_F2 p; setLineF2(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b);
        setXY2(&p, V(0).x, V(0).y, V(1).x, V(1).y);
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_LINE_G: { LINE_G2 p; setLineG2(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b); setRGB1(&p, V(1).r, V(1).g, V(1).b);
        setXY2(&p, V(0).x, V(0).y, V(1).x, V(1).y);
        FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_TILE: { TILE p; setTile(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b); setXY0(&p, V(0).x, V(0).y);
        setWH(&p, q->w, q->h); FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_SPRITE: { DR_TPAGE t; SPRT p;
        setDrawTPage(&t, 1, 0, q->tpage); termPrim(&t); DrawPrim(&t);
        setSprt(&p); termPrim(&p);
        setRGB0(&p, V(0).r, V(0).g, V(0).b); setXY0(&p, V(0).x, V(0).y);
        setUV0(&p, V(0).u, V(0).v); p.clut = q->clut;
        setWH(&p, q->w, q->h); FLAGS(&p, q->flags); DrawPrim(&p); break; }
    case BFM_PRIM_FILL: { RECT16 r;
        r.x = V(0).x; r.y = V(0).y; r.w = (short)q->w; r.h = (short)q->h;
        ClearImage(&r, V(0).r, V(0).g, V(0).b); break; }
    default: break;
    }
}

static int pc_submit(void *self, const BfmPlatPrim *p, size_t n) {
    size_t i;
    (void)self;
    for (i = 0; i < n; i++) draw_one(&p[i]);
    return BFM_PLAT_OK;
}

static int pc_upload(void *self, const BfmPlatRect *r, const uint16_t *px) {
    RECT16 rr;
    (void)self;
    to_rect(&rr, r);
    LoadImage(&rr, (u_long *)(uintptr_t)px);
    DrawSync(0);
    return BFM_PLAT_OK;
}

static int pc_download(void *self, const BfmPlatRect *r, uint16_t *px) {
    RECT16 rr;
    (void)self;
    to_rect(&rr, r);
    DrawSync(0);   /* queued draws first, so the read sees them */
    StoreImage(&rr, (u_long *)px);
    DrawSync(0);
    return BFM_PLAT_OK;
}

static int pc_present(void *self) {
    PsyCrossRenderer *s = (PsyCrossRenderer *)self;
    /* DrawPrim only queues: DrawSync draws the queued splits (and reads the
     * framebuffer back into PsyCross's VRAM) before the scene is shown */
    DrawSync(0);
    if (s->in_scene) {
        PsyX_EndScene();
        s->in_scene = 0;
    }
    return BFM_PLAT_OK;
}

/* Overlay text (console, cheat menu, "PAUSED"): the built-in 5x7 font as
 * TILE runs over a semi-transparent backing tile, drawn straight into the
 * current draw buffer before present. Coordinates are relative to the
 * current draw environment's clip origin, so the text lands on the frame
 * being shown whichever buffer the game draws into. Uses no VRAM texture
 * (FntLoad would overwrite game VRAM). */
static int pc_overlay_text(void *self, int x, int y, const char *text) {
    BfmPlatPrim stack[512];
    BfmPlatPrim *prims = stack;
    size_t need = bfm_plat_font_text_prims(x, y, text, 1, 255, 255, 255, 1, NULL, 0);
    size_t i;
    (void)self;
    if (need > sizeof stack / sizeof stack[0]) {
        prims = (BfmPlatPrim *)malloc(need * sizeof *prims);
        if (!prims) return BFM_PLAT_ERROR;
    }
    bfm_plat_font_text_prims(x, y, text, 1, 255, 255, 255, 1, prims, need);
    for (i = 0; i < need; i++) draw_one(&prims[i]);
    if (prims != stack) free(prims);
    return BFM_PLAT_OK;
}

static const BfmPlatRendererBackend psycross_backend = {
    "psycross", pc_init, pc_shutdown, pc_output, pc_begin, pc_draw_env,
    pc_disp_env, pc_submit, pc_upload, NULL, pc_download, pc_present,
    pc_overlay_text, &state
};

int bfm_plat_backend_psycross_register(void);
int bfm_plat_backend_psycross_register(void) {
    return bfm_plat_renderer_register(&psycross_backend);
}
