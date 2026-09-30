/* CPU side of the `gl` renderer backend. See bfm_gl_core.h. */

#include "bfm_gl_core.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- VRAM mirror ---------------------------------------------------- */

void bfm_gl_vram_init(BfmGlVram *v) {
    memset(v, 0, sizeof *v);
}

void bfm_gl_vram_mark(BfmGlVram *v, int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (w <= 0 || h <= 0) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > BFM_GL_VRAM_W) x1 = BFM_GL_VRAM_W;
    if (y1 > BFM_GL_VRAM_H) y1 = BFM_GL_VRAM_H;
    if (x >= x1 || y >= y1) return;
    if (!v->dirty) {
        v->dirty = 1;
        v->dx0 = x; v->dy0 = y; v->dx1 = x1; v->dy1 = y1;
        return;
    }
    if (x < v->dx0) v->dx0 = x;
    if (y < v->dy0) v->dy0 = y;
    if (x1 > v->dx1) v->dx1 = x1;
    if (y1 > v->dy1) v->dy1 = y1;
}

int bfm_gl_vram_upload(BfmGlVram *v, const BfmPlatRect *r, const uint16_t *px) {
    int row;
    if (!v || !px || !bfm_plat_rect_valid(r)) return BFM_PLAT_INVALID;
    for (row = 0; row < r->h; row++)
        memcpy(&v->px[(size_t)(r->y + row) * BFM_GL_VRAM_W + r->x],
               px + (size_t)row * r->w, (size_t)r->w * 2u);
    bfm_gl_vram_mark(v, r->x, r->y, r->w, r->h);
    return BFM_PLAT_OK;
}

int bfm_gl_vram_download(const BfmGlVram *v, const BfmPlatRect *r, uint16_t *px) {
    int row;
    if (!v || !px || !bfm_plat_rect_valid(r)) return BFM_PLAT_INVALID;
    for (row = 0; row < r->h; row++)
        memcpy(px + (size_t)row * r->w,
               &v->px[(size_t)(r->y + row) * BFM_GL_VRAM_W + r->x],
               (size_t)r->w * 2u);
    return BFM_PLAT_OK;
}

int bfm_gl_vram_move(BfmGlVram *v, const BfmPlatRect *src, int x, int y) {
    uint16_t *tmp;
    int i, j, wraps;
    if (!v || !bfm_plat_rect_valid(src)) return BFM_PLAT_INVALID;
    tmp = (uint16_t *)malloc((size_t)src->w * (size_t)src->h * 2u);
    if (!tmp) return BFM_PLAT_ERROR;
    bfm_gl_vram_download(v, src, tmp);
    x &= BFM_GL_VRAM_W - 1;
    y &= BFM_GL_VRAM_H - 1;
    for (j = 0; j < src->h; j++)
        for (i = 0; i < src->w; i++)
            v->px[(size_t)((y + j) & (BFM_GL_VRAM_H - 1)) * BFM_GL_VRAM_W +
                  ((x + i) & (BFM_GL_VRAM_W - 1))] = tmp[(size_t)j * src->w + i];
    free(tmp);
    wraps = x + src->w > BFM_GL_VRAM_W || y + src->h > BFM_GL_VRAM_H;
    if (wraps) bfm_gl_vram_mark(v, 0, 0, BFM_GL_VRAM_W, BFM_GL_VRAM_H);
    else bfm_gl_vram_mark(v, x, y, src->w, src->h);
    return BFM_PLAT_OK;
}

int bfm_gl_vram_fill(BfmGlVram *v, const BfmPlatRect *r, uint16_t color) {
    int i, j, x0, y0, x1, y1;
    if (!v || !r || r->w <= 0 || r->h <= 0) return BFM_PLAT_INVALID;
    x0 = r->x < 0 ? 0 : r->x;
    y0 = r->y < 0 ? 0 : r->y;
    x1 = r->x + r->w > BFM_GL_VRAM_W ? BFM_GL_VRAM_W : r->x + r->w;
    y1 = r->y + r->h > BFM_GL_VRAM_H ? BFM_GL_VRAM_H : r->y + r->h;
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++) v->px[(size_t)j * BFM_GL_VRAM_W + i] = color;
    bfm_gl_vram_mark(v, x0, y0, x1 - x0, y1 - y0);
    return BFM_PLAT_OK;
}

void bfm_gl_decode_rgb24(const BfmGlVram *v, const BfmPlatRect *d, uint8_t *rgba) {
    int x, y, c;
    for (y = 0; y < d->h; y++) {
        const uint16_t *row = &v->px[(size_t)((d->y + y) & (BFM_GL_VRAM_H - 1)) * BFM_GL_VRAM_W];
        for (x = 0; x < d->w; x++) {
            uint8_t *o = &rgba[((size_t)y * d->w + x) * 4u];
            for (c = 0; c < 3; c++) {
                int byte = x * 3 + c;
                uint16_t h = row[(d->x + byte / 2) & (BFM_GL_VRAM_W - 1)];
                o[c] = (uint8_t)(byte & 1 ? h >> 8 : h & 0xFF);
            }
            o[3] = 255;
        }
    }
}

/* ---- texture pages ---------------------------------------------------- */

BfmGlTpage bfm_gl_decode_tpage(uint16_t tp) {
    BfmGlTpage t;
    int d = (tp >> 7) & 3;
    t.base_x = (tp & 0xF) * 64;
    t.base_y = ((tp >> 4) & 1) * 256;
    t.abr = (tp >> 5) & 3;
    t.depth = d == 0 ? 4 : d == 1 ? 8 : 15;
    return t;
}

void bfm_gl_decode_clut(uint16_t clut, int *x, int *y) {
    *x = (clut & 0x3F) * 16;
    *y = (clut >> 6) & 0x1FF;
}

int bfm_gl_window(int u, int off, int size) {
    if (size <= 0) return u;
    return (u & (size - 1)) | (off & ~(size - 1) & 0xFF);
}

static uint16_t at(const BfmGlVram *v, int x, int y) {
    return v->px[(size_t)(y & (BFM_GL_VRAM_H - 1)) * BFM_GL_VRAM_W +
                 (x & (BFM_GL_VRAM_W - 1))];
}

uint16_t bfm_gl_sample(const BfmGlVram *v, uint16_t tpage, uint16_t clut,
                       int u, int vv, const BfmPlatRect *win) {
    BfmGlTpage t = bfm_gl_decode_tpage(tpage);
    int cx, cy, idx;
    u &= 0xFF;
    vv &= 0xFF;
    if (win) {
        u = bfm_gl_window(u, win->x, win->w);
        vv = bfm_gl_window(vv, win->y, win->h);
    }
    bfm_gl_decode_clut(clut, &cx, &cy);
    switch (t.depth) {
    case 4:
        idx = (at(v, t.base_x + u / 4, t.base_y + vv) >> ((u & 3) * 4)) & 0xF;
        return at(v, cx + idx, cy);
    case 8:
        idx = (at(v, t.base_x + u / 2, t.base_y + vv) >> ((u & 1) * 8)) & 0xFF;
        return at(v, cx + idx, cy);
    default:
        return at(v, t.base_x + u, t.base_y + vv);
    }
}

static int round_i(double x) { return (int)floor(x + 0.5); }

void bfm_gl_modulate(uint16_t texel, int r, int g, int b, int raw,
                     uint8_t out[3]) {
    int c[3], i;
    c[0] = r; c[1] = g; c[2] = b;
    for (i = 0; i < 3; i++) {
        double t = (double)((texel >> (5 * i)) & 31) / 31.0;
        double o = raw ? t : t * (double)c[i] / 128.0;
        if (o > 1.0) o = 1.0;
        out[i] = (uint8_t)round_i(o * 255.0);
    }
}

/* ---- semi-transparency ---------------------------------------------- */

uint16_t bfm_gl_blend_ref(int mode, uint16_t back, uint16_t front) {
    uint16_t out = 0;
    int i;
    for (i = 0; i < 3; i++) {
        int b = (back >> (5 * i)) & 31, f = (front >> (5 * i)) & 31, o;
        switch (mode & 3) {
        case 0: o = (b + f) >> 1; break;
        case 1: o = b + f; break;
        case 2: o = b - f; break;
        default: o = b + (f >> 2); break;
        }
        if (o < 0) o = 0;
        if (o > 31) o = 31;
        out |= (uint16_t)(o << (5 * i));
    }
    return out;
}

BfmGlBlendState bfm_gl_blend_state(int mode) {
    BfmGlBlendState s;
    s.equation = BFM_GL_FUNC_ADD;
    s.src = s.dst = BFM_GL_ONE;
    s.constant = 1.0f;
    switch (mode & 3) {
    case 0:  /* 0.5 * F + 0.5 * B */
        s.src = s.dst = BFM_GL_CONSTANT_COLOR;
        s.constant = 0.5f;
        break;
    case 1:  /* F + B */
        break;
    case 2:  /* B - F */
        s.equation = BFM_GL_FUNC_REVERSE_SUBTRACT;
        break;
    default: /* 0.25 * F + B */
        s.src = BFM_GL_CONSTANT_COLOR;
        s.constant = 0.25f;
        break;
    }
    return s;
}

uint16_t bfm_gl_blend_emulate(const BfmGlBlendState *s, uint16_t back,
                              uint16_t front) {
    uint16_t out = 0;
    int i;
    for (i = 0; i < 3; i++) {
        /* dst: what an RGBA8 target holds for a 5-bit value; src: shader
         * output (float) */
        double b = (double)round_i(((back >> (5 * i)) & 31) * 255.0 / 31.0) / 255.0;
        double f = (double)((front >> (5 * i)) & 31) / 31.0;
        double sf = s->src == BFM_GL_CONSTANT_COLOR ? s->constant : 1.0;
        double df = s->dst == BFM_GL_CONSTANT_COLOR ? s->constant : 1.0;
        double o = s->equation == BFM_GL_FUNC_REVERSE_SUBTRACT
                       ? b * df - f * sf : f * sf + b * df;
        int q;
        if (o < 0.0) o = 0.0;
        if (o > 1.0) o = 1.0;
        q = round_i(o * 255.0) >> 3;   /* stored RGB8, read back as 5-bit */
        out |= (uint16_t)(q << (5 * i));
    }
    return out;
}

/* ---- dither ------------------------------------------------------------ */

const int8_t bfm_gl_dither_matrix[4][4] = {
    {-4, +0, -3, +1},
    {+2, -2, +3, -1},
    {-3, +1, -4, +0},
    {+3, -1, +2, -2},
};

int bfm_gl_dither5(int c8, int x, int y) {
    int c = c8 + bfm_gl_dither_matrix[y & 3][x & 3];
    if (c < 0) c = 0;
    if (c > 255) c = 255;
    return c >> 3;
}

/* ---- HD replacements ----------------------------------------------------- */

static int rect_eq(const BfmPlatRect *a, const BfmPlatRect *b) {
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

static int rect_overlap(const BfmPlatRect *a, const BfmPlatRect *b) {
    return a->x < b->x + b->w && b->x < a->x + a->w &&
           a->y < b->y + b->h && b->y < a->y + a->h;
}

int bfm_gl_repl_add(BfmGlReplacements *set, const BfmPlatRect *rect,
                    uint32_t width, uint32_t height, uint32_t id,
                    uint32_t *old_id) {
    int i, slot = -1;
    if (old_id) *old_id = 0;
    if (!set || !bfm_plat_rect_valid(rect) || !width || !height) return -1;
    for (i = 0; i < BFM_GL_MAX_REPLACEMENTS; i++)
        if (set->r[i].live && rect_eq(&set->r[i].rect, rect)) { slot = i; break; }
    if (slot < 0)
        for (i = 0; i < BFM_GL_MAX_REPLACEMENTS; i++)
            if (!set->r[i].live) { slot = i; break; }
    if (slot < 0) return -1;
    if (old_id && set->r[slot].live) *old_id = set->r[slot].id;
    set->r[slot].rect = *rect;
    set->r[slot].width = width;
    set->r[slot].height = height;
    set->r[slot].id = id;
    set->r[slot].live = 1;
    return slot;
}

int bfm_gl_repl_invalidate(BfmGlReplacements *set, const BfmPlatRect *rect,
                           uint32_t *dead, int max) {
    int i, n = 0;
    if (!set || !rect) return 0;
    for (i = 0; i < BFM_GL_MAX_REPLACEMENTS; i++) {
        if (!set->r[i].live || !rect_overlap(&set->r[i].rect, rect)) continue;
        set->r[i].live = 0;
        if (dead && n < max) dead[n] = set->r[i].id;
        n++;
    }
    return n;
}

static int texel_factor(uint16_t tpage) {
    int d = bfm_gl_decode_tpage(tpage).depth;
    return d == 4 ? 4 : d == 8 ? 2 : 1;
}

static int prim_textured(int kind) {
    return kind == BFM_PRIM_POLY_FT3 || kind == BFM_PRIM_POLY_FT4 ||
           kind == BFM_PRIM_POLY_GT3 || kind == BFM_PRIM_POLY_GT4 ||
           kind == BFM_PRIM_SPRITE;
}

static int prim_verts(int kind) {
    switch (kind) {
    case BFM_PRIM_POLY_F3: case BFM_PRIM_POLY_G3:
    case BFM_PRIM_POLY_FT3: case BFM_PRIM_POLY_GT3: return 3;
    case BFM_PRIM_POLY_F4: case BFM_PRIM_POLY_G4:
    case BFM_PRIM_POLY_FT4: case BFM_PRIM_POLY_GT4: return 4;
    case BFM_PRIM_LINE_F: case BFM_PRIM_LINE_G: return 2;
    default: return 1;
    }
}

int bfm_gl_repl_find(const BfmGlReplacements *set, const BfmPlatPrim *p) {
    BfmGlTpage t;
    int umin, umax, vmin, vmax, f, x0, x1, y0, y1, i;
    if (!set || !p || !prim_textured(p->kind)) return -1;
    if (p->kind == BFM_PRIM_SPRITE) {
        umin = p->v[0].u; umax = umin + p->w;
        vmin = p->v[0].v; vmax = vmin + p->h;
    } else {
        int n = prim_verts(p->kind);
        umin = umax = p->v[0].u;
        vmin = vmax = p->v[0].v;
        for (i = 1; i < n; i++) {
            if (p->v[i].u < umin) umin = p->v[i].u;
            if (p->v[i].u > umax) umax = p->v[i].u;
            if (p->v[i].v < vmin) vmin = p->v[i].v;
            if (p->v[i].v > vmax) vmax = p->v[i].v;
        }
        if (umax == umin) umax++;  /* one texel column */
        if (vmax == vmin) vmax++;
    }
    if (umax > 256 || vmax > 256) return -1;   /* wraps inside the page */
    t = bfm_gl_decode_tpage(p->tpage);
    f = texel_factor(p->tpage);
    x0 = t.base_x + umin / f;
    x1 = t.base_x + (umax + f - 1) / f;
    y0 = t.base_y + vmin;
    y1 = t.base_y + vmax;
    for (i = 0; i < BFM_GL_MAX_REPLACEMENTS; i++) {
        const BfmPlatRect *r = &set->r[i].rect;
        if (!set->r[i].live) continue;
        if (x0 >= r->x && x1 <= r->x + r->w && y0 >= r->y && y1 <= r->y + r->h)
            return i;
    }
    return -1;
}

void bfm_gl_repl_coords(const BfmGlReplacement *r, uint16_t tpage, float u,
                        float v, float *s, float *t) {
    BfmGlTpage tp = bfm_gl_decode_tpage(tpage);
    float f = (float)texel_factor(tpage);
    *s = ((float)tp.base_x * f + u - (float)r->rect.x * f) / ((float)r->rect.w * f);
    *t = ((float)tp.base_y + v - (float)r->rect.y) / (float)r->rect.h;
}

/* ---- batching --------------------------------------------------------- */

static void tiles_clean(BfmGlBatch *b, const BfmPlatRect *r);

void bfm_gl_batch_init(BfmGlBatch *b) {
    memset(b, 0, sizeof *b);
    b->disp.w = 320;
    b->disp.h = 240;
    b->env.clip.w = BFM_GL_VRAM_W;
    b->env.clip.h = BFM_GL_VRAM_H;
    b->scissor = b->env.clip;
    bfm_gl_batch_clear(b);
}

void bfm_gl_batch_free(BfmGlBatch *b) {
    free(b->v);
    free(b->c);
    b->v = NULL;
    b->c = NULL;
    b->nv = b->cap_v = b->nc = b->cap_c = 0;
}

void bfm_gl_batch_clear(BfmGlBatch *b) {
    b->nv = 0;
    b->nc = 0;
    b->nsnap = 0;
    b->last_snap = -1;
}

/* Tiles whose newest pixels lived in a scene target that is being reused. */
static void tiles_forget(BfmGlBatch *b, int target) {
    int tx, ty;
    for (ty = 0; ty < BFM_GL_TILES_Y; ty++)
        for (tx = 0; tx < BFM_GL_TILES_X; tx++)
            if (b->owner[ty][tx] == target + 1) b->owner[ty][tx] = 0;
}

void bfm_gl_batch_target_size(const BfmGlBatch *b, int target, int *w, int *h) {
    if (target == BFM_GL_TARGET_SCENE0 || target == BFM_GL_TARGET_SCENE1) {
        *w = b->disp.w + 2 * b->margin;
        *h = b->disp.h;
    } else if (target == BFM_GL_TARGET_WINDOW) {
        *w = b->disp.w;
        *h = b->disp.h;
    } else {
        *w = BFM_GL_VRAM_W;
        *h = BFM_GL_VRAM_H;
    }
}

int bfm_gl_batch_scene_at(const BfmGlBatch *b, int x, int y) {
    int i;
    for (i = 0; i < BFM_GL_SCENE_SLOTS; i++)
        if (b->scene[i].used && b->scene[i].x == x && b->scene[i].y == y) return i;
    return -1;
}

static int scene_claim(BfmGlBatch *b, int x, int y) {
    int i, pick = -1;
    i = bfm_gl_batch_scene_at(b, x, y);
    if (i >= 0) {
        b->scene[i].stamp = ++b->tick;
        return i;
    }
    for (i = 0; i < BFM_GL_SCENE_SLOTS; i++)
        if (!b->scene[i].used) { pick = i; break; }
    if (pick < 0) {
        pick = 0;
        for (i = 1; i < BFM_GL_SCENE_SLOTS; i++)
            if (b->scene[i].stamp < b->scene[pick].stamp) pick = i;
    }
    if (b->scene[pick].used) tiles_forget(b, BFM_GL_TARGET_SCENE0 + pick);
    b->scene[pick].used = 1;
    b->scene[pick].x = x;
    b->scene[pick].y = y;
    b->scene[pick].stamp = ++b->tick;
    return pick;
}

void bfm_gl_batch_set_display(BfmGlBatch *b, const BfmPlatRect *disp, int margin) {
    if (!disp || disp->w <= 0 || disp->h <= 0) return;
    if (margin < 0) margin = 0;
    if (disp->w != b->disp.w || disp->h != b->disp.h || margin != b->margin) {
        memset(b->scene, 0, sizeof b->scene);
        tiles_forget(b, BFM_GL_TARGET_SCENE0);
        tiles_forget(b, BFM_GL_TARGET_SCENE1);
    }
    b->disp = *disp;
    b->margin = margin;
    if (!b->overlay) scene_claim(b, disp->x, disp->y);
    bfm_gl_batch_set_env(b, &b->env);   /* re-route the current env */
}

/* The scene slot whose frame buffer contains rect, claiming one when the
 * rect has the display's size; -1 = plain VRAM. */
static int scene_for(BfmGlBatch *b, const BfmPlatRect *r) {
    int i;
    for (i = 0; i < BFM_GL_SCENE_SLOTS; i++) {
        const BfmGlScene *s = &b->scene[i];
        if (s->used && r->x >= s->x && r->y >= s->y &&
            r->x + r->w <= s->x + b->disp.w && r->y + r->h <= s->y + b->disp.h)
            return i;
    }
    if (r->w == b->disp.w && r->h == b->disp.h) return scene_claim(b, r->x, r->y);
    return -1;
}

/* rect (VRAM) -> target-local rect in scene slot i, widened by the margin
 * where it touches the frame buffer's left/right edge. */
static BfmPlatRect scene_local(const BfmGlBatch *b, int i, const BfmPlatRect *r) {
    const BfmGlScene *s = &b->scene[i];
    BfmPlatRect o;
    int x0 = r->x, x1 = r->x + r->w, y0 = r->y, y1 = r->y + r->h;
    if (x0 < s->x) x0 = s->x;
    if (y0 < s->y) y0 = s->y;
    if (x1 > s->x + b->disp.w) x1 = s->x + b->disp.w;
    if (y1 > s->y + b->disp.h) y1 = s->y + b->disp.h;
    x0 = x0 - s->x + b->margin;
    x1 = x1 - s->x + b->margin;
    if (x0 == b->margin) x0 = 0;
    if (x1 == b->margin + b->disp.w) x1 += b->margin;
    o.x = (int16_t)x0;
    o.y = (int16_t)(y0 - s->y);
    o.w = (int16_t)(x1 - x0);
    o.h = (int16_t)(y1 - y0);
    return o;
}

static BfmGlCmd *push_cmd(BfmGlBatch *b) {
    if (b->nc == b->cap_c) {
        size_t cap = b->cap_c ? b->cap_c * 2 : 256;
        BfmGlCmd *c = (BfmGlCmd *)realloc(b->c, cap * sizeof *c);
        if (!c) { b->oom = 1; return NULL; }
        b->c = c;
        b->cap_c = cap;
    }
    memset(&b->c[b->nc], 0, sizeof b->c[0]);
    return &b->c[b->nc++];
}

static void emit_fill(BfmGlBatch *b, int target, const BfmPlatRect *r, uint32_t rgb) {
    BfmGlCmd *c;
    if (r->w <= 0 || r->h <= 0) return;
    if (!(c = push_cmd(b))) return;
    c->kind = BFM_GL_CMD_FILL;
    c->target = (uint8_t)target;
    c->blend = -1;
    c->rect = *r;
    c->rgb = rgb;
}

static uint16_t rgb_to_15(uint32_t rgb) {
    return (uint16_t)(((rgb >> 3) & 31) | (((rgb >> 11) & 31) << 5) |
                      (((rgb >> 19) & 31) << 10));
}

/* A VRAM-space fill: mirror, VRAM target, and the scene slots it touches. */
static void fill_route(BfmGlBatch *b, const BfmPlatRect *r, uint32_t rgb) {
    int i;
    if (b->overlay) {
        emit_fill(b, BFM_GL_TARGET_WINDOW, r, rgb);
        return;
    }
    if (b->vram) bfm_gl_vram_fill(b->vram, r, rgb_to_15(rgb));
    emit_fill(b, BFM_GL_TARGET_VRAM, r, rgb);
    tiles_clean(b, r);
    for (i = 0; i < BFM_GL_SCENE_SLOTS; i++) {
        BfmPlatRect area, l;
        if (!b->scene[i].used) continue;
        area.x = (int16_t)b->scene[i].x; area.y = (int16_t)b->scene[i].y;
        area.w = b->disp.w; area.h = b->disp.h;
        if (!rect_overlap(&area, r)) continue;
        l = scene_local(b, i, r);
        emit_fill(b, BFM_GL_TARGET_SCENE0 + i, &l, rgb);
    }
}

void bfm_gl_batch_set_env(BfmGlBatch *b, const BfmPlatDrawEnv *env) {
    int s;
    if (!env) return;
    if (env != &b->env) b->env = *env;
    if (b->overlay) {
        b->target = BFM_GL_TARGET_WINDOW;
        b->org_x = b->org_y = 0;
        b->scissor.x = b->scissor.y = 0;
        b->scissor.w = b->disp.w;
        b->scissor.h = b->disp.h;
    } else if ((s = scene_for(b, &b->env.clip)) >= 0) {
        b->target = BFM_GL_TARGET_SCENE0 + s;
        b->org_x = b->scene[s].x - b->margin;
        b->org_y = b->scene[s].y;
        b->scissor = scene_local(b, s, &b->env.clip);
    } else {
        b->target = BFM_GL_TARGET_VRAM;
        b->org_x = b->org_y = 0;
        b->scissor = b->env.clip;
    }
    if (env != &b->env && env->clear_bg)
        fill_route(b, &b->env.clip, (uint32_t)env->bg_r | ((uint32_t)env->bg_g << 8) |
                                        ((uint32_t)env->bg_b << 16));
}

static int rect_same(const BfmPlatRect *a, const BfmPlatRect *b) {
    return rect_eq(a, b);
}

/* ---- tile map ---------------------------------------------------------- */

int bfm_gl_batch_owner(const BfmGlBatch *b, int x, int y) {
    if (x < 0 || y < 0 || x >= BFM_GL_VRAM_W || y >= BFM_GL_VRAM_H) return 0;
    return b->owner[y / BFM_GL_TILE][x / BFM_GL_TILE];
}

/* Tiles touched by [x0,x1) x [y0,y1) now hold target's pixels. */
static void tiles_rendered(BfmGlBatch *b, int x0, int y0, int x1, int y1, int target) {
    int tx, ty;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > BFM_GL_VRAM_W) x1 = BFM_GL_VRAM_W;
    if (y1 > BFM_GL_VRAM_H) y1 = BFM_GL_VRAM_H;
    if (x0 >= x1 || y0 >= y1) return;
    for (ty = y0 / BFM_GL_TILE; ty <= (y1 - 1) / BFM_GL_TILE; ty++)
        for (tx = x0 / BFM_GL_TILE; tx <= (x1 - 1) / BFM_GL_TILE; tx++)
            b->owner[ty][tx] = (uint8_t)(target + 1);
}

/* A write to VRAM rect [x0,x1) x [y0,y1) makes the last snapshot stale if
 * it overlaps what the snapshot copied. */
static void snap_touch(BfmGlBatch *b, int x0, int y0, int x1, int y1) {
    const BfmPlatRect *v;
    if (b->last_snap < 0) return;
    v = &b->c[b->last_snap].vrect;
    if (x0 < v->x + v->w && v->x < x1 && y0 < v->y + v->h && v->y < y1) b->last_snap = -1;
}

/* Tiles fully inside rect were written everywhere (mirror and targets). */
static void tiles_clean(BfmGlBatch *b, const BfmPlatRect *r) {
    int tx, ty;
    snap_touch(b, r->x, r->y, r->x + r->w, r->y + r->h);   /* fills, uploads */
    int tx0 = (r->x + BFM_GL_TILE - 1) / BFM_GL_TILE, tx1 = (r->x + r->w) / BFM_GL_TILE;
    int ty0 = (r->y + BFM_GL_TILE - 1) / BFM_GL_TILE, ty1 = (r->y + r->h) / BFM_GL_TILE;
    if (r->x < 0 || r->y < 0) return;
    if (tx1 > BFM_GL_TILES_X) tx1 = BFM_GL_TILES_X;
    if (ty1 > BFM_GL_TILES_Y) ty1 = BFM_GL_TILES_Y;
    for (ty = ty0; ty < ty1; ty++)
        for (tx = tx0; tx < tx1; tx++) b->owner[ty][tx] = 0;
}

/* VRAM rect -> target-local rect (clipped to the slot area for scenes). */
static int to_local(const BfmGlBatch *b, int target, const BfmPlatRect *v, BfmPlatRect *l) {
    *l = *v;
    if (target == BFM_GL_TARGET_SCENE0 || target == BFM_GL_TARGET_SCENE1) {
        const BfmGlScene *s = &b->scene[target - BFM_GL_TARGET_SCENE0];
        int x0 = v->x, y0 = v->y, x1 = v->x + v->w, y1 = v->y + v->h;
        if (!s->used) return 0;
        if (x0 < s->x) x0 = s->x;
        if (y0 < s->y) y0 = s->y;
        if (x1 > s->x + b->disp.w) x1 = s->x + b->disp.w;
        if (y1 > s->y + b->disp.h) y1 = s->y + b->disp.h;
        if (x0 >= x1 || y0 >= y1) return 0;
        l->x = (int16_t)(x0 - s->x + b->margin);
        l->y = (int16_t)(y0 - s->y);
        l->w = (int16_t)(x1 - x0);
        l->h = (int16_t)(y1 - y0);
    }
    return 1;
}

/* RESOLVE every rendered tile in [x0,x1) x [y0,y1), in per-owner runs. */
static void resolve_area(BfmGlBatch *b, int x0, int y0, int x1, int y1) {
    int tx, ty;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > BFM_GL_VRAM_W) x1 = BFM_GL_VRAM_W;
    if (y1 > BFM_GL_VRAM_H) y1 = BFM_GL_VRAM_H;
    if (x0 >= x1 || y0 >= y1) return;
    for (ty = y0 / BFM_GL_TILE; ty <= (y1 - 1) / BFM_GL_TILE; ty++) {
        tx = x0 / BFM_GL_TILE;
        while (tx <= (x1 - 1) / BFM_GL_TILE) {
            int own = b->owner[ty][tx], end = tx;
            BfmPlatRect v, l;
            BfmGlCmd *c;
            if (!own) { tx++; continue; }
            while (end + 1 <= (x1 - 1) / BFM_GL_TILE && b->owner[ty][end + 1] == own) end++;
            v.x = (int16_t)(tx * BFM_GL_TILE);
            v.y = (int16_t)(ty * BFM_GL_TILE);
            v.w = (int16_t)((end - tx + 1) * BFM_GL_TILE);
            v.h = BFM_GL_TILE;
            if (to_local(b, own - 1, &v, &l) && (c = push_cmd(b)) != NULL) {
                if (l.w != v.w || l.h != v.h) {   /* clipped to the slot */
                    v.x = (int16_t)(l.x - b->margin + b->scene[own - 1 - BFM_GL_TARGET_SCENE0].x);
                    v.y = (int16_t)(l.y + b->scene[own - 1 - BFM_GL_TARGET_SCENE0].y);
                    v.w = l.w; v.h = l.h;
                }
                c->kind = BFM_GL_CMD_RESOLVE;
                c->target = (uint8_t)(own - 1);
                c->blend = -1;
                c->rect = l;
                c->vrect = v;
                b->feedback_resolves++;
            }
            for (; tx <= end; tx++) b->owner[ty][tx] = 0;
        }
    }
}

void bfm_gl_batch_add_resolve(BfmGlBatch *b, const BfmPlatRect *r) {
    if (!b || !r || b->overlay) return;
    resolve_area(b, r->x, r->y, r->x + r->w, r->y + r->h);
}

/* Page texels a textured prim spans: [umin,umax) x [vmin,vmax); 0 when it
 * wraps inside the page. */
static int uv_span(const BfmPlatPrim *p, int *umin, int *umax, int *vmin, int *vmax) {
    int i;
    if (p->kind == BFM_PRIM_SPRITE) {
        *umin = p->v[0].u; *umax = *umin + p->w;
        *vmin = p->v[0].v; *vmax = *vmin + p->h;
    } else {
        int n = prim_verts(p->kind);
        *umin = *umax = p->v[0].u;
        *vmin = *vmax = p->v[0].v;
        for (i = 1; i < n; i++) {
            if (p->v[i].u < *umin) *umin = p->v[i].u;
            if (p->v[i].u > *umax) *umax = p->v[i].u;
            if (p->v[i].v < *vmin) *vmin = p->v[i].v;
            if (p->v[i].v > *vmax) *vmax = p->v[i].v;
        }
        if (*umax == *umin) (*umax)++;
        if (*vmax == *vmin) (*vmax)++;
    }
    return *umax <= 256 && *vmax <= 256;
}

/* Render feedback for a textured prim about to be drawn. Returns the
 * snapshot index + 1 the prim samples (its VRAM rect in *sv), or 0 when it
 * samples the VRAM texture (after RESOLVEs if rendered tiles were in its
 * footprint or CLUT). */
static int feedback(BfmGlBatch *b, const BfmPlatPrim *p, BfmPlatRect *sv) {
    BfmGlTpage t = bfm_gl_decode_tpage(p->tpage);
    int umin, umax, vmin, vmax, f = texel_factor(p->tpage);
    int nowrap = uv_span(p, &umin, &umax, &vmin, &vmax);
    int x0, x1, y0, y1, cx0 = 0, cy = 0, cw = 0, tx, ty, owner = 0, mixed = 0, clean = 0;
    if (nowrap && !b->env.texture_window.w && !b->env.texture_window.h) {
        x0 = t.base_x + umin / f;
        x1 = t.base_x + (umax + f - 1) / f;
        y0 = t.base_y + vmin;
        y1 = t.base_y + vmax;
    } else {   /* the whole page */
        x0 = t.base_x;
        x1 = t.base_x + 256 / f;
        y0 = t.base_y;
        y1 = t.base_y + 256;
        nowrap = 0;
    }
    if (x1 > BFM_GL_VRAM_W) x1 = BFM_GL_VRAM_W;
    if (y1 > BFM_GL_VRAM_H) y1 = BFM_GL_VRAM_H;
    for (ty = y0 / BFM_GL_TILE; ty <= (y1 - 1) / BFM_GL_TILE; ty++)
        for (tx = x0 / BFM_GL_TILE; tx <= (x1 - 1) / BFM_GL_TILE; tx++) {
            int o = b->owner[ty][tx];
            if (!o) { clean = 1; continue; }
            if (owner && o != owner) mixed = 1;
            owner = o;
        }
    if (t.depth != 15) {
        int any = 0;
        bfm_gl_decode_clut(p->clut, &cx0, &cy);
        cw = t.depth == 4 ? 16 : 256;
        for (tx = cx0 / BFM_GL_TILE; tx <= (cx0 + cw - 1) / BFM_GL_TILE && tx < BFM_GL_TILES_X; tx++)
            any |= b->owner[cy / BFM_GL_TILE][tx] != 0;
        if (any) resolve_area(b, cx0, cy, cx0 + cw, cy + 1);
    }
    if (!owner) return 0;
    if (t.depth == 15 && nowrap && !mixed &&
        (owner - 1 == BFM_GL_TARGET_VRAM || !clean) && b->nsnap < BFM_GL_MAX_SNAPSHOTS) {
        BfmPlatRect v, l;
        BfmGlCmd *c;
        int tgt = owner - 1;
        v.x = (int16_t)(x0 & ~(BFM_GL_TILE - 1));
        v.y = (int16_t)(y0 & ~(BFM_GL_TILE - 1));
        v.w = (int16_t)(((x1 + BFM_GL_TILE - 1) & ~(BFM_GL_TILE - 1)) - v.x);
        v.h = (int16_t)(((y1 + BFM_GL_TILE - 1) & ~(BFM_GL_TILE - 1)) - v.y);
        if (b->last_snap >= 0) {
            const BfmGlCmd *ls = &b->c[b->last_snap];
            if (ls->target == tgt &&
                x0 >= ls->vrect.x && x1 <= ls->vrect.x + ls->vrect.w &&
                y0 >= ls->vrect.y && y1 <= ls->vrect.y + ls->vrect.h) {
                *sv = ls->vrect;
                return ls->snap;
            }
        }
        if (to_local(b, tgt, &v, &l) && l.w == v.w && l.h == v.h &&
            (c = push_cmd(b)) != NULL) {
            c->kind = BFM_GL_CMD_SNAPSHOT;
            c->target = (uint8_t)tgt;
            c->blend = -1;
            c->rect = l;
            c->vrect = v;
            c->snap = (uint16_t)(++b->nsnap);
            b->last_snap = (long)(b->nc - 1);
            b->feedback_snaps++;
            *sv = v;
            return c->snap;
        }
    }
    resolve_area(b, x0, y0, x1, y1);
    return 0;
}

typedef struct DrawKey {
    int target, blend, split, repl, dither, copy, mask_set, mask_check, snap;
} DrawKey;

/* Current DRAW command for this state, reusing the last one if it matches. */
static BfmGlCmd *draw_cmd(BfmGlBatch *b, const DrawKey *k, const BfmPlatRect *sc) {
    static const BfmPlatRect none = {0, 0, 0, 0};
    const BfmPlatRect *win = k->target == BFM_GL_TARGET_WINDOW ? &none
                                                               : &b->env.texture_window;
    BfmGlCmd *c;
    if (b->nc) {
        c = &b->c[b->nc - 1];
        if (c->kind == BFM_GL_CMD_DRAW && c->target == k->target && c->blend == k->blend &&
            c->split_stp == k->split && c->repl == k->repl && c->copy == k->copy &&
            (!k->split || c->mask_set == k->mask_set) && c->mask_check == k->mask_check &&
            c->snap == k->snap && rect_same(&c->rect, sc) && rect_same(&c->win, win) &&
            c->first + c->count == b->nv) {
            c->dither |= (uint8_t)k->dither;   /* vertex flags decide per prim */
            c->mask_set |= (uint8_t)k->mask_set;
            c->mask_clear |= (uint8_t)!k->mask_set;
            return c;
        }
    }
    if (!(c = push_cmd(b))) return NULL;
    c->kind = BFM_GL_CMD_DRAW;
    c->target = (uint8_t)k->target;
    c->blend = (int8_t)k->blend;
    c->split_stp = (uint8_t)k->split;
    c->repl = (uint16_t)k->repl;
    c->dither = (uint8_t)k->dither;
    c->copy = (uint8_t)k->copy;
    c->mask_set = (uint8_t)k->mask_set;
    c->mask_clear = (uint8_t)!k->mask_set;
    c->mask_check = (uint8_t)k->mask_check;
    c->snap = (uint16_t)k->snap;
    c->rect = *sc;
    c->win = *win;
    c->first = (uint32_t)b->nv;
    return c;
}

static BfmGlVertex *push_verts(BfmGlBatch *b, size_t n) {
    if (b->nv + n > b->cap_v) {
        size_t cap = b->cap_v ? b->cap_v : 1024;
        BfmGlVertex *v;
        while (cap < b->nv + n) cap *= 2;
        v = (BfmGlVertex *)realloc(b->v, cap * sizeof *v);
        if (!v) { b->oom = 1; return NULL; }
        b->v = v;
        b->cap_v = cap;
    }
    b->nv += n;
    return &b->v[b->nv - n];
}

typedef struct Corner {
    float x, y, u, v;
    uint8_t r, g, bl;
} Corner;

/* How a prim's vertices sample: replacement slot, or snapshot rect. */
typedef struct Src {
    int repl;                    /* replacement slot or -1 */
    int snap;                    /* snapshot index + 1 or 0 */
    BfmPlatRect sv;              /* snapshot VRAM rect */
} Src;

static void put_tri(BfmGlBatch *b, BfmGlCmd *c, const Corner *k0, const Corner *k1,
                    const Corner *k2, const BfmPlatPrim *p, uint16_t flags, const Src *src) {
    const Corner *k[3];
    BfmGlVertex *v = push_verts(b, 3);
    int i;
    if (!v) return;
    k[0] = k0; k[1] = k1; k[2] = k2;
    for (i = 0; i < 3; i++) {
        memset(&v[i], 0, sizeof v[i]);
        v[i].x = k[i]->x;
        v[i].y = k[i]->y;
        v[i].r = k[i]->r; v[i].g = k[i]->g; v[i].b = k[i]->bl; v[i].a = 255;
        v[i].u = (uint16_t)k[i]->u;
        v[i].v = (uint16_t)k[i]->v;
        v[i].clut = p ? p->clut : 0;
        v[i].tpage = p ? p->tpage : 0;
        v[i].flags = flags;
        if (src && src->repl >= 0 && b->repl && p) {
            v[i].repl = (uint16_t)(src->repl + 1);
            bfm_gl_repl_coords(&b->repl->r[src->repl], p->tpage, k[i]->u, k[i]->v,
                               &v[i].s, &v[i].t);
        } else if (src && src->snap && p) {
            BfmGlTpage t = bfm_gl_decode_tpage(p->tpage);
            v[i].s = ((float)t.base_x + k[i]->u - (float)src->sv.x) / (float)src->sv.w;
            v[i].t = ((float)t.base_y + k[i]->v - (float)src->sv.y) / (float)src->sv.h;
        }
    }
    c->count += 3;
}

static void put_quad(BfmGlBatch *b, BfmGlCmd *c, const Corner *k, const BfmPlatPrim *p,
                     uint16_t flags, const Src *src) {
    /* k in PS1 order: 0 1 / 2 3 */
    put_tri(b, c, &k[0], &k[1], &k[2], p, flags, src);
    put_tri(b, c, &k[1], &k[3], &k[2], p, flags, src);
}

static int is_gouraud(int kind) {
    return kind == BFM_PRIM_POLY_G3 || kind == BFM_PRIM_POLY_G4 ||
           kind == BFM_PRIM_POLY_GT3 || kind == BFM_PRIM_POLY_GT4 ||
           kind == BFM_PRIM_LINE_G;
}

static void corner(Corner *k, float x, float y, const BfmPlatVertex *src,
                   float u, float v) {
    k->x = x; k->y = y; k->u = u; k->v = v;
    k->r = src->r; k->g = src->g; k->bl = src->b;
}

/* The prim's corners in target-local pixels; returns 3 (triangle) or 4
 * (quad in PS1 order), 0 for nothing to draw. */
static int prim_corners(const BfmGlBatch *b, const BfmPlatPrim *p, Corner *k) {
    float ox = (float)(b->env.offset_x - b->org_x);
    float oy = (float)(b->env.offset_y - b->org_y);
    int kind = p->kind, j, nv;
    switch (kind) {
    case BFM_PRIM_TILE:
    case BFM_PRIM_SPRITE: {
        float x = p->v[0].x + ox, y = p->v[0].y + oy;
        float w = p->w, h = p->h, u = p->v[0].u, v = p->v[0].v;
        corner(&k[0], x, y, &p->v[0], u, v);
        corner(&k[1], x + w, y, &p->v[0], u + w, v);
        corner(&k[2], x, y + h, &p->v[0], u, v + h);
        corner(&k[3], x + w, y + h, &p->v[0], u + w, v + h);
        return 4;
    }
    case BFM_PRIM_LINE_F:
    case BFM_PRIM_LINE_G: {
        const BfmPlatVertex *a = &p->v[0], *e = &p->v[1];
        const BfmPlatVertex *ca, *ce;
        float x0, y0, x1, y1;
        if (abs(e->x - a->x) >= abs(e->y - a->y) ? e->x < a->x : e->y < a->y) {
            const BfmPlatVertex *t = a; a = e; e = t;
        }
        ca = a;
        ce = kind == BFM_PRIM_LINE_G ? e : a;
        if (kind == BFM_PRIM_LINE_F) ca = ce = &p->v[0];
        x0 = a->x + ox; y0 = a->y + oy; x1 = e->x + ox; y1 = e->y + oy;
        if (abs(e->x - a->x) >= abs(e->y - a->y)) {
            /* x-major: one pixel tall, endpoints inclusive */
            corner(&k[0], x0, y0, ca, 0, 0);
            corner(&k[1], x1 + 1, y1, ce, 0, 0);
            corner(&k[2], x0, y0 + 1, ca, 0, 0);
            corner(&k[3], x1 + 1, y1 + 1, ce, 0, 0);
        } else {
            corner(&k[0], x0, y0, ca, 0, 0);
            corner(&k[1], x0 + 1, y0, ca, 0, 0);
            corner(&k[2], x1, y1 + 1, ce, 0, 0);
            corner(&k[3], x1 + 1, y1 + 1, ce, 0, 0);
        }
        return 4;
    }
    default:
        nv = prim_verts(kind);
        if (nv < 3) return 0;
        for (j = 0; j < nv; j++) {
            const BfmPlatVertex *src = is_gouraud(kind) ? &p->v[j] : &p->v[0];
            corner(&k[j], p->v[j].x + ox, p->v[j].y + oy, src, p->v[j].u, p->v[j].v);
        }
        return nv;
    }
}

/* The prim's pixels (bbox of corners in the scissor) now belong to the
 * current target. */
static void mark_drawn(BfmGlBatch *b, const Corner *k, int nk) {
    float x0 = k[0].x, x1 = k[0].x, y0 = k[0].y, y1 = k[0].y;
    int i, ix0, iy0, ix1, iy1;
    if (b->overlay) return;
    for (i = 1; i < nk; i++) {
        if (k[i].x < x0) x0 = k[i].x;
        if (k[i].x > x1) x1 = k[i].x;
        if (k[i].y < y0) y0 = k[i].y;
        if (k[i].y > y1) y1 = k[i].y;
    }
    ix0 = (int)floorf(x0); iy0 = (int)floorf(y0);
    ix1 = (int)ceilf(x1); iy1 = (int)ceilf(y1);
    if (ix0 < b->scissor.x) ix0 = b->scissor.x;
    if (iy0 < b->scissor.y) iy0 = b->scissor.y;
    if (ix1 > b->scissor.x + b->scissor.w) ix1 = b->scissor.x + b->scissor.w;
    if (iy1 > b->scissor.y + b->scissor.h) iy1 = b->scissor.y + b->scissor.h;
    if (ix0 >= ix1 || iy0 >= iy1) return;
    ix0 += b->org_x; ix1 += b->org_x; iy0 += b->org_y; iy1 += b->org_y;
    if (b->target != BFM_GL_TARGET_VRAM) {   /* margins are not VRAM */
        const BfmGlScene *s = &b->scene[b->target - BFM_GL_TARGET_SCENE0];
        if (ix0 < s->x) ix0 = s->x;
        if (ix1 > s->x + b->disp.w) ix1 = s->x + b->disp.w;
    }
    tiles_rendered(b, ix0, iy0, ix1, iy1, b->target);
    snap_touch(b, ix0, iy0, ix1, iy1);
}

size_t bfm_gl_batch_add(BfmGlBatch *b, const BfmPlatPrim *prims, size_t n) {
    size_t i, tris = 0;
    for (i = 0; i < n; i++) {
        const BfmPlatPrim *p = &prims[i];
        int kind = p->kind, textured = prim_textured(kind);
        int raw = textured && (p->flags & BFM_PRIM_FLAG_RAW_TEXTURE);
        int semi = (p->flags & BFM_PRIM_FLAG_SEMI_TRANS) != 0;
        int mset = (p->flags & BFM_PRIM_FLAG_MASK_SET) != 0;
        int rect_kind = kind == BFM_PRIM_TILE || kind == BFM_PRIM_SPRITE;
        uint16_t flags;
        DrawKey key;
        Src src;
        Corner k[4];
        BfmGlCmd *c;
        int nk, abr;

        if (kind == BFM_PRIM_FILL) {
            BfmPlatRect r;
            r.x = p->v[0].x; r.y = p->v[0].y;
            r.w = (int16_t)p->w; r.h = (int16_t)p->h;
            fill_route(b, &r, (uint32_t)p->v[0].r | ((uint32_t)p->v[0].g << 8) |
                                  ((uint32_t)p->v[0].b << 16));
            continue;
        }
        if (kind <= 0 || kind >= BFM_PRIM_KIND_COUNT) continue;
        if (!(nk = prim_corners(b, p, k))) continue;
        src.repl = textured && b->repl ? bfm_gl_repl_find(b->repl, p) : -1;
        src.snap = textured && src.repl < 0 && !b->overlay ? feedback(b, p, &src.sv) : 0;
        memset(&key, 0, sizeof key);
        key.target = b->target;
        abr = bfm_gl_decode_tpage(p->tpage).abr;
        key.blend = semi && abr == 2 ? 2 : BFM_GL_BLEND_DUAL;
        key.split = textured && semi && abr == 2 && src.repl < 0;
        key.repl = src.repl + 1;
        key.snap = src.snap;
        key.dither = b->env.dither && !rect_kind && (is_gouraud(kind) || (textured && !raw));
        key.mask_set = mset;
        key.mask_check = (p->flags & BFM_PRIM_FLAG_MASK_CHECK) != 0;
        flags = (uint16_t)((textured ? BFM_GLV_TEXTURED : 0) | (raw ? BFM_GLV_RAW : 0) |
                           (key.dither ? BFM_GLV_DITHER : 0) |
                           (src.snap ? BFM_GLV_SNAP : 0) | (mset ? BFM_GLV_MASKSET : 0) |
                           (semi ? BFM_GLV_SEMI | (abr << BFM_GLV_MODE_SHIFT) : 0));
        if (!(c = draw_cmd(b, &key, &b->scissor))) break;
        c->textured |= (uint8_t)textured;
        if (nk == 3) {
            put_tri(b, c, &k[0], &k[1], &k[2], p, flags, &src);
            tris += 1;
        } else {
            put_quad(b, c, k, p, flags, &src);
            tris += 2;
        }
        mark_drawn(b, k, nk);
    }
    return tris;
}

void bfm_gl_batch_add_copy(BfmGlBatch *b, const BfmPlatRect *r) {
    const uint16_t flags = BFM_GLV_TEXTURED | BFM_GLV_RAW | BFM_GLV_ABS;
    BfmPlatVertex white;
    BfmPlatRect full;
    Corner k[4];
    BfmGlCmd *c;
    int i;
    if (b->overlay || !r || r->w <= 0 || r->h <= 0) return;
    memset(&white, 0xFF, sizeof white);
    full.x = full.y = 0;
    full.w = BFM_GL_VRAM_W;
    full.h = BFM_GL_VRAM_H;
    for (i = -1; i < BFM_GL_SCENE_SLOTS; i++) {
        BfmPlatRect sc = full;
        float ox = 0, oy = 0;
        int target = BFM_GL_TARGET_VRAM;
        if (i >= 0) {
            BfmPlatRect area;
            if (!b->scene[i].used) continue;
            area.x = (int16_t)b->scene[i].x; area.y = (int16_t)b->scene[i].y;
            area.w = b->disp.w; area.h = b->disp.h;
            if (!rect_overlap(&area, r)) continue;
            target = BFM_GL_TARGET_SCENE0 + i;
            ox = (float)(b->margin - b->scene[i].x);
            oy = (float)-b->scene[i].y;
            sc.x = (int16_t)b->margin; sc.y = 0;
            sc.w = b->disp.w; sc.h = b->disp.h;
        }
        {
            DrawKey key;
            memset(&key, 0, sizeof key);
            key.target = target;
            key.blend = -1;
            key.copy = 1;
            if (!(c = draw_cmd(b, &key, &sc))) return;
            c->textured = 1;
        }
        corner(&k[0], r->x + ox, r->y + oy, &white, r->x, r->y);
        corner(&k[1], r->x + r->w + ox, r->y + oy, &white, r->x + r->w, r->y);
        corner(&k[2], r->x + ox, r->y + r->h + oy, &white, r->x, r->y + r->h);
        corner(&k[3], r->x + r->w + ox, r->y + r->h + oy, &white, r->x + r->w,
               r->y + r->h);
        put_quad(b, c, k, NULL, flags, NULL);
    }
    tiles_clean(b, r);
}

/* ---- CPU reference rasteriser ------------------------------------------- */

static int ref_ps1_modulation;

void bfm_gl_ref_set_ps1_modulation(int on) { ref_ps1_modulation = on != 0; }

static uint16_t *ref_px(BfmGlVram *v, int x, int y) {
    return &v->px[(size_t)y * BFM_GL_VRAM_W + x];
}

int bfm_gl_ref_draw(BfmGlVram *v, const BfmPlatDrawEnv *env, const BfmPlatPrim *p) {
    int x0, y0, x1, y1, x, y, n = 0, textured, semi, mset, mcheck, abr;
    int cx0, cy0, cx1, cy1;
    if (!v || !env || !p) return 0;
    if (p->kind == BFM_PRIM_FILL) {
        BfmPlatRect r;
        r.x = p->v[0].x; r.y = p->v[0].y; r.w = (int16_t)p->w; r.h = (int16_t)p->h;
        bfm_gl_vram_fill(v, &r, rgb_to_15((uint32_t)p->v[0].r | ((uint32_t)p->v[0].g << 8) |
                                          ((uint32_t)p->v[0].b << 16)));
        return r.w * r.h;
    }
    if (p->kind == BFM_PRIM_LINE_F || p->kind == BFM_PRIM_LINE_G) {
        /* Bresenham with inclusive endpoints (the stepping native_boot's GPU
         * controller uses), flat colour of v0, clipped to the drawing area */
        int lx0 = p->v[0].x + env->offset_x, ly0 = p->v[0].y + env->offset_y;
        int lx1 = p->v[1].x + env->offset_x, ly1 = p->v[1].y + env->offset_y;
        int dx = lx1 >= lx0 ? lx1 - lx0 : lx0 - lx1, dy = ly1 >= ly0 ? ly1 - ly0 : ly0 - ly1;
        int stx = lx0 < lx1 ? 1 : -1, sty = ly0 < ly1 ? 1 : -1, err = dx - dy, e2;
        uint16_t front = (uint16_t)((p->v[0].r >> 3) | ((p->v[0].g >> 3) << 5) |
                                    ((p->v[0].b >> 3) << 10));
        int ln_semi = (p->flags & BFM_PRIM_FLAG_SEMI_TRANS) != 0;
        int ln_mset = (p->flags & BFM_PRIM_FLAG_MASK_SET) != 0;
        int ln_mcheck = (p->flags & BFM_PRIM_FLAG_MASK_CHECK) != 0;
        int ln_abr = bfm_gl_decode_tpage(p->tpage).abr;
        for (;;) {
            if (lx0 >= env->clip.x && lx0 < env->clip.x + env->clip.w &&
                ly0 >= env->clip.y && ly0 < env->clip.y + env->clip.h &&
                lx0 >= 0 && lx0 < BFM_GL_VRAM_W && ly0 >= 0 && ly0 < BFM_GL_VRAM_H) {
                uint16_t *d = ref_px(v, lx0, ly0), f = front;
                if (!(ln_mcheck && (*d & 0x8000u))) {
                    if (ln_semi) f = bfm_gl_blend_ref(ln_abr, (uint16_t)(*d & 0x7FFF), f);
                    *d = (uint16_t)(f | (ln_mset ? 0x8000u : 0));
                    n++;
                }
            }
            if (lx0 == lx1 && ly0 == ly1) break;
            e2 = 2 * err;
            if (e2 > -dy) { err -= dy; lx0 += stx; }
            if (e2 < dx) { err += dx; ly0 += sty; }
        }
        bfm_gl_vram_mark(v, 0, 0, BFM_GL_VRAM_W, BFM_GL_VRAM_H);
        return n;
    }
    if (p->kind != BFM_PRIM_TILE && p->kind != BFM_PRIM_SPRITE) return 0;
    textured = p->kind == BFM_PRIM_SPRITE;
    semi = (p->flags & BFM_PRIM_FLAG_SEMI_TRANS) != 0;
    mset = (p->flags & BFM_PRIM_FLAG_MASK_SET) != 0;
    mcheck = (p->flags & BFM_PRIM_FLAG_MASK_CHECK) != 0;
    abr = bfm_gl_decode_tpage(p->tpage).abr;
    x0 = p->v[0].x + env->offset_x;
    y0 = p->v[0].y + env->offset_y;
    x1 = x0 + p->w;
    y1 = y0 + p->h;
    cx0 = env->clip.x; cy0 = env->clip.y;
    cx1 = env->clip.x + env->clip.w; cy1 = env->clip.y + env->clip.h;
    for (y = y0; y < y1; y++) {
        if (y < cy0 || y >= cy1 || y < 0 || y >= BFM_GL_VRAM_H) continue;
        for (x = x0; x < x1; x++) {
            uint16_t *d, front, stp = 0;
            if (x < cx0 || x >= cx1 || x < 0 || x >= BFM_GL_VRAM_W) continue;
            d = ref_px(v, x, y);
            if (textured) {
                uint16_t t = bfm_gl_sample(v, p->tpage, p->clut, p->v[0].u + (x - x0),
                                           p->v[0].v + (y - y0),
                                           env->texture_window.w || env->texture_window.h
                                               ? &env->texture_window : NULL);
                uint8_t m[3];
                if (t == 0) continue;
                stp = (uint16_t)(t & 0x8000u);
                if (ref_ps1_modulation && !(p->flags & BFM_PRIM_FLAG_RAW_TEXTURE)) {
                    const int c[3] = {p->v[0].r, p->v[0].g, p->v[0].b};
                    int ch;
                    front = 0;
                    for (ch = 0; ch < 3; ch++) {
                        int o = (((t >> (5 * ch)) & 31) * c[ch]) >> 7;
                        front |= (uint16_t)((o > 31 ? 31 : o) << (5 * ch));
                    }
                } else {
                    bfm_gl_modulate(t, p->v[0].r, p->v[0].g, p->v[0].b,
                                    (p->flags & BFM_PRIM_FLAG_RAW_TEXTURE) != 0, m);
                    front = (uint16_t)((m[0] >> 3) | ((m[1] >> 3) << 5) | ((m[2] >> 3) << 10));
                }
            } else {
                front = (uint16_t)((p->v[0].r >> 3) | ((p->v[0].g >> 3) << 5) |
                                   ((p->v[0].b >> 3) << 10));
            }
            if (mcheck && (*d & 0x8000u)) continue;
            if (semi && (!textured || stp))
                front = bfm_gl_blend_ref(abr, (uint16_t)(*d & 0x7FFF), front);
            *d = (uint16_t)(front | stp | (mset ? 0x8000u : 0));
            n++;
        }
    }
    bfm_gl_vram_mark(v, x0, y0, p->w, p->h);
    return n;
}
