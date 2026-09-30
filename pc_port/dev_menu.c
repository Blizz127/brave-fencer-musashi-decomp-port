/* Port Dev Menu model: opt-in cheats and tested warps (musashi_dev_menu.h). */
#include "musashi_dev_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUEST_HP_MAX 0x80078eb2u
#define GUEST_HP 0x80078eb4u
#define GUEST_BP_MAX 0x80078eb6u
#define GUEST_BP 0x80078eb8u
#define GUEST_DRANS 0x80078e8cu
#define DRANS_CAP 99999u

enum { PAGE_MAIN, PAGE_CHEATS, PAGE_WARPS };
enum { SCRIPT_MAX = 64, LINES_MAX = 16, LINE_W = 48 };

/* A warp is added here only after it is tested working in the port. */
typedef struct DevWarp { const char *area; } DevWarp;
static const DevWarp kWarps[] = { { NULL } };
enum { WARP_COUNT = 0 };

static struct {
    int inited, enabled, open, page, selection;
    int infinite_hp, infinite_bp, max_drans;
    unsigned vblanks, cheat_writes;
    unsigned scene, mode;
    int in_scene;
    MusashiDevMenuGuest guest;
    struct { unsigned frame, key; } script[SCRIPT_MAX];
    unsigned nscript, iscript;
    char lines[LINES_MAX][LINE_W];
} M;

static unsigned parse_key(const char *s, size_t n) {
    static const struct { const char *name; unsigned key; } names[] = {
        {"TOGGLE", MUSASHI_DEV_KEY_TOGGLE}, {"UP", MUSASHI_DEV_KEY_UP},
        {"DOWN", MUSASHI_DEV_KEY_DOWN}, {"OK", MUSASHI_DEV_KEY_OK},
        {"BACK", MUSASHI_DEV_KEY_BACK}};
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (strlen(names[i].name) == n && !strncmp(s, names[i].name, n)) return names[i].key;
    return 0;
}

static void parse_script(const char *s) {
    while (s && *s && M.nscript < SCRIPT_MAX) {
        char *end;
        unsigned long frame = strtoul(s, &end, 10);
        const char *key = end + 1, *next;
        size_t n;
        if (end == s || *end != ':') break;
        next = strchr(key, ',');
        n = next ? (size_t)(next - key) : strlen(key);
        M.script[M.nscript].frame = (unsigned)frame;
        M.script[M.nscript].key = parse_key(key, n);
        if (M.script[M.nscript].key) ++M.nscript;
        s = next ? next + 1 : NULL;
    }
}

int musashi_dev_menu_init(const MusashiDevMenuGuest *guest, int force_on) {
    const char *env = getenv("BFM_DEV_MENU");
    if (M.inited) return M.enabled;
    memset(&M, 0, sizeof M);
    M.inited = 1;
    M.enabled = force_on || (env && env[0] == '1');
    if (!M.enabled || !guest) {
        M.enabled = 0;
        return 0;
    }
    M.guest = *guest;
    parse_script(getenv("BFM_DEV_MENU_KEYS"));
    fprintf(stderr, "native_boot: DEV_MENU enabled scripted_keys=%u (F1 or L3+R3 opens)\n",
            M.nscript);
    return 1;
}

int musashi_dev_menu_enabled(void) { return M.enabled; }
int musashi_dev_menu_is_open(void) { return M.enabled && M.open; }

static unsigned page_items(void) {
    switch (M.page) {
    case PAGE_CHEATS: return 4u;
    case PAGE_WARPS: return WARP_COUNT + 1u;
    default: return 3u;
    }
}

static void toggle_cheat(int *flag, const char *name) {
    *flag = !*flag;
    fprintf(stderr, "native_boot: DEV_MENU cheat=%s %s\n", name, *flag ? "on" : "off");
}

static void select_item(void) {
    unsigned n = page_items();
    if ((unsigned)M.selection + 1u == n && M.page != PAGE_MAIN) {
        M.page = PAGE_MAIN;
        M.selection = 0;
        return;
    }
    if (M.page == PAGE_MAIN) {
        if (M.selection == 0) M.page = PAGE_CHEATS;
        else if (M.selection == 1) M.page = PAGE_WARPS;
        else M.open = 0;
        M.selection = 0;
    } else if (M.page == PAGE_CHEATS) {
        if (M.selection == 0) toggle_cheat(&M.infinite_hp, "infinite_hp");
        else if (M.selection == 1) toggle_cheat(&M.infinite_bp, "infinite_bp");
        else toggle_cheat(&M.max_drans, "max_drans");
    }
    /* PAGE_WARPS: no tested warp yet, only Back. */
}

void musashi_dev_menu_keys(unsigned pressed) {
    unsigned n;
    if (!M.enabled || !pressed) return;
    if (pressed & MUSASHI_DEV_KEY_TOGGLE) {
        M.open = !M.open;
        M.page = PAGE_MAIN;
        M.selection = 0;
        fprintf(stderr, "native_boot: DEV_MENU %s vblank=%u\n", M.open ? "open" : "closed", M.vblanks);
        return;
    }
    if (!M.open) return;
    n = page_items();
    if (pressed & MUSASHI_DEV_KEY_UP) M.selection = (M.selection + (int)n - 1) % (int)n;
    if (pressed & MUSASHI_DEV_KEY_DOWN) M.selection = (M.selection + 1) % (int)n;
    if (pressed & MUSASHI_DEV_KEY_OK) select_item();
    else if (pressed & MUSASHI_DEV_KEY_BACK) {
        if (M.page == PAGE_MAIN) M.open = 0;
        M.page = PAGE_MAIN;
        M.selection = 0;
    }
}

/* current = max, the same copy the game's own refill (func_8014BC44) does. */
static void refill(uint32_t max_address, uint32_t address) {
    uint16_t max, cur;
    if (!M.guest.read16(M.guest.user, max_address, &max) ||
        !M.guest.read16(M.guest.user, address, &cur) || cur == max) return;
    if (M.guest.write16(M.guest.user, address, max)) ++M.cheat_writes;
}

void musashi_dev_menu_vblank(int in_scene, unsigned scene, unsigned mode) {
    if (!M.enabled) return;
    ++M.vblanks;
    M.in_scene = in_scene;
    M.scene = scene;
    M.mode = mode;
    while (M.iscript < M.nscript && M.script[M.iscript].frame <= M.vblanks)
        musashi_dev_menu_keys(M.script[M.iscript++].key);
    if (!in_scene) return;
    if (M.infinite_hp) refill(GUEST_HP_MAX, GUEST_HP);
    if (M.infinite_bp) refill(GUEST_BP_MAX, GUEST_BP);
    if (M.max_drans) {
        uint32_t drans;
        if (M.guest.read32(M.guest.user, GUEST_DRANS, &drans) && drans != DRANS_CAP &&
            M.guest.write32(M.guest.user, GUEST_DRANS, DRANS_CAP)) ++M.cheat_writes;
    }
}

static void line(size_t *n, const char *fmt, const char *a, const char *b) {
    if (*n >= LINES_MAX) return;
    snprintf(M.lines[*n], LINE_W, fmt, a ? a : "", b ? b : "");
    ++*n;
}

size_t musashi_dev_menu_text(const char **lines, size_t max, int *highlight) {
    size_t n = 0, i, first;
    char info[LINE_W];
    int any = M.infinite_hp || M.infinite_bp || M.max_drans;
    if (highlight) *highlight = -1;
    if (!M.enabled || (!M.open && !any)) return 0;
    if (!M.open) {
        line(&n, "DEV%s%s", "", "");
    } else {
        snprintf(info, sizeof info, "SCENE %04X  MODE %04X%s", M.scene & 0xffffu, M.mode & 0xffffu,
                 M.in_scene ? "" : "  (NO SCENE)");
        line(&n, "PORT DEV MENU%s%s", "", "");
        line(&n, "%s%s", info, "");
        line(&n, "%s%s", "", "");
        first = n;
        if (M.page == PAGE_MAIN) {
            line(&n, "  CHEATS%s%s", "", "");
            line(&n, "  WARPS%s%s", "", "");
            line(&n, "  CLOSE%s%s", "", "");
        } else if (M.page == PAGE_CHEATS) {
            line(&n, "  INFINITE HP      %s%s", M.infinite_hp ? "ON" : "OFF", "");
            line(&n, "  INFINITE BP      %s%s", M.infinite_bp ? "ON" : "OFF", "");
            line(&n, "  MAX DRANS        %s%s", M.max_drans ? "ON" : "OFF", "");
            line(&n, "  BACK%s%s", "", "");
        } else {
            for (i = 0; i < WARP_COUNT; ++i) line(&n, "  %s%s", kWarps[i].area, "");
            if (!WARP_COUNT) {
                line(&n, "  (NO TESTED WARPS YET)%s%s", "", "");
                ++first;
            }
            line(&n, "  BACK%s%s", "", "");
        }
        if (highlight) *highlight = (int)(first + (size_t)M.selection);
        line(&n, "%s%s", "", "");
        line(&n, "UP/DOWN MOVE  CROSS/ENTER OK  CIRCLE/ESC BACK%s%s", "", "");
    }
    for (i = 0; i < n && i < max; ++i) lines[i] = M.lines[i];
    return n < max ? n : max;
}

void musashi_dev_menu_state(MusashiDevMenuState *out) {
    if (!out) return;
    out->enabled = M.enabled;
    out->open = M.open;
    out->page = M.page;
    out->selection = M.selection;
    out->infinite_hp = M.infinite_hp;
    out->infinite_bp = M.infinite_bp;
    out->max_drans = M.max_drans;
    out->vblanks = M.vblanks;
    out->cheat_writes = M.cheat_writes;
}

unsigned musashi_dev_menu_warp_count(void) { return WARP_COUNT; }

void musashi_dev_menu_reset(void) {
    (void)kWarps;
    memset(&M, 0, sizeof M);
}
