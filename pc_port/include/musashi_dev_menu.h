#ifndef MUSASHI_DEV_MENU_H
#define MUSASHI_DEV_MENU_H

/* Port Dev Menu: opt-in cheats and warps for native_boot.
 *
 * Off by default. Without BFM_DEV_MENU=1 (or --dev-menu) every entry point
 * is a no-op: no guest write, no input consumed, nothing drawn, so the game
 * runs exactly as it does without the menu (docs/KNOWN-DIVERGENCES.md: mods
 * and cheats are opt-in and inert when off).
 *
 * Cheats act only through the game's own state, the RAM the game itself
 * reads (SLUS-00726): gauge 1 max/current HP 80078EB2/80078EB4 (raised by
 * func_8014BC0C, capped at 500; refilled by func_8014BC44), gauge 2 max/current
 * BP 80078EB6/80078EB8, Drans 80078E8C (the game's cap 99999). They are
 * applied once per guest VBlank while in a scene, never before one.
 *
 * Warps are listed only once they are tested working in the port; the table
 * grows as the port progresses (see kWarps in dev_menu.c).
 *
 * Open/close: F1, or L3+R3 (both stick clicks) on a controller; the digital
 * pad the game reads has no L3/R3. While open the game's pad reads neutral.
 * Navigate: Up/Down (d-pad, left stick, arrows), Cross/A or Enter/C select,
 * Circle/B or Esc/Backspace back. "DEV" is shown while any cheat is on.
 *
 * Headless tests: BFM_DEV_MENU_KEYS="frame:KEY,..." presses KEY (TOGGLE, UP,
 * DOWN, OK, BACK) at guest VBlank `frame`. */

#include <stddef.h>
#include <stdint.h>

typedef struct MusashiDevMenuGuest {
    void *user;
    int (*read16)(void *user, uint32_t address, uint16_t *value);
    int (*write16)(void *user, uint32_t address, uint16_t value);
    int (*read32)(void *user, uint32_t address, uint32_t *value);
    int (*write32)(void *user, uint32_t address, uint32_t value);
} MusashiDevMenuGuest;

enum {
    MUSASHI_DEV_KEY_TOGGLE = 1u << 0,
    MUSASHI_DEV_KEY_UP = 1u << 1,
    MUSASHI_DEV_KEY_DOWN = 1u << 2,
    MUSASHI_DEV_KEY_OK = 1u << 3,
    MUSASHI_DEV_KEY_BACK = 1u << 4
};

/* Reads BFM_DEV_MENU and BFM_DEV_MENU_KEYS; `force_on` is --dev-menu.
 * Returns 1 when the menu is enabled. */
int musashi_dev_menu_init(const MusashiDevMenuGuest *guest, int force_on);
int musashi_dev_menu_enabled(void);
/* 1 while the menu is on screen: the host must feed the game a neutral pad. */
int musashi_dev_menu_is_open(void);
/* Keys pressed this VBlank (edges, MUSASHI_DEV_KEY_*). */
void musashi_dev_menu_keys(unsigned pressed);
/* Once per guest VBlank: scripted keys, then enabled cheats (only when
 * `in_scene`). `scene` and `mode` are shown on the info line. */
void musashi_dev_menu_vblank(int in_scene, unsigned scene, unsigned mode);

/* Text to draw, top to bottom; *highlight is the selected line or -1.
 * Returns the line count (0 when nothing is shown). */
size_t musashi_dev_menu_text(const char **lines, size_t max, int *highlight);

/* Test access. */
typedef struct MusashiDevMenuState {
    int enabled, open, page, selection;
    int infinite_hp, infinite_bp, max_drans;
    unsigned vblanks, cheat_writes;
} MusashiDevMenuState;
void musashi_dev_menu_state(MusashiDevMenuState *out);
unsigned musashi_dev_menu_warp_count(void);
void musashi_dev_menu_reset(void);

#endif
