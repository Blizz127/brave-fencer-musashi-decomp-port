#include "bfm_plat_renderer.h"
#include "bfm_plat_mods.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BACKENDS 8
#define VRAM_W 1024
#define VRAM_H 512

static uint16_t null_vram[VRAM_W * VRAM_H];

static int null_upload(void *self, const BfmPlatRect *r, const uint16_t *px) {
    int y;
    (void)self;
    for (y = 0; y < r->h; y++)
        memcpy(&null_vram[(r->y + y) * VRAM_W + r->x], px + (size_t)y * r->w,
               (size_t)r->w * 2u);
    return BFM_PLAT_OK;
}

static int null_download(void *self, const BfmPlatRect *r, uint16_t *px) {
    int y;
    (void)self;
    for (y = 0; y < r->h; y++)
        memcpy(px + (size_t)y * r->w, &null_vram[(r->y + y) * VRAM_W + r->x],
               (size_t)r->w * 2u);
    return BFM_PLAT_OK;
}

static int null_submit(void *self, const BfmPlatPrim *p, size_t n) {
    size_t i;
    (void)self;
    /* FILL is the one primitive whose result is fully defined without a
     * rasterizer; keep VRAM honest for it. */
    for (i = 0; i < n; i++) {
        if (p[i].kind == BFM_PRIM_FILL) {
            BfmPlatRect r;
            uint16_t c = (uint16_t)((p[i].v[0].r >> 3) |
                                    ((p[i].v[0].g >> 3) << 5) |
                                    ((p[i].v[0].b >> 3) << 10));
            int x, y;
            r.x = p[i].v[0].x; r.y = p[i].v[0].y;
            r.w = (int16_t)p[i].w; r.h = (int16_t)p[i].h;
            if (!bfm_plat_rect_valid(&r)) continue;
            for (y = 0; y < r.h; y++)
                for (x = 0; x < r.w; x++)
                    null_vram[(r.y + y) * VRAM_W + r.x + x] = c;
        }
    }
    return BFM_PLAT_OK;
}

static int null_ok(void *self) { (void)self; return BFM_PLAT_OK; }
static int null_init(void *self, const BfmPlatOutput *o) {
    (void)self; (void)o;
    memset(null_vram, 0, sizeof null_vram);
    return BFM_PLAT_OK;
}
static int null_output(void *self, const BfmPlatOutput *o) {
    (void)self; (void)o;
    return BFM_PLAT_OK;
}
static int null_draw_env(void *self, const BfmPlatDrawEnv *e) {
    (void)self; (void)e;
    return BFM_PLAT_OK;
}
static int null_disp_env(void *self, const BfmPlatDispEnv *e) {
    (void)self; (void)e;
    return BFM_PLAT_OK;
}

static const BfmPlatRendererBackend null_backend = {
    "null", null_init, NULL, null_output, null_ok, null_draw_env,
    null_disp_env, null_submit, null_upload, NULL, null_download, null_ok,
    NULL, NULL
};

static const BfmPlatRendererBackend *backends[MAX_BACKENDS] = {&null_backend};
static unsigned backend_count = 1;
static const BfmPlatRendererBackend *active;
static BfmPlatRendererStats stats;
static BfmPlatDispEnv last_disp = {{0, 0, 320, 240}, {0, 0, 256, 240}, 0, 0};

void bfm_plat_renderer_last_disp_env(BfmPlatDispEnv *out) {
    if (out) *out = last_disp;
}

const uint16_t *bfm_plat_renderer_null_vram(void) { return null_vram; }

int bfm_plat_rect_valid(const BfmPlatRect *r) {
    return r && r->w > 0 && r->h > 0 && r->x >= 0 && r->y >= 0 &&
           r->x + r->w <= VRAM_W && r->y + r->h <= VRAM_H;
}

int bfm_plat_renderer_register(const BfmPlatRendererBackend *b) {
    unsigned i;
    if (!b || !b->name || !*b->name) return BFM_PLAT_INVALID;
    for (i = 0; i < backend_count; i++)
        if (strcmp(backends[i]->name, b->name) == 0) return BFM_PLAT_INVALID;
    if (backend_count >= MAX_BACKENDS) return BFM_PLAT_NO_SPACE;
    backends[backend_count++] = b;
    return BFM_PLAT_OK;
}

const BfmPlatRendererBackend *bfm_plat_renderer_find(const char *name) {
    unsigned i;
    if (!name) return NULL;
    for (i = 0; i < backend_count; i++)
        if (strcmp(backends[i]->name, name) == 0) return backends[i];
    return NULL;
}

int bfm_plat_renderer_open(const char *name, const BfmPlatOutput *out,
                           const char **selected) {
    const BfmPlatRendererBackend *b = bfm_plat_renderer_find(name);
    int r;
    bfm_plat_renderer_close();
    if (!b) b = &null_backend;
    r = b->init ? b->init(b->self, out) : BFM_PLAT_OK;
    if (r != BFM_PLAT_OK && b != &null_backend) {
        b = &null_backend;
        r = b->init(b->self, out);
    }
    active = b;
    if (selected) *selected = b->name;
    return r;
}

void bfm_plat_renderer_close(void) {
    if (active && active->shutdown) active->shutdown(active->self);
    active = NULL;
}

const char *bfm_plat_renderer_active(void) {
    return active ? active->name : NULL;
}

#define REQUIRE_ACTIVE() do { if (!active) return BFM_PLAT_NOT_READY; } while (0)

int bfm_plat_renderer_set_output(const BfmPlatOutput *o) {
    REQUIRE_ACTIVE();
    if (!o) return BFM_PLAT_INVALID;
    return active->set_output ? active->set_output(active->self, o)
                              : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_renderer_begin_frame(void) {
    REQUIRE_ACTIVE();
    return active->begin_frame ? active->begin_frame(active->self) : BFM_PLAT_OK;
}

int bfm_plat_renderer_set_draw_env(const BfmPlatDrawEnv *e) {
    REQUIRE_ACTIVE();
    if (!e) return BFM_PLAT_INVALID;
    stats.draw_envs++;
    return active->set_draw_env ? active->set_draw_env(active->self, e)
                                : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_renderer_set_disp_env(const BfmPlatDispEnv *e) {
    REQUIRE_ACTIVE();
    if (!e) return BFM_PLAT_INVALID;
    stats.disp_envs++;
    last_disp = *e;
    return active->set_disp_env ? active->set_disp_env(active->self, e)
                                : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_renderer_submit(const BfmPlatPrim *p, size_t n) {
    size_t i;
    REQUIRE_ACTIVE();
    if (n && !p) return BFM_PLAT_INVALID;
    for (i = 0; i < n; i++)
        if (p[i].kind == 0 || p[i].kind >= BFM_PRIM_KIND_COUNT)
            return BFM_PLAT_INVALID;
    for (i = 0; i < n; i++) stats.prims_by_kind[p[i].kind]++;
    stats.prims += n;
    return active->submit ? active->submit(active->self, p, n)
                          : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_renderer_upload_vram(const BfmPlatRect *r, const uint16_t *px) {
    BfmPlatImage img;
    REQUIRE_ACTIVE();
    if (!bfm_plat_rect_valid(r) || !px) return BFM_PLAT_INVALID;
    stats.uploads++;
    bfm_plat_mods_observe_texture(r, px);
    if (bfm_plat_mods_replace_texture(r, px, &img) == BFM_PLAT_OK) {
        if (active->upload_replacement) {
            stats.replaced_uploads++;
            return active->upload_replacement(active->self, r, px, &img);
        }
        /* No HD path: a same-size replacement is folded back into 15-bit
         * VRAM so PS1-accurate backends still show it. Larger ones need a
         * backend with upload_replacement. */
        if (img.width == (uint32_t)r->w && img.height == (uint32_t)r->h &&
            active->upload_vram) {
            size_t i, n = (size_t)r->w * (size_t)r->h;
            uint16_t *conv = (uint16_t *)malloc(n * 2u);
            if (conv) {
                int res;
                for (i = 0; i < n; i++) {
                    const uint8_t *c = img.rgba + i * 4u;
                    conv[i] = c[3] == 0 ? 0 : (uint16_t)(
                        (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10) |
                        (((c[0] | c[1] | c[2]) >> 3) == 0 ? 0x8000u : 0u));
                }
                res = active->upload_vram(active->self, r, conv);
                free(conv);
                stats.replaced_uploads++;
                return res;
            }
        }
    }
    return active->upload_vram ? active->upload_vram(active->self, r, px)
                               : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_renderer_download_vram(const BfmPlatRect *r, uint16_t *px) {
    REQUIRE_ACTIVE();
    if (!bfm_plat_rect_valid(r) || !px) return BFM_PLAT_INVALID;
    stats.downloads++;
    return active->download_vram ? active->download_vram(active->self, r, px)
                                 : BFM_PLAT_UNSUPPORTED;
}

int bfm_plat_renderer_present(void) {
    int r;
    REQUIRE_ACTIVE();
    bfm_plat_mods_emit(BFM_EVENT_BEFORE_PRESENT, NULL);
    r = active->present ? active->present(active->self) : BFM_PLAT_UNSUPPORTED;
    stats.frames++;
    bfm_plat_mods_emit(BFM_EVENT_AFTER_PRESENT, NULL);
    return r;
}

int bfm_plat_renderer_overlay_text(int x, int y, const char *text) {
    REQUIRE_ACTIVE();
    if (!text) return BFM_PLAT_INVALID;
    return active->overlay_text ? active->overlay_text(active->self, x, y, text)
                                : BFM_PLAT_OK;
}

void bfm_plat_renderer_stats(BfmPlatRendererStats *out) {
    if (out) *out = stats;
}

void bfm_plat_renderer_reset(void) {
    bfm_plat_renderer_close();
    backend_count = 1;
    memset(&stats, 0, sizeof stats);
    last_disp.disp.x = last_disp.disp.y = 0;
    last_disp.disp.w = 320;
    last_disp.disp.h = 240;
    last_disp.rgb24 = 0;
}
