#include "bfm_plat_widescreen.h"
#include "bfm_plat.h"

#include <math.h>
#include <string.h>

int bfm_plat_widescreen_compute(int num, int den, int mode, int disp_w,
                                int disp_h, BfmPlatWidescreen *out) {
    uint64_t scale;
    int64_t vis;
    if (!out || num <= 0 || den <= 0 || disp_w <= 0 || disp_h <= 0 ||
        (mode != BFM_WIDESCREEN_HOR_PLUS && mode != BFM_WIDESCREEN_ANAMORPHIC))
        return BFM_PLAT_INVALID;
    /* A < 4/3 ? (num*3 < den*4) */
    if ((int64_t)num * 3 < (int64_t)den * 4) return BFM_PLAT_INVALID;
    memset(out, 0, sizeof *out);
    out->aspect_num = num;
    out->aspect_den = den;
    out->mode = mode;
    out->disp_w = disp_w;
    out->disp_h = disp_h;
    out->active = (int64_t)num * 3 != (int64_t)den * 4;
    /* k = (4/3)/(num/den) = 4*den / (3*num), rounded to nearest */
    scale = ((uint64_t)4 * (uint64_t)den * 65536u + (uint64_t)(3 * num) / 2u) /
            (uint64_t)(3 * num);
    out->sx_scale_q16 = (mode == BFM_WIDESCREEN_ANAMORPHIC) ? (uint32_t)scale : 65536u;
    if (mode == BFM_WIDESCREEN_HOR_PLUS) {
        /* visible = disp_w * (num/den) / (4/3) = disp_w*3*num / (4*den) */
        vis = ((int64_t)disp_w * 3 * num + 2 * den) / (4 * (int64_t)den);
        if ((vis - disp_w) & 1) vis++;          /* keep it centred */
        out->visible_w = (int)vis;
        out->visible_x0 = -(int)((vis - disp_w) / 2);
    } else {
        out->visible_w = disp_w;
        out->visible_x0 = 0;
    }
    return BFM_PLAT_OK;
}

int bfm_plat_widescreen_from_config(const BfmPlatConfig *cfg, int disp_w,
                                    int disp_h, BfmPlatWidescreen *out) {
    int n, d;
    bfm_plat_config_aspect(cfg, &n, &d);
    return bfm_plat_widescreen_compute(n, d, cfg ? cfg->widescreen_mode : 0,
                                       disp_w, disp_h, out);
}

int32_t bfm_plat_widescreen_sx(const BfmPlatWidescreen *ws, int32_t sx,
                               int32_t ofx) {
    int64_t d, q;
    if (!ws || !ws->active || ws->mode != BFM_WIDESCREEN_ANAMORPHIC) return sx;
    d = (int64_t)sx - ofx;
    q = d * (int64_t)ws->sx_scale_q16;
    q = q >= 0 ? (q + 32768) >> 16 : -((-q + 32768) >> 16);
    return (int32_t)(ofx + q);
}

void bfm_plat_widescreen_window(const BfmPlatWidescreen *ws, int height,
                                int *width) {
    int64_t w;
    if (!width) return;
    if (!ws || height <= 0) { *width = 0; return; }
    w = ((int64_t)height * ws->aspect_num + ws->aspect_den / 2) / ws->aspect_den;
    *width = (int)(w + (w & 1));
}

static BfmPlatProjection proj = {1000, 0, 0, 0};

static void emit(void) {
    BfmEventProjection e;
    proj.updates++;
    e.h = proj.h;
    e.ofx = proj.ofx;
    e.ofy = proj.ofy;
    bfm_plat_mods_emit(BFM_EVENT_PROJECTION, &e);
}

void bfm_plat_projection_set_screen(int32_t h) {
    proj.h = h;
    emit();
}

void bfm_plat_projection_set_offset(int32_t ofx, int32_t ofy) {
    proj.ofx = ofx;
    proj.ofy = ofy;
    emit();
}

void bfm_plat_projection_record(int32_t h, int32_t ofx, int32_t ofy) {
    proj.h = h;
    proj.ofx = ofx;
    proj.ofy = ofy;
    proj.updates++;
}

void bfm_plat_projection_get(BfmPlatProjection *out) {
    if (out) *out = proj;
}

void bfm_plat_projection_reset(void) {
    proj.h = 1000;
    proj.ofx = proj.ofy = 0;
    proj.updates = 0;
}

int32_t bfm_plat_projection_hfov_mdeg(int32_t h, int32_t w) {
    if (h <= 0 || w <= 0) return 0;
    return (int32_t)lround(2.0 * atan((double)w / 2.0 / (double)h) * 180000.0 /
                           3.14159265358979323846);
}

static BfmPlatWidescreen cur;
static int cur_key[5] = {-1, -1, -1, -1, -1};

const BfmPlatWidescreen *bfm_plat_widescreen_current(void) {
    const BfmPlatConfig *cfg = bfm_plat_config();
    BfmPlatDispEnv d;
    int n, den, w, h;
    bfm_plat_renderer_last_disp_env(&d);
    bfm_plat_config_aspect(cfg, &n, &den);
    w = d.disp.w > 0 ? d.disp.w : 320;
    h = d.disp.h > 0 ? d.disp.h : 240;
    if (cur_key[0] != n || cur_key[1] != den || cur_key[2] != cfg->widescreen_mode ||
        cur_key[3] != w || cur_key[4] != h) {
        if (bfm_plat_widescreen_compute(n, den, cfg->widescreen_mode, w, h, &cur) != BFM_PLAT_OK)
            bfm_plat_widescreen_compute(4, 3, BFM_WIDESCREEN_HOR_PLUS, w, h, &cur);
        cur_key[0] = n; cur_key[1] = den; cur_key[2] = cfg->widescreen_mode;
        cur_key[3] = w; cur_key[4] = h;
    }
    return &cur;
}

int32_t bfm_plat_widescreen_sx_current(int32_t sx) {
    return bfm_plat_widescreen_sx(bfm_plat_widescreen_current(), sx, proj.ofx);
}
