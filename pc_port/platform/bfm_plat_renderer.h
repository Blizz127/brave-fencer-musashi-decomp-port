#ifndef BFM_PLAT_RENDERER_H
#define BFM_PLAT_RENDERER_H

/* Renderer interface, defined by meaning rather than by GPU registers.
 *
 * Game C (or the libgpu-shaped shim that stands in for the retail Psy-Q
 * calls LoadImage/PutDrawEnv/DrawOTag/...) hands the renderer draw and
 * display environments, semantic primitives in submission order, and VRAM
 * uploads/downloads. A PS1-accurate backend rasterizes into 1024x512 VRAM;
 * a modern backend may upscale, replace textures, widen the view or
 * interpolate frames, because it sees what is drawn instead of GP0 words.
 *
 * Every VRAM upload passes through the mods texture hook first
 * (bfm_plat_mods_replace_texture), so replacement works on every backend. */

#include "bfm_plat_types.h"

typedef struct BfmPlatDrawEnv {
    BfmPlatRect clip;          /* drawing area */
    int16_t offset_x, offset_y;
    BfmPlatRect texture_window;
    uint16_t tpage;            /* PS1 texpage word: base, abr, color depth */
    uint8_t dither;
    uint8_t draw_on_display;
    uint8_t clear_bg;          /* clear clip rect to bg colour on apply */
    uint8_t bg_r, bg_g, bg_b;
} BfmPlatDrawEnv;

typedef struct BfmPlatDispEnv {
    BfmPlatRect disp;          /* VRAM area shown */
    BfmPlatRect screen;        /* output window adjustment */
    uint8_t interlaced;
    uint8_t rgb24;
} BfmPlatDispEnv;

typedef enum BfmPlatPrimKind {
    BFM_PRIM_POLY_F3 = 1,
    BFM_PRIM_POLY_F4,
    BFM_PRIM_POLY_G3,
    BFM_PRIM_POLY_G4,
    BFM_PRIM_POLY_FT3,
    BFM_PRIM_POLY_FT4,
    BFM_PRIM_POLY_GT3,
    BFM_PRIM_POLY_GT4,
    BFM_PRIM_LINE_F,
    BFM_PRIM_LINE_G,
    BFM_PRIM_TILE,             /* flat rect: v[0] = xy, w/h */
    BFM_PRIM_SPRITE,           /* textured rect: v[0] = xy/uv, w/h */
    BFM_PRIM_FILL,             /* VRAM fill, ignores draw env: v[0], w/h */
    BFM_PRIM_KIND_COUNT
} BfmPlatPrimKind;

typedef struct BfmPlatVertex {
    int16_t x, y;
    uint8_t u, v;
    uint8_t r, g, b;
    uint8_t pad;
} BfmPlatVertex;

#define BFM_PRIM_FLAG_SEMI_TRANS 0x01u
#define BFM_PRIM_FLAG_RAW_TEXTURE 0x02u  /* no colour modulation */
/* GP0 E6 mask state in effect for the primitive: set bit 15 on every pixel
 * written / leave pixels whose bit 15 is set untouched. */
#define BFM_PRIM_FLAG_MASK_SET 0x04u
#define BFM_PRIM_FLAG_MASK_CHECK 0x08u

typedef struct BfmPlatPrim {
    uint8_t kind;              /* BfmPlatPrimKind */
    uint8_t flags;
    uint16_t tpage;            /* texpage (E1) in effect; abr applies to all */
    uint16_t clut;
    uint16_t w, h;             /* TILE / SPRITE / FILL */
    uint32_t depth;            /* ordering-table slot, for sorting renderers */
    BfmPlatVertex v[4];
} BfmPlatPrim;

/* A replacement texture served from a mod: RGBA8888, width/height may be a
 * multiple of the original rectangle (HD replacement). */
typedef struct BfmPlatImage {
    uint32_t width, height;
    const uint8_t *rgba;
} BfmPlatImage;

typedef struct BfmPlatOutput {
    int window_width, window_height;
    int internal_scale;
    int aspect_num, aspect_den;
    int frame_rate;
    int fullscreen;
    int vsync;
} BfmPlatOutput;

typedef struct BfmPlatRendererStats {
    uint64_t frames;
    uint64_t prims;
    uint64_t prims_by_kind[BFM_PRIM_KIND_COUNT];
    uint64_t uploads;
    uint64_t replaced_uploads;
    uint64_t downloads;
    uint64_t draw_envs, disp_envs;
} BfmPlatRendererStats;

/* Backend vtable. Required: name. Anything NULL returns BFM_PLAT_UNSUPPORTED
 * (or is a no-op for begin_frame/overlay_text). */
typedef struct BfmPlatRendererBackend {
    const char *name;
    int (*init)(void *self, const BfmPlatOutput *out);
    void (*shutdown)(void *self);
    int (*set_output)(void *self, const BfmPlatOutput *out);
    int (*begin_frame)(void *self);
    int (*set_draw_env)(void *self, const BfmPlatDrawEnv *env);
    int (*set_disp_env)(void *self, const BfmPlatDispEnv *env);
    int (*submit)(void *self, const BfmPlatPrim *prims, size_t count);
    int (*upload_vram)(void *self, const BfmPlatRect *rect,
                       const uint16_t *pixels);
    /* Optional HD path: called instead of upload_vram when a mod supplies a
     * replacement. The original pixels are passed too so a backend without
     * HD support can still keep VRAM consistent. */
    int (*upload_replacement)(void *self, const BfmPlatRect *rect,
                              const uint16_t *original,
                              const BfmPlatImage *replacement);
    int (*download_vram)(void *self, const BfmPlatRect *rect,
                         uint16_t *pixels);
    int (*present)(void *self);
    /* Debug/cheat-menu text overlay in output pixels; optional. */
    int (*overlay_text)(void *self, int x, int y, const char *text);
    void *self;
} BfmPlatRendererBackend;

/* Registry. Up to 8 backends; names must be unique. The built-in "null"
 * backend is always registered. */
int bfm_plat_renderer_register(const BfmPlatRendererBackend *backend);
const BfmPlatRendererBackend *bfm_plat_renderer_find(const char *name);

/* Selects and initializes a backend (falls back to "null" if name is
 * unknown; *selected receives the actual name if non-NULL). */
int bfm_plat_renderer_open(const char *name, const BfmPlatOutput *out,
                           const char **selected);
void bfm_plat_renderer_close(void);
const char *bfm_plat_renderer_active(void);

/* Game-facing calls. */
int bfm_plat_renderer_set_output(const BfmPlatOutput *out);
int bfm_plat_renderer_begin_frame(void);
int bfm_plat_renderer_set_draw_env(const BfmPlatDrawEnv *env);
int bfm_plat_renderer_set_disp_env(const BfmPlatDispEnv *env);
int bfm_plat_renderer_submit(const BfmPlatPrim *prims, size_t count);
int bfm_plat_renderer_upload_vram(const BfmPlatRect *rect,
                                  const uint16_t *pixels);
int bfm_plat_renderer_download_vram(const BfmPlatRect *rect,
                                    uint16_t *pixels);
int bfm_plat_renderer_present(void);
int bfm_plat_renderer_overlay_text(int x, int y, const char *text);
void bfm_plat_renderer_stats(BfmPlatRendererStats *out);
/* The last display environment set (default 0,0 320x240, 15-bit). */
void bfm_plat_renderer_last_disp_env(BfmPlatDispEnv *out);

/* Rect must be non-empty and inside 1024x512 VRAM. */
int bfm_plat_rect_valid(const BfmPlatRect *rect);

/* Built-in "null" backend keeps a real 1024x512 VRAM so uploads/downloads
 * round-trip (tests, headless runs, the route autopilot). Exposed for
 * backends that want a software VRAM copy. */
const uint16_t *bfm_plat_renderer_null_vram(void);

/* Test support: unregisters everything but "null" and clears stats. */
void bfm_plat_renderer_reset(void);

#endif
