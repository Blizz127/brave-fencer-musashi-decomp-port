/* Port Dev Menu model: inert when off, cheats only through the game's own
 * state and only in a scene, scripted keys, no untested warp. */
#include "musashi_dev_menu.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t ram[0x200000];
static unsigned writes;

static uint8_t *at(uint32_t a) { return &ram[a & 0x1fffffu]; }
static int r16(void *u, uint32_t a, uint16_t *v) { (void)u; memcpy(v, at(a), 2); return 1; }
static int w16(void *u, uint32_t a, uint16_t v) { (void)u; memcpy(at(a), &v, 2); ++writes; return 1; }
static int r32(void *u, uint32_t a, uint32_t *v) { (void)u; memcpy(v, at(a), 4); return 1; }
static int w32(void *u, uint32_t a, uint32_t v) { (void)u; memcpy(at(a), &v, 4); ++writes; return 1; }
static const MusashiDevMenuGuest guest = { NULL, r16, w16, r32, w32 };

static void put16(uint32_t a, uint16_t v) { memcpy(at(a), &v, 2); }
static uint16_t get16(uint32_t a) { uint16_t v; memcpy(&v, at(a), 2); return v; }
static uint32_t get32(uint32_t a) { uint32_t v; memcpy(&v, at(a), 4); return v; }

static void game_state(void) {
    memset(ram, 0, sizeof ram);
    put16(0x80078eb2u, 300); put16(0x80078eb4u, 12);   /* HP max, current */
    put16(0x80078eb6u, 200); put16(0x80078eb8u, 7);    /* BP max, current */
    writes = 0;
}

int main(void) {
    MusashiDevMenuState st;
    const char *lines[16];
    int hl;
    unsigned i;

    /* Off by default: every entry point is inert. */
    unsetenv("BFM_DEV_MENU");
    game_state();
    assert(!musashi_dev_menu_init(&guest, 0));
    for (i = 0; i < 100; ++i) {
        musashi_dev_menu_keys(MUSASHI_DEV_KEY_TOGGLE | MUSASHI_DEV_KEY_OK);
        musashi_dev_menu_vblank(1, 0x000b, 8);
    }
    assert(!musashi_dev_menu_is_open() && writes == 0);
    assert(musashi_dev_menu_text(lines, 16, &hl) == 0 && hl == -1);
    musashi_dev_menu_state(&st);
    assert(!st.enabled && !st.open && !st.infinite_hp && st.vblanks == 0);

    /* On: open, Cheats page, Infinite HP. Nothing written before a scene. */
    musashi_dev_menu_reset();
    setenv("BFM_DEV_MENU", "1", 1);
    setenv("BFM_DEV_MENU_KEYS", "2:TOGGLE,3:OK,4:OK", 1);
    game_state();
    assert(musashi_dev_menu_init(&guest, 0));
    for (i = 0; i < 5; ++i) musashi_dev_menu_vblank(0, 0, 0);
    musashi_dev_menu_state(&st);
    assert(st.open && st.page == 1 && st.infinite_hp && !st.infinite_bp);
    assert(writes == 0 && get16(0x80078eb4u) == 12);
    assert(musashi_dev_menu_text(lines, 16, &hl) > 4 && hl >= 0);
    assert(strstr(lines[hl], "INFINITE HP") && strstr(lines[hl], "ON"));
    /* The game-facing pad is neutral while open (the host checks this). */
    assert(musashi_dev_menu_is_open());

    /* In a scene: current HP refilled to max, the game's own gauge. */
    musashi_dev_menu_vblank(1, 0x000b, 8);
    assert(get16(0x80078eb4u) == 300 && get16(0x80078eb2u) == 300 && writes == 1);
    musashi_dev_menu_vblank(1, 0x000b, 8);
    assert(writes == 1);                       /* no write when already full */

    /* Down, down: Max Drans; the game's cap 99999. BP stays untouched. */
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_DOWN);
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_DOWN);
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_OK);
    musashi_dev_menu_vblank(1, 0x000b, 8);
    assert(get32(0x80078e8cu) == 99999u && get16(0x80078eb8u) == 7);

    /* Closed with cheats on: only the DEV badge is drawn. */
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_BACK);   /* cheats -> main */
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_BACK);   /* main -> closed */
    assert(!musashi_dev_menu_is_open());
    assert(musashi_dev_menu_text(lines, 16, &hl) == 1 && !strcmp(lines[0], "DEV") && hl == -1);
    /* Menu keys other than the toggle do nothing while closed. */
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_OK | MUSASHI_DEV_KEY_DOWN);
    musashi_dev_menu_state(&st);
    assert(!st.open && st.infinite_hp && st.max_drans);

    /* Warps: none is listed until tested in the port; only Back. */
    assert(musashi_dev_menu_warp_count() == 0);
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_TOGGLE);
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_DOWN);
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_OK);
    assert(musashi_dev_menu_text(lines, 16, &hl) > 0 && strstr(lines[hl], "BACK"));
    musashi_dev_menu_keys(MUSASHI_DEV_KEY_OK);
    musashi_dev_menu_state(&st);
    assert(st.open && st.page == 0);

    puts("DEV_MENU_PASS off_inert=1 cheats=game_state warps=0");
    return 0;
}
