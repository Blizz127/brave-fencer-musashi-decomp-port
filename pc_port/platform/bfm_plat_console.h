#ifndef BFM_PLAT_CONSOLE_H
#define BFM_PLAT_CONSOLE_H

/* Cheat registry, cheat menu model and developer console.
 *
 * Cheats come from plugins (register_cheat) or from a mod's cheats.ini
 * (poke cheats, no C needed). Enabled cheats run on every
 * BFM_EVENT_FRAME_END. The menu is a model only: a renderer overlay draws
 * it with bfm_plat_renderer_overlay_text, and input hotkeys drive it.
 *
 * Built-in console commands:
 *   help                      list commands
 *   cheats                    list cheats and state
 *   cheat NAME on|off|toggle  switch a cheat
 *   peek ADDR [N]             hex dump N (<=64) guest bytes
 *   poke ADDR VALUE [W]       write W (1/2/4) bytes once
 *   get SECTION.KEY           read config
 *   set SECTION.KEY VALUE     change config (runtime copy)
 *   mods                      list loaded mods
 *   ff [SPEED]                toggle fast-forward / set speed */

#include "bfm_plugin.h"
#include "bfm_plat_config.h"

#define BFM_PLAT_CHEATS_MAX 256
#define BFM_PLAT_COMMANDS_MAX 64

typedef struct BfmPlatCheatState {
    BfmCheat cheat;
    int enabled;
    int owner_mod;                /* -1 = built-in */
    uint64_t applied_frames;
    int last_error;
} BfmPlatCheatState;

/* Registers the FRAME_END hook; the config pointer (may be NULL) backs
 * get/set and must outlive the console. */
void bfm_plat_console_init(BfmPlatConfig *config);
void bfm_plat_console_shutdown(void);

int bfm_plat_cheat_register(const BfmCheat *cheat, int owner_mod);
/* Copies name/description into console-owned storage (for cheats.ini). */
int bfm_plat_cheat_register_copy(const BfmCheat *cheat, int owner_mod);
/* Drops every cheat owned by a mod (owner_mod >= 0); mods shutdown. */
void bfm_plat_cheat_drop_owned(void);
int bfm_plat_cheat_set(const char *name, int enabled);
unsigned bfm_plat_cheat_count(void);
const BfmPlatCheatState *bfm_plat_cheat_get(unsigned index);
/* Runs every enabled cheat once (the FRAME_END hook calls this). */
void bfm_plat_cheats_apply(void);

/* Cheat menu model. */
void bfm_plat_cheat_menu_open(int open);
int bfm_plat_cheat_menu_is_open(void);
void bfm_plat_cheat_menu_move(int delta);
unsigned bfm_plat_cheat_menu_selection(void);
int bfm_plat_cheat_menu_toggle_selected(void);
/* Draws the menu through the renderer overlay (no-op if closed). */
void bfm_plat_cheat_menu_draw(void);

int bfm_plat_console_register(const char *name, const char *help,
                              BfmCommandFn fn, void *user);
/* As above, owned by a mod (>= 0): dropped by bfm_plat_console_drop_owned
 * when mods shut down, so an unloaded plugin never leaves a command. */
int bfm_plat_console_register_owned(const char *name, const char *help,
                                    BfmCommandFn fn, void *user, int owner_mod);
void bfm_plat_console_drop_owned(void);
/* Executes one line; reply goes into out. Returns 0 ok, nonzero error
 * (unknown command or usage). */
int bfm_plat_console_exec(const char *line, char *out, size_t out_size);

void bfm_plat_console_reset(void);

#endif
