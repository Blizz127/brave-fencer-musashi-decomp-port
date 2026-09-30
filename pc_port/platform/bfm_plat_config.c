#include "bfm_plat_config.h"
#include "bfm_plat_ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_str(char *dst, size_t size, const char *src) {
    size_t n = strlen(src);
    if (n >= size) n = size - 1u;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void bfm_plat_config_defaults(BfmPlatConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof *cfg);
    cfg->widescreen = 0;
    cfg->aspect_num = 4;
    cfg->aspect_den = 3;
    cfg->widescreen_mode = BFM_WIDESCREEN_HOR_PLUS;
    cfg->internal_scale = 1;
    cfg->window_width = 960;
    cfg->window_height = 720;
    cfg->fullscreen = 0;
    cfg->frame_rate = 30;
    cfg->vsync = 1;
    cfg->dither = BFM_DITHER_AUTO;
    cfg->fast_forward_speed = 4.0;
    cfg->fast_forward_toggle = 0;
    copy_str(cfg->renderer, sizeof cfg->renderer, "psycross");
    copy_str(cfg->audio, sizeof cfg->audio, "sdl");
    copy_str(cfg->input, sizeof cfg->input, "host");
    copy_str(cfg->storage, sizeof cfg->storage, "image");
    copy_str(cfg->save_dir, sizeof cfg->save_dir, "saves");
    copy_str(cfg->disc_search, sizeof cfg->disc_search, "disc");
    cfg->disc_validate = 1;
    copy_str(cfg->screenshot_dir, sizeof cfg->screenshot_dir, "screenshots");
    cfg->mods_enabled = 1;
    copy_str(cfg->mod_dirs, sizeof cfg->mod_dirs, "mods");
    copy_str(cfg->dump_dir, sizeof cfg->dump_dir, "dump");
    cfg->watch_poll = 1;
    cfg->derived_events = 1;
    cfg->console = 1;
    cfg->log_level = 2;
}

int bfm_plat_config_default_path(char *out, size_t size) {
    const char *env = getenv("BFM_PORT_CONFIG");
    int n;
    if (!out || size == 0) return BFM_PLAT_INVALID;
    if (env && *env) {
        n = snprintf(out, size, "%s", env);
    } else {
#ifdef _WIN32
        const char *appdata = getenv("APPDATA");
        if (!appdata || !*appdata) return BFM_PLAT_NOT_FOUND;
        n = snprintf(out, size, "%s\\bfm-port\\config.ini", appdata);
#else
        const char *xdg = getenv("XDG_CONFIG_HOME");
        const char *home = getenv("HOME");
        if (xdg && *xdg)
            n = snprintf(out, size, "%s/bfm-port/config.ini", xdg);
        else if (home && *home)
            n = snprintf(out, size, "%s/.config/bfm-port/config.ini", home);
        else
            return BFM_PLAT_NOT_FOUND;
#endif
    }
    return (n < 0 || (size_t)n >= size) ? BFM_PLAT_NO_SPACE : BFM_PLAT_OK;
}

static void record(BfmPlatConfig *cfg, const char *key, const char *value) {
    unsigned i;
    for (i = 0; i < cfg->entry_count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            copy_str(cfg->entries[i].value, sizeof cfg->entries[i].value,
                     value);
            return;
        }
    }
    if (cfg->entry_count >= BFM_PLAT_CONFIG_EXTRA_MAX) return;
    copy_str(cfg->entries[cfg->entry_count].key,
             sizeof cfg->entries[0].key, key);
    copy_str(cfg->entries[cfg->entry_count].value,
             sizeof cfg->entries[0].value, value);
    cfg->entry_count++;
}

static int set_bool(int *field, const char *value) {
    int v;
    if (!bfm_plat_ini_bool(value, &v)) return BFM_PLAT_INVALID;
    *field = v;
    return BFM_PLAT_OK;
}

static int set_int(int *field, const char *value, long lo, long hi) {
    long v;
    if (!bfm_plat_ini_long(value, &v) || v < lo || v > hi)
        return BFM_PLAT_INVALID;
    *field = (int)v;
    return BFM_PLAT_OK;
}

static int set_name(char *field, size_t size, const char *value) {
    size_t i, n = strlen(value);
    if (n == 0 || n >= size) return BFM_PLAT_INVALID;
    for (i = 0; i < n; i++) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-'))
            return BFM_PLAT_INVALID;
    }
    copy_str(field, size, value);
    return BFM_PLAT_OK;
}

static int set_path(char *field, size_t size, const char *value) {
    if (strlen(value) >= size) return BFM_PLAT_INVALID;
    copy_str(field, size, value);
    return BFM_PLAT_OK;
}

int bfm_plat_config_set(BfmPlatConfig *cfg, const char *key,
                        const char *value) {
    int r = BFM_PLAT_OK;
    if (!cfg || !key || !value || !strchr(key, '.') || strlen(key) >= 64)
        return BFM_PLAT_INVALID;
    record(cfg, key, value);
    if (strcmp(key, "video.widescreen") == 0) {
        int w;
        if (bfm_plat_ini_bool(value, &w)) {
            cfg->aspect_num = w ? 16 : 4;
            cfg->aspect_den = w ? 9 : 3;
            cfg->widescreen = w;
        } else r = BFM_PLAT_INVALID;
    } else if (strcmp(key, "video.aspect") == 0) {
        if (strcmp(value, "4:3") == 0) { cfg->aspect_num = 4; cfg->aspect_den = 3; }
        else if (strcmp(value, "16:9") == 0) { cfg->aspect_num = 16; cfg->aspect_den = 9; }
        else if (strcmp(value, "16:10") == 0) { cfg->aspect_num = 16; cfg->aspect_den = 10; }
        else r = BFM_PLAT_INVALID;
        cfg->widescreen = !(cfg->aspect_num == 4 && cfg->aspect_den == 3);
    } else if (strcmp(key, "video.widescreen_mode") == 0) {
        if (strcmp(value, "hor+") == 0) cfg->widescreen_mode = BFM_WIDESCREEN_HOR_PLUS;
        else if (strcmp(value, "anamorphic") == 0) cfg->widescreen_mode = BFM_WIDESCREEN_ANAMORPHIC;
        else r = BFM_PLAT_INVALID;
    }
    else if (strcmp(key, "video.internal_scale") == 0) r = set_int(&cfg->internal_scale, value, 1, 8);
    else if (strcmp(key, "video.window_width") == 0) r = set_int(&cfg->window_width, value, 320, 16384);
    else if (strcmp(key, "video.window_height") == 0) r = set_int(&cfg->window_height, value, 240, 16384);
    else if (strcmp(key, "video.fullscreen") == 0) r = set_bool(&cfg->fullscreen, value);
    else if (strcmp(key, "video.vsync") == 0) r = set_bool(&cfg->vsync, value);
    else if (strcmp(key, "video.gpu") == 0) {
        if (strcmp(value, "cpu") == 0) cfg->gpu_path = BFM_GPU_CPU;
        else if (strcmp(value, "bfm_plat") == 0) cfg->gpu_path = BFM_GPU_BFM_PLAT;
        else r = BFM_PLAT_INVALID;
    } else if (strcmp(key, "video.dither") == 0) {
        int on;
        if (strcmp(value, "auto") == 0) cfg->dither = BFM_DITHER_AUTO;
        else if (bfm_plat_ini_bool(value, &on)) cfg->dither = on ? BFM_DITHER_ON : BFM_DITHER_OFF;
        else r = BFM_PLAT_INVALID;
    }
    else if (strcmp(key, "video.screenshot_dir") == 0) r = set_path(cfg->screenshot_dir, sizeof cfg->screenshot_dir, value);
    else if (strcmp(key, "video.frame_rate") == 0) {
        long v;
        if (bfm_plat_ini_long(value, &v) && (v == 30 || v == 60)) cfg->frame_rate = (int)v;
        else r = BFM_PLAT_INVALID;
    } else if (strcmp(key, "timing.fast_forward_speed") == 0) {
        double v;
        if (bfm_plat_ini_double(value, &v) && v >= 0.0 && v <= 64.0) cfg->fast_forward_speed = v;
        else r = BFM_PLAT_INVALID;
    } else if (strcmp(key, "timing.fast_forward_toggle") == 0) r = set_bool(&cfg->fast_forward_toggle, value);
    else if (strcmp(key, "backends.renderer") == 0) r = set_name(cfg->renderer, sizeof cfg->renderer, value);
    else if (strcmp(key, "backends.audio") == 0) r = set_name(cfg->audio, sizeof cfg->audio, value);
    else if (strcmp(key, "backends.input") == 0) r = set_name(cfg->input, sizeof cfg->input, value);
    else if (strcmp(key, "backends.storage") == 0) r = set_name(cfg->storage, sizeof cfg->storage, value);
    else if (strcmp(key, "disc.path") == 0) r = set_path(cfg->disc_path, sizeof cfg->disc_path, value);
    else if (strcmp(key, "disc.search") == 0) r = set_path(cfg->disc_search, sizeof cfg->disc_search, value);
    else if (strcmp(key, "disc.validate") == 0) r = set_bool(&cfg->disc_validate, value);
    else if (strcmp(key, "storage.save_dir") == 0) r = set_path(cfg->save_dir, sizeof cfg->save_dir, value);
    else if (strcmp(key, "mods.enabled") == 0) r = set_bool(&cfg->mods_enabled, value);
    else if (strcmp(key, "mods.dirs") == 0) r = set_path(cfg->mod_dirs, sizeof cfg->mod_dirs, value);
    else if (strcmp(key, "mods.dump_textures") == 0) r = set_bool(&cfg->dump_textures, value);
    else if (strcmp(key, "mods.dump_dir") == 0) r = set_path(cfg->dump_dir, sizeof cfg->dump_dir, value);
    else if (strcmp(key, "mods.watch_poll") == 0) r = set_bool(&cfg->watch_poll, value);
    else if (strcmp(key, "mods.derived_events") == 0) r = set_bool(&cfg->derived_events, value);
    else if (strcmp(key, "debug.console") == 0) r = set_bool(&cfg->console, value);
    else if (strcmp(key, "debug.log_level") == 0) r = set_int(&cfg->log_level, value, 0, 4);
    if (r != BFM_PLAT_OK) cfg->rejected_values++;
    return r;
}

static int ini_cb(void *user, const char *section, const char *key,
                  const char *value, int line) {
    char full[64];
    (void)line;
    if (snprintf(full, sizeof full, "%s.%s", section, key) >= (int)sizeof full)
        return 1;
    bfm_plat_config_set((BfmPlatConfig *)user, full, value);
    return 1;
}

int bfm_plat_config_load_string(BfmPlatConfig *cfg, const char *text) {
    int r;
    if (!cfg || !text) return BFM_PLAT_INVALID;
    r = bfm_plat_ini_parse_string(text, ini_cb, cfg);
    cfg->bad_line = r > 0 ? r : 0;
    return BFM_PLAT_OK;
}

int bfm_plat_config_load_file(BfmPlatConfig *cfg, const char *path) {
    FILE *probe;
    int r;
    if (!cfg || !path) return BFM_PLAT_INVALID;
    probe = fopen(path, "rb");
    if (!probe) return BFM_PLAT_NOT_FOUND;
    fclose(probe);
    r = bfm_plat_ini_parse_file(path, ini_cb, cfg);
    if (r < 0) return BFM_PLAT_ERROR;
    cfg->bad_line = r;
    return BFM_PLAT_OK;
}

static void mark(unsigned char *used, int i) {
    if (used) used[i] = 1;
}

int bfm_plat_config_apply_args(BfmPlatConfig *cfg, int argc, char **argv,
                               unsigned char *used) {
    int i;
    if (!cfg || argc < 0 || (argc > 0 && !argv)) return BFM_PLAT_INVALID;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) return BFM_PLAT_INVALID;
            if (bfm_plat_config_load_file(cfg, argv[i + 1]) == BFM_PLAT_ERROR)
                return BFM_PLAT_INVALID;
            mark(used, i);
            mark(used, i + 1);
            i++;
        }
    }
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *next = i + 1 < argc ? argv[i + 1] : NULL;
        const char *key = NULL;
        if (strcmp(a, "--config") == 0) { i++; continue; }
        if (strcmp(a, "--widescreen") == 0) {
            bfm_plat_config_set(cfg, "video.aspect", "16:9");
            mark(used, i);
            continue;
        }
        if (strcmp(a, "--fullscreen") == 0) {
            cfg->fullscreen = 1;
            record(cfg, "video.fullscreen", "1");
            mark(used, i);
            continue;
        }
        if (strcmp(a, "--no-mods") == 0) {
            cfg->mods_enabled = 0;
            record(cfg, "mods.enabled", "0");
            mark(used, i);
            continue;
        }
        if (strcmp(a, "--set") == 0) {
            char k[64];
            const char *eq;
            if (!next || !(eq = strchr(next, '=')) ||
                (size_t)(eq - next) >= sizeof k)
                return BFM_PLAT_INVALID;
            memcpy(k, next, (size_t)(eq - next));
            k[eq - next] = '\0';
            if (bfm_plat_config_set(cfg, k, eq + 1) != BFM_PLAT_OK)
                return BFM_PLAT_INVALID;
            mark(used, i);
            mark(used, i + 1);
            i++;
            continue;
        }
        if (strcmp(a, "--fps") == 0) key = "video.frame_rate";
        else if (strcmp(a, "--scale") == 0) key = "video.internal_scale";
        else if (strcmp(a, "--aspect") == 0) key = "video.aspect";
        else if (strcmp(a, "--renderer") == 0) key = "backends.renderer";
        else if (strcmp(a, "--audio") == 0) key = "backends.audio";
        else if (strcmp(a, "--disc") == 0) key = "disc.path";
        if (key) {
            if (!next || bfm_plat_config_set(cfg, key, next) != BFM_PLAT_OK)
                return BFM_PLAT_INVALID;
            mark(used, i);
            mark(used, i + 1);
            i++;
        }
    }
    return BFM_PLAT_OK;
}

const char *bfm_plat_config_get(const BfmPlatConfig *cfg, const char *key) {
    unsigned i;
    if (!cfg || !key) return NULL;
    for (i = 0; i < cfg->entry_count; i++)
        if (strcmp(cfg->entries[i].key, key) == 0) return cfg->entries[i].value;
    return NULL;
}

static int is_typed_section(const char *key) {
    static const char *typed[] = {"video.", "timing.", "backends.", "disc.",
                                  "storage.", "mods.", "debug."};
    size_t i;
    for (i = 0; i < sizeof typed / sizeof typed[0]; i++)
        if (strncmp(key, typed[i], strlen(typed[i])) == 0) return 1;
    return 0;
}

int bfm_plat_config_save_file(const BfmPlatConfig *c, const char *path) {
    FILE *f;
    unsigned i;
    char section[64] = "";
    if (!c || !path || !(f = fopen(path, "w"))) return BFM_PLAT_ERROR;
    fprintf(f, "; Brave Fencer Musashi PC port configuration\n\n");
    fprintf(f, "[video]\naspect = %d:%d\nwidescreen_mode = %s\n",
            c->aspect_num, c->aspect_den,
            c->widescreen_mode == BFM_WIDESCREEN_ANAMORPHIC ? "anamorphic" : "hor+");
    fprintf(f, "internal_scale = %d\n"
               "window_width = %d\nwindow_height = %d\nfullscreen = %d\n"
               "frame_rate = %d\nvsync = %d\ndither = %s\ngpu = %s\nscreenshot_dir = %s\n\n",
            c->internal_scale, c->window_width,
            c->window_height, c->fullscreen, c->frame_rate, c->vsync,
            c->dither == BFM_DITHER_AUTO ? "auto" : c->dither ? "on" : "off",
            c->gpu_path == BFM_GPU_BFM_PLAT ? "bfm_plat" : "cpu", c->screenshot_dir);
    fprintf(f, "[timing]\nfast_forward_speed = %g\nfast_forward_toggle = %d\n\n",
            c->fast_forward_speed, c->fast_forward_toggle);
    fprintf(f, "[backends]\nrenderer = %s\naudio = %s\ninput = %s\n"
               "storage = %s\n\n",
            c->renderer, c->audio, c->input, c->storage);
    fprintf(f, "[disc]\npath = %s\nsearch = %s\nvalidate = %d\n\n[storage]\nsave_dir = %s\n\n",
            c->disc_path, c->disc_search, c->disc_validate, c->save_dir);
    fprintf(f, "[mods]\nenabled = %d\ndirs = %s\ndump_textures = %d\n"
               "dump_dir = %s\nwatch_poll = %d\nderived_events = %d\n\n",
            c->mods_enabled, c->mod_dirs, c->dump_textures, c->dump_dir,
            c->watch_poll, c->derived_events);
    fprintf(f, "[debug]\nconsole = %d\nlog_level = %d\n", c->console,
            c->log_level);
    for (i = 0; i < c->entry_count; i++) {
        const char *k = c->entries[i].key;
        const char *dot = strrchr(k, '.');
        size_t slen;
        if (is_typed_section(k) || !dot) continue;
        slen = (size_t)(dot - k);
        if (strlen(section) != slen || strncmp(section, k, slen) != 0) {
            memcpy(section, k, slen);
            section[slen] = '\0';
            fprintf(f, "\n[%s]\n", section);
        }
        fprintf(f, "%s = %s\n", dot + 1, c->entries[i].value);
    }
    return fclose(f) == 0 ? BFM_PLAT_OK : BFM_PLAT_ERROR;
}

void bfm_plat_config_aspect(const BfmPlatConfig *cfg, int *num, int *den) {
    if (num) *num = cfg && cfg->aspect_num > 0 ? cfg->aspect_num : 4;
    if (den) *den = cfg && cfg->aspect_den > 0 ? cfg->aspect_den : 3;
}
