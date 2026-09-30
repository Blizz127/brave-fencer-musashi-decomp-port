#ifndef BFM_GL_CORE_H
#define BFM_GL_CORE_H

/* CPU side of the `gl` renderer backend: everything that needs no GL
 * context, so it is unit-tested headless (tests/bfm_gl_core_probe.c).
 *
 *  - the 1024x512 16-bit VRAM mirror (upload/download/move/fill) behind
 *    LoadImage/StoreImage/MoveImage; the GPU copy is an R16UI texture;
 *  - texture-page / CLUT decode and a reference texel sampler (4/8/15-bit,
 *    texture window) that the fragment shader implements identically;
 *  - PS1 semi-transparency formulas, the GL blend state realising each and
 *    a float model of that GL state (to check the two agree);
 *  - the PS1 4x4 dither matrix;
 *  - the batcher: BfmPlatPrim -> triangles plus an ordered command list;
 *  - hor+ routing: draw areas shaped like the display ("scene slots", one per
 *    frame buffer) render into wider targets so geometry beyond the 4:3
 *    edges survives;
 *  - the HD replacement registry (VRAM rect -> replacement texture).
 *
 * Coordinates: VRAM pixels. Vertex positions in the command list are
 * target-local pixels at 1x; the executor scales by the internal
 * resolution. */

#include <stddef.h>
#include <stdint.h>

#include "../../bfm_plat_renderer.h"

#define BFM_GL_VRAM_W 1024
#define BFM_GL_VRAM_H 512

/* ---- VRAM mirror ---- */
typedef struct BfmGlVram {
    uint16_t px[BFM_GL_VRAM_W * BFM_GL_VRAM_H];
    int dirty;                   /* dirty box since the last GPU upload */
    int dx0, dy0, dx1, dy1;      /* [x0,x1) x [y0,y1) */
} BfmGlVram;

void bfm_gl_vram_init(BfmGlVram *v);
int bfm_gl_vram_upload(BfmGlVram *v, const BfmPlatRect *r, const uint16_t *px);
int bfm_gl_vram_download(const BfmGlVram *v, const BfmPlatRect *r, uint16_t *px);
/* MoveImage: copies src rect to (x, y); overlapping moves behave as if the
 * source were read completely first. Wraps at the VRAM edges like the PS1. */
int bfm_gl_vram_move(BfmGlVram *v, const BfmPlatRect *src, int x, int y);
int bfm_gl_vram_fill(BfmGlVram *v, const BfmPlatRect *r, uint16_t color);
void bfm_gl_vram_mark(BfmGlVram *v, int x, int y, int w, int h);

/* 24-bit display mode (MDEC movies): the display area holds packed RGB888,
 * 3 bytes per pixel in little-endian halfword order, starting at VRAM
 * (disp.x, disp.y); disp.w is in pixels. Writes disp.w x disp.h RGBA8888. */
void bfm_gl_decode_rgb24(const BfmGlVram *v, const BfmPlatRect *disp,
                         uint8_t *rgba);

/* ---- texture pages ---- */
typedef struct BfmGlTpage {
    int base_x, base_y;          /* VRAM pixels */
    int depth;                   /* 4, 8 or 15 */
    int abr;                     /* semi-transparency mode 0..3 */
} BfmGlTpage;

BfmGlTpage bfm_gl_decode_tpage(uint16_t tpage);
void bfm_gl_decode_clut(uint16_t clut, int *x, int *y);
/* Texture window (PsyQ RECT tw: offset x/y, power-of-two size w/h; w or
 * h == 0 disables that axis): u' = (u & (w-1)) | (x & ~(w-1)). */
int bfm_gl_window(int u, int off, int size);
/* The 16-bit texel the GPU fetches for page coords (u, v); 0x0000 is
 * transparent. win may be NULL. */
uint16_t bfm_gl_sample(const BfmGlVram *v, uint16_t tpage, uint16_t clut,
                       int u, int vv, const BfmPlatRect *win);
/* Final 8-bit colour of a textured fragment: texel modulated by the vertex
 * colour ((t * c) >> 7 in 5-bit units, as the PS1) unless raw. */
void bfm_gl_modulate(uint16_t texel, int r, int g, int b, int raw,
                     uint8_t out[3]);

/* ---- semi-transparency ---- */
/* PS1 formula on 5-bit channels, clamped 0..31:
 *   0: B/2 + F/2   1: B + F   2: B - F   3: B + F/4 */
uint16_t bfm_gl_blend_ref(int mode, uint16_t back, uint16_t front);

#define BFM_GL_FUNC_ADD 0
#define BFM_GL_FUNC_REVERSE_SUBTRACT 1   /* dst - src */
#define BFM_GL_ONE 0
#define BFM_GL_CONSTANT_COLOR 1

typedef struct BfmGlBlendState {
    int equation;
    int src, dst;
    float constant;              /* glBlendColor value for CONSTANT_COLOR */
} BfmGlBlendState;

BfmGlBlendState bfm_gl_blend_state(int mode);
/* The GL blend on an RGBA8 target, in float, for 5-bit inputs; returns the
 * result mapped back to 5-bit channels (rounded). */
uint16_t bfm_gl_blend_emulate(const BfmGlBlendState *s, uint16_t back,
                              uint16_t front);

/* ---- dither ---- */
extern const int8_t bfm_gl_dither_matrix[4][4];
/* 8-bit channel at native pixel (x, y) -> dithered, truncated to 5 bits */
int bfm_gl_dither5(int c8, int x, int y);

/* ---- HD replacements ---- */
#define BFM_GL_MAX_REPLACEMENTS 256
typedef struct BfmGlReplacement {
    BfmPlatRect rect;            /* VRAM rect of the original upload */
    uint32_t width, height;      /* replacement image size */
    uint32_t id;                 /* executor texture name */
    int live;
} BfmGlReplacement;

typedef struct BfmGlReplacements {
    BfmGlReplacement r[BFM_GL_MAX_REPLACEMENTS];
} BfmGlReplacements;

/* Registers a replacement (reusing the slot of the same rect, then a dead
 * slot). Returns the slot index, or -1 when full. *old_id receives the
 * texture the slot held (0 = none) so the caller can delete it. */
int bfm_gl_repl_add(BfmGlReplacements *set, const BfmPlatRect *rect,
                    uint32_t width, uint32_t height, uint32_t id,
                    uint32_t *old_id);
/* A plain upload overlapping a replaced rect kills that replacement (the
 * game changed the data). Returns how many died; their ids go to dead[]
 * (up to max) for deletion. */
int bfm_gl_repl_invalidate(BfmGlReplacements *set, const BfmPlatRect *rect,
                           uint32_t *dead, int max);
/* The replacement containing the whole texel footprint of a textured prim,
 * or -1. Footprint: the page texels [umin, umax) x [vmin, vmax) the prim
 * spans (sprites: u0..u0+w). */
int bfm_gl_repl_find(const BfmGlReplacements *set, const BfmPlatPrim *p);
/* Normalised replacement coordinates of page texel (u, v). */
void bfm_gl_repl_coords(const BfmGlReplacement *r, uint16_t tpage, float u,
                        float v, float *s, float *t);

/* ---- batching ---- */
typedef struct BfmGlVertex {
    float x, y;                  /* target-local pixels at 1x */
    uint8_t r, g, b, a;
    uint16_t u, v;               /* page texel coords (ABS: VRAM coords) */
    uint16_t clut, tpage;
    uint16_t flags;              /* BFM_GLV_* */
    uint16_t repl;               /* replacement slot + 1, 0 = none */
    float s, t;                  /* replacement coords */
} BfmGlVertex;                   /* 32 bytes */

#define BFM_GLV_TEXTURED 0x01u
#define BFM_GLV_RAW      0x02u   /* no colour modulation */
#define BFM_GLV_DITHER   0x04u
#define BFM_GLV_ABS      0x08u   /* 15-bit copy from absolute VRAM coords */
#define BFM_GLV_SNAP     0x10u   /* sample the command's render snapshot */
#define BFM_GLV_MASKSET  0x20u   /* E6 set-mask: written pixels get bit 15 */
#define BFM_GLV_SEMI     0x40u   /* semi-transparent prim; mode in bits 8-9 */
#define BFM_GLV_MODE_SHIFT 8

/* DRAW command blending (BfmGlCmd.blend):
 *   -1                opaque, blending off (uploads copied into targets)
 *   BFM_GL_BLEND_DUAL the fragment decides: opaque, or semi mode 0/1/3,
 *                     through dual-source factors (src * sf + dst * df),
 *                     so opaque and semi prims share draws in PS1 order
 *   2                 mode 2 (B - F) needs FUNC_REVERSE_SUBTRACT: own
 *                     draws; textured ones split opaque/STP (split_stp) */
#define BFM_GL_BLEND_DUAL 4

typedef enum BfmGlCmdKind {
    BFM_GL_CMD_DRAW = 1,         /* triangles [first, first + count) */
    BFM_GL_CMD_FILL,             /* rect <- rgb, mask bit 0, no blending */
    /* Render feedback (textures sampling pixels the GPU drew):
     * SNAPSHOT copies target-local rect of `target` into snapshot `snap`-1
     * at full internal resolution (15-bit pages keep HD detail);
     * RESOLVE reads target-local rect back into the mirror at vrect and
     * re-uploads the VRAM texture (CLUT pages, texture windows, StoreImage). */
    BFM_GL_CMD_SNAPSHOT,
    BFM_GL_CMD_RESOLVE
} BfmGlCmdKind;

#define BFM_GL_TARGET_VRAM 0     /* the scaled 1024x512 target */
#define BFM_GL_TARGET_SCENE0 1   /* scene slot 0 / 1: wide frame buffers */
#define BFM_GL_TARGET_SCENE1 2
#define BFM_GL_TARGET_WINDOW 3   /* overlay: display pixels on the window */
#define BFM_GL_SCENE_SLOTS 2
#define BFM_GL_MAX_SNAPSHOTS 32  /* per command list */

/* Tile map: which render target holds the newest pixels of each 16x16
 * VRAM tile (0 = the mirror is current, else target + 1). */
#define BFM_GL_TILE 16
#define BFM_GL_TILES_X (BFM_GL_VRAM_W / BFM_GL_TILE)
#define BFM_GL_TILES_Y (BFM_GL_VRAM_H / BFM_GL_TILE)

typedef struct BfmGlCmd {
    uint8_t kind;
    uint8_t target;
    int8_t blend;                /* see BFM_GL_BLEND_DUAL */
    uint8_t split_stp;           /* textured mode 2: only STP texels blend */
    uint32_t first, count;       /* vertices (DRAW) */
    uint16_t repl;               /* slot + 1 for the whole batch, 0 = none */
    uint8_t dither;              /* batch may dither (vertex flag decides) */
    uint8_t copy;                /* an upload copied into the target */
    uint8_t mask_set, mask_check;/* E6: some prim sets / all check */
    uint8_t mask_clear;          /* some prim does not set (mask_set is
                                  * uniform in split_stp commands) */
    uint8_t textured;            /* some prim samples a texture */
    uint16_t snap;               /* DRAW/SNAPSHOT: snapshot index + 1 */
    BfmPlatRect vrect;           /* SNAPSHOT/RESOLVE: the VRAM rect */
    BfmPlatRect rect;            /* DRAW: scissor; FILL: area (target-local) */
    BfmPlatRect win;             /* DRAW: texture window of the env */
    uint32_t rgb;                /* FILL: 0xBBGGRR */
} BfmGlCmd;

typedef struct BfmGlScene {
    int used;
    int x, y;                    /* VRAM origin of the frame buffer */
    uint32_t stamp;              /* LRU */
} BfmGlScene;

typedef struct BfmGlBatch {
    BfmGlVertex *v;
    size_t nv, cap_v;
    BfmGlCmd *c;
    size_t nc, cap_c;
    BfmPlatDrawEnv env;
    int target;                  /* for the current env */
    BfmPlatRect scissor;         /* target-local */
    int org_x, org_y;            /* VRAM -> target-local: subtract */
    BfmPlatRect disp;            /* display area */
    int margin;                  /* hor+ extra pixels on each side */
    BfmGlScene scene[BFM_GL_SCENE_SLOTS];
    uint32_t tick;
    int overlay;                 /* this batch draws to the window */
    const BfmGlReplacements *repl;
    BfmGlVram *vram;             /* fills also land in the mirror */
    int oom;
    uint8_t owner[BFM_GL_TILES_Y][BFM_GL_TILES_X];
    unsigned nsnap;              /* snapshots in this command list */
    long last_snap;              /* the last SNAPSHOT cmd while still current, -1 */
    uint32_t feedback_snaps, feedback_resolves;   /* statistics */
} BfmGlBatch;

void bfm_gl_batch_init(BfmGlBatch *b);
void bfm_gl_batch_free(BfmGlBatch *b);
/* Drops the command list (after the executor ran it); keeps state. */
void bfm_gl_batch_clear(BfmGlBatch *b);
/* Display area and hor+ margin (1x pixels on each side). A change of size
 * or margin forgets the scene slots. */
void bfm_gl_batch_set_display(BfmGlBatch *b, const BfmPlatRect *disp,
                              int margin);
void bfm_gl_batch_set_env(BfmGlBatch *b, const BfmPlatDrawEnv *env);
/* Appends prims; returns the number of triangles generated. */
size_t bfm_gl_batch_add(BfmGlBatch *b, const BfmPlatPrim *p, size_t n);
/* A VRAM upload landing in render targets: copies the rect from the VRAM
 * texture into the VRAM target and any scene slot it overlaps. */
void bfm_gl_batch_add_copy(BfmGlBatch *b, const BfmPlatRect *rect);
/* StoreImage support: RESOLVE commands for every rendered tile in rect,
 * so the mirror holds what was drawn once the list has run. */
void bfm_gl_batch_add_resolve(BfmGlBatch *b, const BfmPlatRect *rect);
/* Target + 1 holding tile (x, y) of VRAM pixel coordinates, 0 = mirror. */
int bfm_gl_batch_owner(const BfmGlBatch *b, int x, int y);
/* The scene slot showing VRAM origin (x, y), or -1. */
int bfm_gl_batch_scene_at(const BfmGlBatch *b, int x, int y);
/* Target size in 1x pixels. */
void bfm_gl_batch_target_size(const BfmGlBatch *b, int target, int *w, int *h);

/* ---- CPU reference rasteriser (tests, and the definition of exact) ----
 * Draws TILE / SPRITE / FILL and lines (Bresenham, inclusive endpoints,
 * flat colour of v0, clipped to the drawing area) into a VRAM image with
 * PS1 semantics: env clip
 * and offset, texture window, CLUT decode, transparency of 0x0000,
 * modulation (as the GL path rounds it: bfm_gl_modulate >> 3), semi-
 * transparency by the prim's abr on untextured pixels and STP texels,
 * E6 mask check (skip pixels with bit 15) and set (force bit 15), bit 15
 * of written pixels = texel STP | mask-set, fills clear bit 15. Returns the
 * number of pixels written. */
int bfm_gl_ref_draw(BfmGlVram *v, const BfmPlatDrawEnv *env, const BfmPlatPrim *p);
/* Texture modulation in bfm_gl_ref_draw: 0 (default) = the gl renderer's
 * 8-bit path (bfm_gl_modulate >> 3, what the GL tests compare against);
 * 1 = the PS1 integer formula min(31, (t * c) >> 7), for comparing with a
 * PS1-exact GPU model. They differ by at most one 5-bit step. */
void bfm_gl_ref_set_ps1_modulation(int on);

#endif
