#ifndef BFM_PLAT_MODS_H
#define BFM_PLAT_MODS_H

/* Mods: folder loader, event hooks, guest-memory binding, asset replacement.
 *
 * Layout of one mod (a folder inside any configured mods dir):
 *
 *   mods/<folder>/mod.ini        [mod] name, version, priority, enabled,
 *                                      plugin, script, description
 *   mods/<folder>/<plugin>.so    C plugin (see bfm_plugin.h); .dll on Windows
 *   mods/<folder>/cheats.ini     [cheat NAME] address, value, width,
 *                                      description, enabled
 *   mods/<folder>/assets/textures/<hash>.<ext>   VRAM upload replacements
 *   mods/<folder>/assets/files/<hash>.<ext>      disc file replacements
 *
 * Load order: ascending priority (default 100), then folder name. Later
 * mods win when two replace the same asset. Replacement files come only
 * from mods; nothing is shipped from the disc.
 *
 * Asset key: 16 lowercase hex digits of FNV-1a 64 over the original bytes.
 * Textures hash the upload as: width (le16), height (le16), then the
 * width*height 16-bit pixels (le16 each).
 *
 * Texture decoders are registered per file extension. Built in: ".bfmi"
 * = "BFMI" magic, le16 width, le16 height, then RGBA8888 pixels. PNG and
 * others plug in through bfm_plat_mods_register_decoder. */

#include "bfm_plugin.h"
#include "bfm_plat_renderer.h"

#define BFM_PLAT_MODS_MAX 64
#define BFM_PLAT_MOD_NAME_MAX 64

typedef struct BfmPlatModInfo {
    char name[BFM_PLAT_MOD_NAME_MAX];
    char version[32];
    char folder[BFM_PLAT_MOD_NAME_MAX];
    char dir[512];
    char plugin[BFM_PLAT_MOD_NAME_MAX];
    char script[BFM_PLAT_MOD_NAME_MAX];
    int priority;
    int enabled;
    int plugin_loaded;            /* 1 loaded, 0 none, <0 failed */
    int script_loaded;
    unsigned cheats;
    unsigned textures;
    unsigned files;
} BfmPlatModInfo;

typedef struct BfmPlatGuestMemory {
    int (*read)(void *user, uint32_t address, void *out, uint32_t size);
    int (*write)(void *user, uint32_t address, const void *data,
                 uint32_t size);
    void *user;
} BfmPlatGuestMemory;

/* Decodes a replacement texture file into RGBA8888. *rgba is malloc'd and
 * freed by the mods module. */
typedef int (*BfmPlatTextureDecoder)(const char *path, uint32_t *width,
                                     uint32_t *height, uint8_t **rgba);

/* Script runtime seam (Lua later). load() is called for each mod whose
 * manifest names a script with this runtime's extension. */
typedef struct BfmPlatScriptRuntime {
    const char *name;
    const char *extension;        /* ".lua" */
    int (*load)(const BfmPluginHost *host, const char *script_path);
    void (*shutdown)(void);
} BfmPlatScriptRuntime;

typedef struct BfmPlatModsOptions {
    const char *dirs;             /* ';'-separated */
    int allow_plugins;            /* dlopen native plugins */
    int dump_textures;
    const char *dump_dir;
    void (*log)(const char *mod, int level, const char *message);
} BfmPlatModsOptions;

/* Scans, sorts, loads plugins/scripts/cheats and indexes assets, then emits
 * BFM_EVENT_BOOT. Returns number of enabled mods (>= 0) or an error. */
int bfm_plat_mods_init(const BfmPlatModsOptions *options);
void bfm_plat_mods_shutdown(void);

unsigned bfm_plat_mods_count(void);
const BfmPlatModInfo *bfm_plat_mods_get(unsigned index);

/* Plugins linked into the executable (no dlopen), keyed by the manifest's
 * plugin name. Register before bfm_plat_mods_init. */
int bfm_plat_mods_register_static_plugin(const char *plugin_name,
                                         BfmPluginInitFn init,
                                         BfmPluginShutdownFn shutdown);
int bfm_plat_mods_register_script_runtime(const BfmPlatScriptRuntime *rt);
int bfm_plat_mods_register_decoder(const char *extension,
                                   BfmPlatTextureDecoder decoder);

/* Events. Subscriptions run in load order, then subscription order. */
int bfm_plat_mods_subscribe(BfmEvent event, BfmEventFn fn, void *user);
int bfm_plat_mods_unsubscribe(int subscription);
void bfm_plat_mods_emit(BfmEvent event, void *payload);
uint64_t bfm_plat_mods_event_count(BfmEvent event);

/* The runtime binds guest RAM once it exists; NULL unbinds. */
void bfm_plat_guest_memory_bind(const BfmPlatGuestMemory *memory);
int bfm_plat_guest_read(uint32_t address, void *out, uint32_t size);
int bfm_plat_guest_write(uint32_t address, const void *data, uint32_t size);

/* Guest CPU registers (r0..r31 by number), bound by the runtime so hook
 * fills can read call arguments ($a0 = 4, $a1 = 5, ...). */
typedef struct BfmPlatGuestRegs {
    int (*read)(void *user, unsigned reg, uint32_t *out);
    void *user;
} BfmPlatGuestRegs;
void bfm_plat_guest_regs_bind(const BfmPlatGuestRegs *regs);
int bfm_plat_guest_reg(unsigned reg, uint32_t *out);

/* Asset replacement. */
uint64_t bfm_plat_hash64(const void *data, size_t size);
uint64_t bfm_plat_texture_hash(const BfmPlatRect *rect, const uint16_t *px);
void bfm_plat_hash_hex(uint64_t hash, char out[17]);
/* BFM_PLAT_OK with out pointing at mods-owned bytes, or BFM_PLAT_NOT_FOUND. */
int bfm_plat_mods_replace_asset(const char *kind, const void *data,
                                size_t size, const uint8_t **out,
                                size_t *out_size);
int bfm_plat_mods_replace_texture(const BfmPlatRect *rect,
                                  const uint16_t *pixels, BfmPlatImage *out);
/* Texture dumping for modders (off by default): writes each unique upload
 * to <dump_dir>/textures/<hash>.bfmi on the user's machine. */
void bfm_plat_mods_observe_texture(const BfmPlatRect *rect,
                                   const uint16_t *pixels);

/* The host table handed to plugins/scripts, for a given mod. */
const BfmPluginHost *bfm_plat_mods_host(unsigned mod_index);

/* Index of the mod whose plugin/script is loading right now, or -1. */
int bfm_plat_mods_loading(void);

/* Backs host->config_get (bfm_plat_init wires it to the user config). */
void bfm_plat_mods_set_config_getter(const char *(*fn)(const char *key));

void bfm_plat_mods_log(const char *mod, int level, const char *message);

/* Test support: drops every mod, subscription, static plugin, decoder,
 * runtime and guest binding. */
void bfm_plat_mods_reset(void);

#endif
