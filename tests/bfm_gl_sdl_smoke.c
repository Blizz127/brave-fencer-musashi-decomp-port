/* End-to-end smoke test of the `gl` renderer backend through SDL2 and the
 * bfm_plat renderer interface. Needs a GL 3.3 context: a display, or SDL's
 * offscreen driver with EGL (SDL_VIDEODRIVER=offscreen). Prints "skip" and
 * exits 0 when the backend cannot open (it falls back to "null"). Synthetic
 * data only. */

#include "bfm_plat.h"
#include "bfm_plat_renderer.h"

#include <stdio.h>
#include <string.h>

int bfm_plat_backend_gl_register(void);

static int fails;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);               \
            fails++;                                                          \
        }                                                                     \
    } while (0)

static BfmPlatRect R(int x, int y, int w, int h) {
    BfmPlatRect r;
    r.x = (int16_t)x; r.y = (int16_t)y; r.w = (int16_t)w; r.h = (int16_t)h;
    return r;
}

int main(void) {
    static uint16_t tex[16 * 16], out[320 * 4];
    static uint8_t hd[32 * 32 * 4];
    BfmPlatOutput o;
    BfmPlatDispEnv de;
    BfmPlatDrawEnv env;
    BfmPlatPrim p[2];
    BfmPlatRect r;
    BfmPlatImage img;
    const BfmPlatRendererBackend *gl;
    const char *sel = NULL;
    int i, frame;

    memset(&o, 0, sizeof o);
    o.window_width = 640; o.window_height = 480; o.internal_scale = 2;
    o.aspect_num = 16; o.aspect_den = 9; o.vsync = 0;
    CHECK(bfm_plat_backend_gl_register() == BFM_PLAT_OK);
    bfm_plat_renderer_open("gl", &o, &sel);
    if (!sel || strcmp(sel, "gl") != 0) {
        printf("skip: gl backend could not open (selected %s)\n", sel ? sel : "none");
        return 0;
    }
    memset(&de, 0, sizeof de);
    de.disp = R(0, 0, 320, 240);
    CHECK(bfm_plat_renderer_set_disp_env(&de) == BFM_PLAT_OK);

    /* 15-bit texture at (640, 0) */
    for (i = 0; i < 256; i++) tex[i] = (uint16_t)(0x001F | ((i & 1) ? 0x03E0 : 0));
    r = R(640, 0, 16, 16);
    CHECK(bfm_plat_renderer_upload_vram(&r, tex) == BFM_PLAT_OK);

    for (frame = 0; frame < 3; frame++) {
        memset(&env, 0, sizeof env);
        env.clip = R(0, (frame & 1) * 240, 320, 240);
        env.offset_y = (int16_t)((frame & 1) * 240);
        env.clear_bg = 1;
        env.bg_b = 255;
        CHECK(bfm_plat_renderer_set_draw_env(&env) == BFM_PLAT_OK);
        memset(p, 0, sizeof p);
        p[0].kind = BFM_PRIM_SPRITE;
        p[0].flags = BFM_PRIM_FLAG_RAW_TEXTURE;
        p[0].tpage = (uint16_t)((2 << 7) | (640 >> 6));
        p[0].v[0].x = 10; p[0].v[0].y = 20;
        p[0].w = 16; p[0].h = 16;
        p[1].kind = BFM_PRIM_TILE;
        p[1].v[0].x = -30; p[1].v[0].y = 100; p[1].w = 10; p[1].h = 4;
        p[1].v[0].g = 255;
        CHECK(bfm_plat_renderer_submit(p, 2) == BFM_PLAT_OK);
        CHECK(bfm_plat_renderer_overlay_text(4, 4, "GL") == BFM_PLAT_OK);
        de.disp = R(0, (frame & 1) * 240, 320, 240);
        CHECK(bfm_plat_renderer_set_disp_env(&de) == BFM_PLAT_OK);
        CHECK(bfm_plat_renderer_present() == BFM_PLAT_OK);
    }
    /* StoreImage of the last frame buffer resolves drawn pixels */
    r = R(0, 20, 320, 1);
    CHECK(bfm_plat_renderer_download_vram(&r, out) == BFM_PLAT_OK);
    CHECK(out[10] == 0x001F && out[11] == 0x03FF && out[25] == 0x03FF && out[26] == 0x7C00);
    printf("frame buffer row: %04x %04x .. %04x %04x\n", out[10], out[11], out[25], out[26]);

    /* HD replacement through the backend's upload_replacement */
    gl = bfm_plat_renderer_find("gl");
    for (i = 0; i < 32 * 32; i++) {
        hd[i * 4 + 0] = 0; hd[i * 4 + 1] = 0; hd[i * 4 + 2] = 255; hd[i * 4 + 3] = 255;
    }
    img.width = 32; img.height = 32; img.rgba = hd;
    r = R(640, 0, 16, 16);
    CHECK(gl->upload_replacement(gl->self, &r, tex, &img) == BFM_PLAT_OK);
    memset(&env, 0, sizeof env);
    env.clip = R(0, 0, 320, 240);
    CHECK(bfm_plat_renderer_set_draw_env(&env) == BFM_PLAT_OK);
    p[0].v[0].y = 20;
    CHECK(bfm_plat_renderer_submit(p, 1) == BFM_PLAT_OK);
    r = R(0, 20, 320, 1);
    CHECK(bfm_plat_renderer_download_vram(&r, out) == BFM_PLAT_OK);
    CHECK(out[10] == 0x7C00 && out[25] == 0x7C00);   /* blue from the HD image */
    /* the original texels are still in VRAM for StoreImage */
    r = R(640, 0, 16, 1);
    CHECK(bfm_plat_renderer_download_vram(&r, out) == BFM_PLAT_OK && out[1] == tex[1]);
    CHECK(bfm_plat_renderer_present() == BFM_PLAT_OK);

    /* 24-bit display (movie frames) */
    for (i = 0; i < 256; i++) tex[i] = 0x8040;
    r = R(0, 0, 16, 16);
    CHECK(bfm_plat_renderer_upload_vram(&r, tex) == BFM_PLAT_OK);
    de.disp = R(0, 0, 320, 240);
    de.rgb24 = 1;
    CHECK(bfm_plat_renderer_set_disp_env(&de) == BFM_PLAT_OK);
    CHECK(bfm_plat_renderer_present() == BFM_PLAT_OK);
    de.rgb24 = 0;
    CHECK(bfm_plat_renderer_set_disp_env(&de) == BFM_PLAT_OK);

    /* internal scale change keeps uploaded VRAM content */
    o.internal_scale = 4;
    CHECK(bfm_plat_renderer_set_output(&o) == BFM_PLAT_OK);
    CHECK(bfm_plat_renderer_present() == BFM_PLAT_OK);
    bfm_plat_renderer_close();
    if (fails) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("ok gl_sdl\n");
    return 0;
}
