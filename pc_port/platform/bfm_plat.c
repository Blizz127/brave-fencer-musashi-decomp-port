#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "bfm_plat.h"

#include "bfm_plat_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef BFM_PLAT_WITH_PNG
int bfm_plat_texture_png_register(void);
#endif
#ifdef BFM_PLAT_WITH_LUA
int bfm_plat_script_lua_register(void);
#endif

static BfmPlatConfig cfg_copy;
static BfmPlatDiscCheck disc_check;

const BfmPlatDiscCheck *bfm_plat_disc_last_check(void) { return &disc_check; }

static int open_disc(void) {
    char msg[600];
    memset(&disc_check, 0, sizeof disc_check);
    disc_check.status = BFM_DISC_NO_PATH;
    if (!cfg_copy.disc_path[0] && cfg_copy.disc_search[0]) {
        char found[BFM_PLAT_PATH_MAX];
        if (bfm_plat_disc_autodetect(cfg_copy.disc_search, found, sizeof found) == BFM_PLAT_OK)
            snprintf(cfg_copy.disc_path, sizeof cfg_copy.disc_path, "%s", found);
    }
    if (!cfg_copy.disc_path[0]) return BFM_PLAT_OK;     /* no disc: headless/tools */
    if (cfg_copy.disc_validate) {
        if (bfm_plat_disc_validate(cfg_copy.disc_path, &disc_check) != BFM_PLAT_OK) {
            bfm_plat_disc_check_describe(&disc_check, msg, sizeof msg);
            bfm_plat_mods_log("disc", 0, msg);
            return BFM_PLAT_INVALID;
        }
        if (strcmp(cfg_copy.storage, "image") == 0) return BFM_PLAT_OK;  /* stays open */
        bfm_plat_disc_close();
    }
    return bfm_plat_disc_open(cfg_copy.storage, cfg_copy.disc_path);
}
static int up;
static int mods_loaded;
static uint64_t frames;

const char *bfm_plat_result_name(int r) {
    switch (r) {
    case BFM_PLAT_OK: return "ok";
    case BFM_PLAT_ERROR: return "error";
    case BFM_PLAT_UNSUPPORTED: return "unsupported";
    case BFM_PLAT_INVALID: return "invalid";
    case BFM_PLAT_NOT_FOUND: return "not found";
    case BFM_PLAT_NO_SPACE: return "no space";
    case BFM_PLAT_NOT_READY: return "not ready";
    }
    return "unknown";
}

static int cmd_pause(void *, int, const char *const *, char *, size_t);
static int cmd_screenshot(void *, int, const char *const *, char *, size_t);
static int cmd_quit(void *, int, const char *const *, char *, size_t);
static int paused, quit_requested, screenshot_pending;

static const char *config_lookup(const char *key) {
    return bfm_plat_config_get(&cfg_copy, key);
}

static void output_from_config(BfmPlatOutput *o) {
    memset(o, 0, sizeof *o);
    o->window_width = cfg_copy.window_width;
    o->window_height = cfg_copy.window_height;
    o->internal_scale = cfg_copy.internal_scale;
    bfm_plat_config_aspect(&cfg_copy, &o->aspect_num, &o->aspect_den);
    o->frame_rate = cfg_copy.frame_rate;
    o->fullscreen = cfg_copy.fullscreen;
    o->vsync = cfg_copy.vsync;
}

int bfm_plat_init(const BfmPlatConfig *cfg) {
    BfmPlatOutput out;
    int r;
    if (up) bfm_plat_shutdown();
    if (cfg) cfg_copy = *cfg;
    else bfm_plat_config_defaults(&cfg_copy);
    bfm_plat_register_builtin_backends();
    output_from_config(&out);
    r = bfm_plat_renderer_open(cfg_copy.renderer, &out, NULL);
    if (r != BFM_PLAT_OK) return r;
    if (bfm_plat_renderer_set_output(&out) == BFM_PLAT_UNSUPPORTED &&
        cfg_copy.widescreen_mode == BFM_WIDESCREEN_ANAMORPHIC && cfg_copy.widescreen) {
        bfm_plat_mods_log("port", 1, "renderer cannot stretch: anamorphic widescreen falls back to hor+");
        cfg_copy.widescreen_mode = BFM_WIDESCREEN_HOR_PLUS;
    }
    r = bfm_plat_audio_open(cfg_copy.audio, NULL);
    if (r != BFM_PLAT_OK) return r;
    r = bfm_plat_input_open(cfg_copy.input, NULL);
    if (r != BFM_PLAT_OK) return r;
    bfm_plat_timing_init();
    bfm_plat_timing_set_fast_forward_speed(cfg_copy.fast_forward_speed);
    bfm_plat_memcard_set_dir(cfg_copy.save_dir);
    r = open_disc();
    if (r != BFM_PLAT_OK) return r;
    bfm_plat_mods_set_config_getter(config_lookup);
#ifdef BFM_PLAT_WITH_PNG
    bfm_plat_texture_png_register();
#endif
#ifdef BFM_PLAT_WITH_LUA
    bfm_plat_script_lua_register();
#endif
    bfm_plat_watch_reset();
    if (cfg_copy.derived_events) bfm_plat_derived_events_enable();
    bfm_plat_console_init(&cfg_copy);
    bfm_plat_events_register_console();
    bfm_plat_console_register("pause", "pause - toggle pause", cmd_pause, NULL);
    bfm_plat_console_register("screenshot", "screenshot [PATH.png] - save the display area",
                              cmd_screenshot, NULL);
    bfm_plat_console_register("quit", "quit - request exit", cmd_quit, NULL);
    paused = quit_requested = screenshot_pending = 0;
    mods_loaded = 0;
    if (cfg_copy.mods_enabled) {
        BfmPlatModsOptions mo;
        memset(&mo, 0, sizeof mo);
        mo.dirs = cfg_copy.mod_dirs;
        mo.allow_plugins = 1;
        mo.dump_textures = cfg_copy.dump_textures;
        mo.dump_dir = cfg_copy.dump_dir;
        r = bfm_plat_mods_init(&mo);
        mods_loaded = r > 0 ? r : 0;
    }
    frames = 0;
    up = 1;
    return BFM_PLAT_OK;
}

void bfm_plat_shutdown(void) {
    bfm_plat_mods_shutdown();
    bfm_plat_console_shutdown();
    bfm_plat_console_ui_open(0);
    bfm_plat_watch_reset();
    bfm_plat_disc_close();
    bfm_plat_input_close();
    bfm_plat_audio_close();
    bfm_plat_renderer_close();
    bfm_plat_mods_set_config_getter(NULL);
    up = 0;
}

BfmPlatConfig *bfm_plat_config(void) { return &cfg_copy; }

static unsigned screenshot_index;
static char last_screenshot[BFM_PLAT_PATH_MAX + 32];

static void set_paused(int p) {
    p = p != 0;
    if (p == paused) return;
    paused = p;
    bfm_plat_audio_pause(paused);
}

void bfm_plat_pause(int p) { set_paused(p); }
int bfm_plat_is_paused(void) { return paused; }
int bfm_plat_quit_requested(void) { return quit_requested; }
void bfm_plat_request_quit(void) { quit_requested = 1; }
void bfm_plat_request_screenshot(void) { screenshot_pending = 1; }
const char *bfm_plat_last_screenshot(void) {
    return last_screenshot[0] ? last_screenshot : NULL;
}

static void make_dir(const char *path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

int bfm_plat_screenshot(const char *path, char *out_path, size_t out_size) {
    BfmPlatDispEnv d;
    BfmPlatRect r;
    uint16_t *vram;
    uint8_t *rgb;
    char auto_path[BFM_PLAT_PATH_MAX + 32];
    int res;
    size_t y, w, h;
    bfm_plat_renderer_last_disp_env(&d);
    w = d.disp.w > 0 ? (size_t)d.disp.w : 320u;
    h = d.disp.h > 0 ? (size_t)d.disp.h : 240u;
    r.x = d.disp.x;
    r.y = d.disp.y;
    r.w = (int16_t)(d.rgb24 ? (w * 3u + 1u) / 2u : w);
    r.h = (int16_t)h;
    if (!bfm_plat_rect_valid(&r)) return BFM_PLAT_INVALID;
    if (!path) {
        FILE *probe;
        make_dir(cfg_copy.screenshot_dir);
        for (;;) {
            snprintf(auto_path, sizeof auto_path, "%s/bfm_%05u.png",
                     cfg_copy.screenshot_dir, screenshot_index++);
            probe = fopen(auto_path, "rb");
            if (!probe) break;
            fclose(probe);
            if (screenshot_index > 99999u) return BFM_PLAT_NO_SPACE;
        }
        path = auto_path;
    }
    vram = (uint16_t *)malloc((size_t)r.w * (size_t)r.h * 2u);
    rgb = (uint8_t *)malloc(w * h * 3u);
    if (!vram || !rgb) { free(vram); free(rgb); return BFM_PLAT_ERROR; }
    res = bfm_plat_renderer_download_vram(&r, vram);
    if (res == BFM_PLAT_OK) {
        if (d.rgb24)
            for (y = 0; y < h; y++)
                bfm_plat_vram24_to_rgb(vram + y * (size_t)r.w, w, rgb + y * w * 3u);
        else
            bfm_plat_vram15_to_rgb(vram, w * h, rgb);
        res = bfm_plat_png_write(path, (uint32_t)w, (uint32_t)h, 3, rgb);
    }
    free(vram);
    free(rgb);
    if (res == BFM_PLAT_OK) {
        snprintf(last_screenshot, sizeof last_screenshot, "%s", path);
        if (out_path && out_size) snprintf(out_path, out_size, "%s", path);
    }
    return res;
}

static void handle_hotkeys(void) {
    uint32_t held = bfm_plat_input_hotkeys();
    uint32_t pressed = bfm_plat_input_hotkeys_pressed();
    if (cfg_copy.fast_forward_toggle) {
        if (pressed & BFM_HOTKEY_FAST_FORWARD)
            bfm_plat_timing_set_fast_forward(!bfm_plat_timing_fast_forward());
    } else {
        bfm_plat_timing_set_fast_forward((held & BFM_HOTKEY_FAST_FORWARD) != 0);
    }
    if (pressed & BFM_HOTKEY_CHEAT_MENU)
        bfm_plat_cheat_menu_open(!bfm_plat_cheat_menu_is_open());
    if ((pressed & BFM_HOTKEY_CONSOLE) && cfg_copy.console)
        bfm_plat_console_ui_toggle();
    if (pressed & BFM_HOTKEY_PAUSE) set_paused(!paused);
    if (pressed & BFM_HOTKEY_SCREENSHOT) screenshot_pending = 1;
    if (pressed & BFM_HOTKEY_QUIT) quit_requested = 1;
}

int bfm_plat_frame_begin(void) {
    int r;
    if (!up) return BFM_PLAT_NOT_READY;
    r = bfm_plat_input_poll();
    if (r != BFM_PLAT_OK) return r;
    handle_hotkeys();
    if (bfm_plat_console_ui_is_open()) bfm_plat_console_ui_pump();
    bfm_plat_renderer_begin_frame();
    if (paused) return BFM_PLAT_PAUSED;
    bfm_plat_mods_emit(BFM_EVENT_FRAME_BEGIN, NULL);
    return BFM_PLAT_OK;
}

int bfm_plat_frame_wait(void) {
    int r = bfm_plat_frame_begin();
    while (r == BFM_PLAT_PAUSED && !quit_requested) {
        bfm_plat_frame_end(1);
        r = bfm_plat_frame_begin();
    }
    return r == BFM_PLAT_PAUSED ? BFM_PLAT_OK : r;
}

static void draw_overlays(void) {
    if (paused) bfm_plat_renderer_overlay_text(8, 8, "PAUSED");
    bfm_plat_cheat_menu_draw();
    bfm_plat_console_ui_draw();
}

static void take_pending_screenshot(void) {
    char path[BFM_PLAT_PATH_MAX + 32];
    char msg[BFM_PLAT_PATH_MAX + 64];
    int r;
    if (!screenshot_pending) return;
    screenshot_pending = 0;
    /* Before overlays are drawn, so the image is the game's frame only. */
    r = bfm_plat_screenshot(NULL, path, sizeof path);
    if (r == BFM_PLAT_OK) snprintf(msg, sizeof msg, "screenshot %s", path);
    else snprintf(msg, sizeof msg, "screenshot failed: %s", bfm_plat_result_name(r));
    bfm_plat_console_ui_print(msg);
    bfm_plat_mods_log("port", 2, msg);
}

int bfm_plat_frame_hook_end(void) {
    if (!up) return BFM_PLAT_NOT_READY;
    if (cfg_copy.watch_poll && !paused) bfm_plat_watch_poll();
    take_pending_screenshot();
    if (!paused) bfm_plat_mods_emit(BFM_EVENT_FRAME_END, NULL);
    draw_overlays();
    frames++;
    return BFM_PLAT_OK;
}

int bfm_plat_frame_end(unsigned fields) {
    int r;
    if (!up) return BFM_PLAT_NOT_READY;
    if (cfg_copy.watch_poll && !paused) bfm_plat_watch_poll();
    take_pending_screenshot();
    if (!paused) bfm_plat_mods_emit(BFM_EVENT_FRAME_END, NULL);
    draw_overlays();
    r = bfm_plat_renderer_present();
    bfm_plat_timing_vsync(fields ? fields : 1);
    frames++;
    return r;
}

static int cmd_pause(void *u, int argc, const char *const *argv, char *out,
                     size_t n) {
    (void)u; (void)argc; (void)argv;
    set_paused(!paused);
    snprintf(out, n, "%s", paused ? "paused" : "running");
    return 0;
}

static int cmd_screenshot(void *u, int argc, const char *const *argv,
                          char *out, size_t n) {
    char path[BFM_PLAT_PATH_MAX + 32];
    int r;
    (void)u;
    if (argc > 2) { snprintf(out, n, "usage: screenshot [PATH.png]"); return 1; }
    r = bfm_plat_screenshot(argc == 2 ? argv[1] : NULL, path, sizeof path);
    if (r != BFM_PLAT_OK) { snprintf(out, n, "screenshot failed: %s", bfm_plat_result_name(r)); return 1; }
    snprintf(out, n, "screenshot %s", path);
    return 0;
}

static int cmd_quit(void *u, int argc, const char *const *argv, char *out,
                    size_t n) {
    (void)u; (void)argc; (void)argv;
    quit_requested = 1;
    snprintf(out, n, "quit requested");
    return 0;
}

void bfm_plat_status(BfmPlatStatus *s) {
    if (!s) return;
    s->renderer = bfm_plat_renderer_active();
    s->audio = bfm_plat_audio_active();
    s->input = bfm_plat_input_active();
    s->storage = bfm_plat_disc_backend();
    s->mods_loaded = mods_loaded;
    s->frames = frames;
}

void bfm_plat_event_room_enter(uint32_t area, uint32_t room) {
    BfmEventRoom e;
    e.area = area;
    e.room = room;
    e.location = -1;
    bfm_plat_mods_emit(BFM_EVENT_ROOM_ENTER, &e);
}

void bfm_plat_event_room_exit(uint32_t area, uint32_t room) {
    BfmEventRoom e;
    e.area = area;
    e.room = room;
    e.location = -1;
    bfm_plat_mods_emit(BFM_EVENT_ROOM_EXIT, &e);
}

void bfm_plat_event_battle(int start, uint32_t encounter, int32_t result) {
    BfmEventBattle e;
    e.encounter = encounter;
    e.result = start ? 0 : result;
    bfm_plat_mods_emit(start ? BFM_EVENT_BATTLE_START : BFM_EVENT_BATTLE_END, &e);
}

void bfm_plat_event_item_get(uint32_t item, int32_t count) {
    BfmEventItem e;
    e.item = item;
    e.count = count;
    bfm_plat_mods_emit(BFM_EVENT_ITEM_GET, &e);
}

void bfm_plat_event_save(int load, uint32_t slot) {
    BfmEventSave e;
    e.slot = slot;
    bfm_plat_mods_emit(load ? BFM_EVENT_LOAD : BFM_EVENT_SAVE, &e);
}
