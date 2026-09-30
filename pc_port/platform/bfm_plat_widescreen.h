#ifndef BFM_PLAT_WIDESCREEN_H
#define BFM_PLAT_WIDESCREEN_H

/* Widescreen: projection maths and the game's GTE projection state.
 *
 * The PS1 display is always shown at 4:3, whatever its pixel width (256,
 * 320, 368, 512, 640). For a wider output aspect A = num/den there are two
 * modes:
 *
 *  hor+        The projection is unchanged. The renderer shows a wider window
 *              with unstretched pixels, so geometry the GTE puts left of 0 or
 *              right of the display width becomes visible. Visible width in
 *              display pixels is  disp_w * A / (4/3),  centred on the display.
 *              PsyCross (PGXP) already maps the display this way into a wide
 *              window. The cost: objects the game culls at the 4:3 edges
 *              pop at the sides.
 *  anamorphic  Screen X is squeezed around OFX by  k = (4/3) / A  after
 *              perspective division, and the renderer stretches the 4:3
 *              image to A. The game's own culling stays correct. 2D HUD
 *              elements are stretched. It needs a renderer that stretches
 *              (and the GTE backend applying bfm_plat_widescreen_sx).
 *
 * All maths is 16.16 fixed point, so every backend rounds identically. */

#include "bfm_plat_types.h"
#include "bfm_plat_config.h"

typedef struct BfmPlatWidescreen {
    int aspect_num, aspect_den;
    int mode;                   /* BFM_WIDESCREEN_* */
    int active;                 /* aspect wider than 4:3 */
    uint32_t sx_scale_q16;      /* anamorphic X factor (65536 = 1.0) */
    int disp_w, disp_h;
    int visible_x0;             /* hor+: first visible display pixel (<= 0) */
    int visible_w;              /* hor+: visible width in display pixels */
} BfmPlatWidescreen;

/* Aspect must be >= 4:3 (narrower is refused). disp_w/h: display size. */
int bfm_plat_widescreen_compute(int aspect_num, int aspect_den, int mode,
                                int disp_w, int disp_h, BfmPlatWidescreen *out);
int bfm_plat_widescreen_from_config(const BfmPlatConfig *cfg, int disp_w,
                                    int disp_h, BfmPlatWidescreen *out);

/* Screen-space X after the widescreen transform, for a projected sx and the
 * current OFX in pixels. Identity unless anamorphic is active. Rounds
 * half away from OFX. */
int32_t bfm_plat_widescreen_sx(const BfmPlatWidescreen *ws, int32_t sx,
                               int32_t ofx);

/* Output window for a given height at the configured aspect (width rounded
 * to an even number). */
void bfm_plat_widescreen_window(const BfmPlatWidescreen *ws, int height,
                                int *width);

/* ---- The game's GTE projection state ----------------------------------
 * Hook sites at the libgte setters (bfm_plat_hook_sites.c) record what the
 * game programs, so renderers and plugins can read it:
 *   SetGeomScreen(h)        func_8004923C  (ctc2 $a0, $26)
 *   SetGeomOffset(ofx, ofy) func_8004921C  (ctc2 $a0<<16, $24 / $a1<<16, $25)
 * Each change emits BFM_EVENT_PROJECTION. */
typedef struct BfmPlatProjection {
    int32_t h;                  /* GTE H (screen distance), retail 1000 */
    int32_t ofx, ofy;           /* screen offset in pixels */
    uint32_t updates;
} BfmPlatProjection;

void bfm_plat_projection_set_screen(int32_t h);
void bfm_plat_projection_set_offset(int32_t ofx, int32_t ofy);
void bfm_plat_projection_get(BfmPlatProjection *out);
/* Hook-site path: record without emitting (the hook dispatcher emits). */
void bfm_plat_projection_record(int32_t h, int32_t ofx, int32_t ofy);
void bfm_plat_projection_reset(void);

/* The widescreen state for the current config and display (last
 * set_disp_env), recomputed when either changes. Never NULL. */
const BfmPlatWidescreen *bfm_plat_widescreen_current(void);
/* bfm_plat_widescreen_sx with the game's current OFX: what a GTE backend
 * calls on each projected SX (RTPS/RTPT) when anamorphic is on. */
int32_t bfm_plat_widescreen_sx_current(int32_t sx);

/* Horizontal field of view in 1/1000 degrees for H and a visible width
 * (2*atan(w/2 / H)), for logs and plugins. */
int32_t bfm_plat_projection_hfov_mdeg(int32_t h, int32_t visible_w);

#endif
