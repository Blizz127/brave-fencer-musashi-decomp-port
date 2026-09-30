#ifndef BFM_PLAT_CONFIG_H
#define BFM_PLAT_CONFIG_H

/* User configuration: one INI file, then command-line overrides.
 *
 * Default path: $BFM_PORT_CONFIG, else $XDG_CONFIG_HOME/bfm-port/config.ini,
 * else $HOME/.config/bfm-port/config.ini (%APPDATA%\bfm-port\config.ini on
 * Windows). A missing file is not an error; defaults apply.
 *
 *   [video]    aspect (4:3 | 16:9 | 16:10), widescreen_mode (hor+ |
 *              anamorphic), widescreen (legacy: 1 = 16:9, 0 = 4:3),
 *              internal_scale, window_width, window_height, fullscreen,
 *              frame_rate (30|60), vsync, screenshot_dir,
 *              dither (auto | on | off; auto = PS1 dither at 1x only),
 *              gpu (cpu | bfm_plat: native_boot's GPU ports rasterise on the
 *              CPU, or feed bfm_plat_gpu_bridge -> the renderer backend)
 *   [timing]   fast_forward_speed (x real speed, 0 = uncapped),
 *              fast_forward_toggle (0 hold / 1 toggle)
 *   [backends] renderer, audio, input, storage   (backend names)
 *   [disc]     path       (the user's own disc image: .cue, .bin or .iso)
 *              search (';'-separated dirs tried when path is empty),
 *              validate (check SLUS_007.26 before booting, default 1)
 *   [storage]  save_dir   (memory card files)
 *   [mods]     enabled, dirs (';'-separated), dump_textures, dump_dir,
 *              watch_poll, derived_events
 *   [debug]    console, log_level
 *
 * Command line: --config PATH, --set section.key=value, --widescreen,
 * --aspect 16:9, --fps N, --scale N, --renderer NAME, --audio NAME, --disc PATH, --no-mods,
 * --fullscreen. Unknown options are left for the caller (see argv_used).
 *
 * Every key is also readable as a string through bfm_plat_config_get() so
 * plugins can have their own "[mod.<name>]" sections. */

#include "bfm_plat_types.h"

#define BFM_PLAT_NAME_MAX 32
#define BFM_PLAT_PATH_MAX 512
#define BFM_PLAT_CONFIG_EXTRA_MAX 128

#define BFM_WIDESCREEN_HOR_PLUS 0     /* wider view, unstretched pixels */
#define BFM_WIDESCREEN_ANAMORPHIC 1
#define BFM_GPU_CPU 0                 /* host's own GPU rasteriser */
#define BFM_GPU_BFM_PLAT 1            /* bfm_plat_gpu_bridge -> renderer */
#define BFM_DITHER_AUTO (-1)          /* dither at internal_scale 1 only */
#define BFM_DITHER_OFF 0
#define BFM_DITHER_ON 1   /* squeeze X in projection, stretch output */

typedef struct BfmPlatConfigEntry {
    char key[64];   /* "section.key" */
    char value[BFM_PLAT_PATH_MAX];
} BfmPlatConfigEntry;

typedef struct BfmPlatConfig {
    /* video */
    int widescreen;          /* derived: aspect is not 4:3 */
    int aspect_num, aspect_den;   /* output aspect, 4:3 retail */
    int widescreen_mode;     /* BFM_WIDESCREEN_HOR_PLUS / _ANAMORPHIC */
    int internal_scale;      /* 1..8, multiplies the 320x240-class buffer */
    int window_width;
    int window_height;
    int fullscreen;
    int frame_rate;          /* 30 (retail pacing) or 60 (interpolated) */
    int vsync;
    int dither;              /* BFM_DITHER_AUTO / _OFF / _ON (gl renderer) */
    int gpu_path;            /* BFM_GPU_CPU / BFM_GPU_BFM_PLAT */
    char screenshot_dir[BFM_PLAT_PATH_MAX];
    /* timing */
    double fast_forward_speed;
    int fast_forward_toggle;
    /* backends */
    char renderer[BFM_PLAT_NAME_MAX];
    char audio[BFM_PLAT_NAME_MAX];
    char input[BFM_PLAT_NAME_MAX];
    char storage[BFM_PLAT_NAME_MAX];
    /* disc / storage */
    char disc_path[BFM_PLAT_PATH_MAX];
    char disc_search[BFM_PLAT_PATH_MAX];  /* first-run autodetect dirs */
    int disc_validate;       /* check the boot EXE before booting */
    char save_dir[BFM_PLAT_PATH_MAX];
    /* mods */
    int mods_enabled;
    char mod_dirs[BFM_PLAT_PATH_MAX];
    int dump_textures;
    char dump_dir[BFM_PLAT_PATH_MAX];
    int watch_poll;          /* poll guest-memory watches each frame */
    int derived_events;      /* damage/bp_use/money/item_get from watches */
    /* debug */
    int console;
    int log_level;
    /* Everything read (including unknown keys), for bfm_plat_config_get. */
    unsigned entry_count;
    BfmPlatConfigEntry entries[BFM_PLAT_CONFIG_EXTRA_MAX];
    /* Diagnostics from the last load: first bad line (0 = none) and count of
     * keys whose values were out of range and fell back to defaults. */
    int bad_line;
    int rejected_values;
} BfmPlatConfig;

void bfm_plat_config_defaults(BfmPlatConfig *cfg);

/* Resolves the default config path into out. Returns BFM_PLAT_OK or
 * BFM_PLAT_NOT_FOUND if no home/config directory is known. */
int bfm_plat_config_default_path(char *out, size_t out_size);

/* Applies one "section.key" = value. Returns BFM_PLAT_OK, or
 * BFM_PLAT_INVALID when the value is out of range (the field keeps its
 * previous value; the string is still recorded). */
int bfm_plat_config_set(BfmPlatConfig *cfg, const char *key,
                        const char *value);

/* Loads INI text/file on top of the current values. Missing file returns
 * BFM_PLAT_NOT_FOUND and leaves cfg unchanged. */
int bfm_plat_config_load_string(BfmPlatConfig *cfg, const char *text);
int bfm_plat_config_load_file(BfmPlatConfig *cfg, const char *path);

/* Parses argv (argv[0] skipped). Recognized options are applied and marked
 * in argv_used[i] = 1 if argv_used is non-NULL. A --config PATH option is
 * loaded first, before the other overrides, whatever its position.
 * Returns BFM_PLAT_OK or BFM_PLAT_INVALID on a malformed option. */
int bfm_plat_config_apply_args(BfmPlatConfig *cfg, int argc,
                               char **argv, unsigned char *argv_used);

/* "section.key" lookup over every loaded/overridden entry. NULL if unset. */
const char *bfm_plat_config_get(const BfmPlatConfig *cfg, const char *key);

/* Writes the typed fields back as INI (unknown sections are kept). */
int bfm_plat_config_save_file(const BfmPlatConfig *cfg, const char *path);

/* Output aspect (numerator/denominator). */
void bfm_plat_config_aspect(const BfmPlatConfig *cfg, int *num, int *den);

#endif
