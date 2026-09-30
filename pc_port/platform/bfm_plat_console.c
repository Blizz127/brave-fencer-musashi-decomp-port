#include "bfm_plat_console.h"
#include "bfm_plat_mods.h"
#include "bfm_plat_renderer.h"
#include "bfm_plat_timing.h"
#include "bfm_plat_ini.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 8

typedef struct Command {
    char name[32];
    const char *help;
    BfmCommandFn fn;
    void *user;
    int owner;
} Command;

/* cheats.ini cheats: console-owned copies of their strings. */
typedef struct CheatStrings {
    char name[64];
    char description[128];
} CheatStrings;

static BfmPlatCheatState cheats[BFM_PLAT_CHEATS_MAX];
static CheatStrings cheat_strings[BFM_PLAT_CHEATS_MAX];
static unsigned cheat_count;
static Command commands[BFM_PLAT_COMMANDS_MAX];
static unsigned command_count;
static BfmPlatConfig *config;
static int frame_sub;
static int menu_open;
static unsigned menu_sel;

static void reply(char *out, size_t size, const char *fmt, ...) {
    va_list ap;
    if (!out || !size) return;
    va_start(ap, fmt);
    vsnprintf(out, size, fmt, ap);
    va_end(ap);
}

static void append(char *out, size_t size, const char *fmt, ...) {
    size_t n;
    va_list ap;
    if (!out || !size) return;
    n = strlen(out);
    if (n + 1 >= size) return;
    va_start(ap, fmt);
    vsnprintf(out + n, size - n, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------- cheats */

static int name_ok(const char *s) {
    if (!s || !*s || strlen(s) >= 64) return 0;
    for (; *s; s++)
        if (isspace((unsigned char)*s)) return 0;
    return 1;
}

static int find_cheat(const char *name) {
    unsigned i;
    for (i = 0; i < cheat_count; i++)
        if (strcmp(cheats[i].cheat.name, name) == 0) return (int)i;
    return -1;
}

int bfm_plat_cheat_register(const BfmCheat *c, int owner) {
    if (!c || !name_ok(c->name)) return BFM_PLAT_INVALID;
    if (!c->apply && c->width != 1 && c->width != 2 && c->width != 4)
        return BFM_PLAT_INVALID;
    if (find_cheat(c->name) >= 0) return BFM_PLAT_INVALID;
    if (cheat_count >= BFM_PLAT_CHEATS_MAX) return BFM_PLAT_NO_SPACE;
    memset(&cheats[cheat_count], 0, sizeof cheats[0]);
    cheats[cheat_count].cheat = *c;
    if (!cheats[cheat_count].cheat.description)
        cheats[cheat_count].cheat.description = "";
    cheats[cheat_count].owner_mod = owner;
    cheat_count++;
    return BFM_PLAT_OK;
}

int bfm_plat_cheat_register_copy(const BfmCheat *c, int owner) {
    CheatStrings *s;
    BfmCheat copy;
    int r;
    if (!c || !name_ok(c->name)) return BFM_PLAT_INVALID;
    if (cheat_count >= BFM_PLAT_CHEATS_MAX) return BFM_PLAT_NO_SPACE;
    s = &cheat_strings[cheat_count];
    snprintf(s->name, sizeof s->name, "%s", c->name);
    snprintf(s->description, sizeof s->description, "%s",
             c->description ? c->description : "");
    copy = *c;
    copy.name = s->name;
    copy.description = s->description;
    r = bfm_plat_cheat_register(&copy, owner);
    return r;
}

/* Strings of a copied cheat live at cheat_strings[index]; keep both arrays
 * in step when compacting. */
void bfm_plat_cheat_drop_owned(void) {
    unsigned i, j = 0;
    for (i = 0; i < cheat_count; i++) {
        if (cheats[i].owner_mod >= 0) continue;
        if (i != j) {
            int copied = cheats[i].cheat.name == cheat_strings[i].name;
            cheats[j] = cheats[i];
            cheat_strings[j] = cheat_strings[i];
            if (copied) {
                cheats[j].cheat.name = cheat_strings[j].name;
                cheats[j].cheat.description = cheat_strings[j].description;
            }
        }
        j++;
    }
    cheat_count = j;
    if (menu_sel >= cheat_count) menu_sel = cheat_count ? cheat_count - 1 : 0;
}

int bfm_plat_cheat_set(const char *name, int enabled) {
    int i = name ? find_cheat(name) : -1;
    BfmPlatCheatState *c;
    if (i < 0) return BFM_PLAT_NOT_FOUND;
    c = &cheats[i];
    enabled = enabled != 0;
    if (c->enabled == enabled) return BFM_PLAT_OK;
    c->enabled = enabled;
    if (c->cheat.on_toggle) c->cheat.on_toggle(c->cheat.user, enabled);
    return BFM_PLAT_OK;
}

unsigned bfm_plat_cheat_count(void) { return cheat_count; }

const BfmPlatCheatState *bfm_plat_cheat_get(unsigned i) {
    return i < cheat_count ? &cheats[i] : NULL;
}

void bfm_plat_cheats_apply(void) {
    unsigned i;
    for (i = 0; i < cheat_count; i++) {
        BfmPlatCheatState *c = &cheats[i];
        if (!c->enabled) continue;
        if (c->cheat.apply) {
            c->cheat.apply(c->cheat.user);
            c->last_error = BFM_PLAT_OK;
        } else {
            uint8_t b[4];
            uint32_t v = c->cheat.value;
            b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8);
            b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
            c->last_error = bfm_plat_guest_write(c->cheat.address, b,
                                                 c->cheat.width);
        }
        if (c->last_error == BFM_PLAT_OK) c->applied_frames++;
    }
}

static void on_frame_end(void *user, BfmEvent ev, void *payload) {
    (void)user; (void)ev; (void)payload;
    bfm_plat_cheats_apply();
}

/* ---------------------------------------------------------- cheat menu */

void bfm_plat_cheat_menu_open(int open) { menu_open = open != 0; }
int bfm_plat_cheat_menu_is_open(void) { return menu_open; }

void bfm_plat_cheat_menu_move(int d) {
    long s;
    if (!cheat_count) { menu_sel = 0; return; }
    s = ((long)menu_sel + d) % (long)cheat_count;
    if (s < 0) s += (long)cheat_count;
    menu_sel = (unsigned)s;
}

unsigned bfm_plat_cheat_menu_selection(void) { return menu_sel; }

int bfm_plat_cheat_menu_toggle_selected(void) {
    if (menu_sel >= cheat_count) return BFM_PLAT_NOT_FOUND;
    return bfm_plat_cheat_set(cheats[menu_sel].cheat.name,
                              !cheats[menu_sel].enabled);
}

void bfm_plat_cheat_menu_draw(void) {
    unsigned i;
    char line[160];
    if (!menu_open) return;
    bfm_plat_renderer_overlay_text(8, 8, "CHEATS");
    for (i = 0; i < cheat_count; i++) {
        snprintf(line, sizeof line, "%c [%c] %s  %s", i == menu_sel ? '>' : ' ',
                 cheats[i].enabled ? 'x' : ' ', cheats[i].cheat.name,
                 cheats[i].cheat.description);
        bfm_plat_renderer_overlay_text(8, 24 + (int)i * 12, line);
    }
}

/* ------------------------------------------------------------ console */

int bfm_plat_console_register(const char *name, const char *help,
                              BfmCommandFn fn, void *user) {
    return bfm_plat_console_register_owned(name, help, fn, user, -1);
}

void bfm_plat_console_drop_owned(void) {
    unsigned i, j = 0;
    for (i = 0; i < command_count; i++)
        if (commands[i].owner < 0) commands[j++] = commands[i];
    command_count = j;
}

int bfm_plat_console_register_owned(const char *name, const char *help,
                                    BfmCommandFn fn, void *user, int owner) {
    unsigned i;
    if (!name_ok(name) || strlen(name) >= sizeof commands[0].name || !fn)
        return BFM_PLAT_INVALID;
    for (i = 0; i < command_count; i++)
        if (strcmp(commands[i].name, name) == 0) return BFM_PLAT_INVALID;
    if (command_count >= BFM_PLAT_COMMANDS_MAX) return BFM_PLAT_NO_SPACE;
    snprintf(commands[command_count].name, sizeof commands[0].name, "%s", name);
    commands[command_count].help = help ? help : "";
    commands[command_count].fn = fn;
    commands[command_count].user = user;
    commands[command_count].owner = owner;
    command_count++;
    return BFM_PLAT_OK;
}

static int parse_u32(const char *s, uint32_t *out) {
    long long v;
    char *end;
    if (!s || !*s) return 0;
    v = strtoll(s, &end, 0);
    if (*end || v < -2147483648LL || v > 0xFFFFFFFFLL) return 0;
    *out = (uint32_t)v;
    return 1;
}

static int cmd_help(void *u, int argc, const char *const *argv, char *out,
                    size_t n) {
    unsigned i;
    (void)u; (void)argc; (void)argv;
    reply(out, n, "");
    for (i = 0; i < command_count; i++)
        append(out, n, "%s - %s\n", commands[i].name, commands[i].help);
    return 0;
}

static int cmd_cheats(void *u, int argc, const char *const *argv, char *out,
                      size_t n) {
    unsigned i;
    (void)u; (void)argc; (void)argv;
    reply(out, n, "");
    if (!cheat_count) append(out, n, "no cheats\n");
    for (i = 0; i < cheat_count; i++)
        append(out, n, "%s %s - %s\n", cheats[i].enabled ? "[on] " : "[off]",
               cheats[i].cheat.name, cheats[i].cheat.description);
    return 0;
}

static int cmd_cheat(void *u, int argc, const char *const *argv, char *out,
                     size_t n) {
    int i, want;
    (void)u;
    if (argc != 3) { reply(out, n, "usage: cheat NAME on|off|toggle"); return 1; }
    i = find_cheat(argv[1]);
    if (i < 0) { reply(out, n, "no cheat '%s'", argv[1]); return 1; }
    if (strcmp(argv[2], "toggle") == 0) want = !cheats[i].enabled;
    else if (!bfm_plat_ini_bool(argv[2], &want)) {
        reply(out, n, "usage: cheat NAME on|off|toggle");
        return 1;
    }
    bfm_plat_cheat_set(argv[1], want);
    reply(out, n, "%s %s", argv[1], want ? "on" : "off");
    return 0;
}

static int cmd_peek(void *u, int argc, const char *const *argv, char *out,
                    size_t n) {
    uint32_t addr, count = 16, i;
    uint8_t buf[64];
    int r;
    (void)u;
    if (argc < 2 || argc > 3 || !parse_u32(argv[1], &addr) ||
        (argc == 3 && (!parse_u32(argv[2], &count) || count == 0 || count > 64))) {
        reply(out, n, "usage: peek ADDR [N<=64]");
        return 1;
    }
    r = bfm_plat_guest_read(addr, buf, count);
    if (r != BFM_PLAT_OK) { reply(out, n, "peek failed: %s", bfm_plat_result_name(r)); return 1; }
    reply(out, n, "%08x:", (unsigned)addr);
    for (i = 0; i < count; i++) append(out, n, " %02x", buf[i]);
    return 0;
}

static int cmd_poke(void *u, int argc, const char *const *argv, char *out,
                    size_t n) {
    uint32_t addr, value, width = 4;
    uint8_t b[4];
    int r;
    (void)u;
    if (argc < 3 || argc > 4 || !parse_u32(argv[1], &addr) ||
        !parse_u32(argv[2], &value) ||
        (argc == 4 && (!parse_u32(argv[3], &width) ||
                       (width != 1 && width != 2 && width != 4)))) {
        reply(out, n, "usage: poke ADDR VALUE [1|2|4]");
        return 1;
    }
    b[0] = (uint8_t)value; b[1] = (uint8_t)(value >> 8);
    b[2] = (uint8_t)(value >> 16); b[3] = (uint8_t)(value >> 24);
    r = bfm_plat_guest_write(addr, b, width);
    if (r != BFM_PLAT_OK) { reply(out, n, "poke failed: %s", bfm_plat_result_name(r)); return 1; }
    reply(out, n, "ok");
    return 0;
}

static int cmd_get(void *u, int argc, const char *const *argv, char *out,
                   size_t n) {
    const char *v;
    (void)u;
    if (argc != 2) { reply(out, n, "usage: get SECTION.KEY"); return 1; }
    v = bfm_plat_config_get(config, argv[1]);
    reply(out, n, "%s = %s", argv[1], v ? v : "(unset)");
    return 0;
}

static int cmd_set(void *u, int argc, const char *const *argv, char *out,
                   size_t n) {
    (void)u;
    if (argc != 3 || !config) { reply(out, n, "usage: set SECTION.KEY VALUE"); return 1; }
    if (bfm_plat_config_set(config, argv[1], argv[2]) != BFM_PLAT_OK) {
        reply(out, n, "rejected value for %s", argv[1]);
        return 1;
    }
    reply(out, n, "%s = %s", argv[1], argv[2]);
    return 0;
}

static int cmd_mods(void *u, int argc, const char *const *argv, char *out,
                    size_t n) {
    unsigned i;
    (void)u; (void)argc; (void)argv;
    reply(out, n, "");
    if (!bfm_plat_mods_count()) append(out, n, "no mods\n");
    for (i = 0; i < bfm_plat_mods_count(); i++) {
        const BfmPlatModInfo *m = bfm_plat_mods_get(i);
        append(out, n, "%s %s (%s) prio %d%s\n", m->name, m->version,
               m->folder, m->priority, m->enabled ? "" : " [disabled]");
    }
    return 0;
}

static int cmd_ff(void *u, int argc, const char *const *argv, char *out,
                  size_t n) {
    double speed;
    char *end;
    (void)u;
    if (argc == 2) {
        speed = strtod(argv[1], &end);
        if (*end || speed < 0.0 || speed > 64.0) { reply(out, n, "usage: ff [SPEED]"); return 1; }
        bfm_plat_timing_set_fast_forward_speed(speed);
        bfm_plat_timing_set_fast_forward(1);
    } else if (argc == 1) {
        bfm_plat_timing_set_fast_forward(!bfm_plat_timing_fast_forward());
    } else {
        reply(out, n, "usage: ff [SPEED]");
        return 1;
    }
    reply(out, n, "fast-forward %s", bfm_plat_timing_fast_forward() ? "on" : "off");
    return 0;
}

int bfm_plat_console_exec(const char *line, char *out, size_t size) {
    char buf[256];
    const char *argv[MAX_ARGS];
    int argc = 0;
    char *p;
    unsigned i;
    if (out && size) out[0] = '\0';
    if (!line || strlen(line) >= sizeof buf) { reply(out, size, "line too long"); return 1; }
    strcpy(buf, line);
    for (p = buf; *p;) {
        while (isspace((unsigned char)*p)) *p++ = '\0';
        if (!*p) break;
        if (argc == MAX_ARGS) { reply(out, size, "too many arguments"); return 1; }
        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
    }
    if (argc == 0) return 0;
    for (i = 0; i < command_count; i++)
        if (strcmp(commands[i].name, argv[0]) == 0)
            return commands[i].fn(commands[i].user, argc, argv, out, size) ? 1 : 0;
    reply(out, size, "unknown command '%s' (try help)", argv[0]);
    return 1;
}

static void register_builtins(void) {
    bfm_plat_console_register("help", "list commands", cmd_help, NULL);
    bfm_plat_console_register("cheats", "list cheats", cmd_cheats, NULL);
    bfm_plat_console_register("cheat", "cheat NAME on|off|toggle", cmd_cheat, NULL);
    bfm_plat_console_register("peek", "peek ADDR [N] - dump guest bytes", cmd_peek, NULL);
    bfm_plat_console_register("poke", "poke ADDR VALUE [W] - write guest bytes", cmd_poke, NULL);
    bfm_plat_console_register("get", "get SECTION.KEY", cmd_get, NULL);
    bfm_plat_console_register("set", "set SECTION.KEY VALUE", cmd_set, NULL);
    bfm_plat_console_register("mods", "list mods", cmd_mods, NULL);
    bfm_plat_console_register("ff", "ff [SPEED] - fast-forward", cmd_ff, NULL);
}

void bfm_plat_console_init(BfmPlatConfig *cfg) {
    config = cfg;
    if (command_count == 0) register_builtins();
    if (!frame_sub) {
        int id = bfm_plat_mods_subscribe(BFM_EVENT_FRAME_END, on_frame_end, NULL);
        frame_sub = id > 0 ? id : 0;
    }
}

void bfm_plat_console_shutdown(void) {
    if (frame_sub) bfm_plat_mods_unsubscribe(frame_sub);
    frame_sub = 0;
    config = NULL;
    menu_open = 0;
}

void bfm_plat_console_reset(void) {
    bfm_plat_console_shutdown();
    cheat_count = 0;
    command_count = 0;
    menu_sel = 0;
}
