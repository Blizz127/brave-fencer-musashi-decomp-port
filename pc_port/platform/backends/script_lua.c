/* Lua script runtime for mods (optional, BFM_PLAT_WITH_LUA).
 *
 * Lua 5.4 (MIT, Lua.org / PUC-Rio) is fetched by tools/fetch_lua.sh into
 * tools/third_party/lua and never committed. A mod whose manifest says
 * `script = main.lua` gets its own sandboxed lua_State. Scripts see the same
 * host services as a C plugin (bfm_plugin.h), as a global table `bfm`:
 *
 *   bfm.mod_name, bfm.mod_dir, bfm.abi_version
 *   bfm.log(msg [, level])
 *   id = bfm.on(event, fn)      event = name ("frame_end") or number;
 *                               fn(event_name, payload_table_or_nil)
 *   bfm.off(id)
 *   bfm.read8/16/32(addr), bfm.write8/16/32(addr, value)
 *   s = bfm.read(addr, n), bfm.write(addr, s)          guest RAM
 *   bfm.cheat{name=, description=, address=, value=, width=}   poke cheat
 *   bfm.cheat{name=, description=, apply=fn, on_toggle=fn}     callback cheat
 *   bfm.set_cheat(name, on)
 *   bfm.command(name, help, fn)   fn(arg1, ...) returns the reply string
 *   bfm.config(key)               "section.key" or nil
 *   id = bfm.watch(addr, len, fn(addr, old, new)), bfm.unwatch(id)
 *   bfm.PAD_* / bfm.HOTKEY_*      button and hotkey masks
 *
 * The "input" payload is {pads = {{buttons=, lx=, ly=, rx=, ry=,
 * connected=, analog=}, ...}, hotkeys = n}. Changes to buttons, sticks
 * and hotkeys are written back. Other payloads are plain tables (room: area,
 * room; battle: encounter, result; item: item, count; save: slot;
 * projection: h, ofx, ofy).
 *
 * Sandbox: base (without dofile/loadfile/load/require), table, string,
 * math, utf8 and coroutine only. No io, os, package or debug. Each load and
 * each callback runs under an instruction budget (BFM_LUA_BUDGET), so a
 * runaway script errors out instead of hanging the game. Cheats and
 * commands may only be registered while the script's main chunk runs,
 * which keeps their ownership with the mod. */
#include "../bfm_plat.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BFM_LUA_BUDGET 5000000     /* VM instructions per load / callback */
#define BFM_LUA_MAX_STATES 16
#define BFM_LUA_MAX_HOOKS 256

typedef struct LuaMod LuaMod;

typedef struct LuaHook {
    LuaMod *mod;
    int ref;                        /* registry ref of the function */
    int kind;                       /* 0 event, 1 cheat apply, 2 toggle, 3 command */
    int sub;                        /* subscription id for events */
    char name[64];                  /* cheat/command name storage */
    char description[128];
    BfmCheat cheat;
} LuaHook;

struct LuaMod {
    lua_State *L;
    const BfmPluginHost *host;
    int loading;
    int errors;
};

static LuaMod mods_[BFM_LUA_MAX_STATES];
static unsigned mod_count;
static LuaHook *hooks[BFM_LUA_MAX_HOOKS];
static unsigned hook_count;

static const char *const event_names[BFM_EVENT_COUNT] = {
    "boot", "shutdown", "frame_begin", "frame_end", "vsync", "input",
    "before_present", "after_present", "room_enter", "room_exit",
    "battle_start", "battle_end", "item_get", "save", "load", "projection",
    "damage", "bp_use", "money", "item_use"
};

static LuaMod *self_of(lua_State *L) {
    return (LuaMod *)lua_touserdata(L, lua_upvalueindex(1));
}

static void budget_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    luaL_error(L, "instruction budget exceeded");
}

static void arm_budget(lua_State *L) {
    lua_sethook(L, budget_hook, LUA_MASKCOUNT, BFM_LUA_BUDGET);
}

static void report(LuaMod *m, const char *what) {
    char msg[512];
    snprintf(msg, sizeof msg, "lua %s: %s", what, lua_tostring(m->L, -1));
    m->host->log(m->host->mod_name, 1, msg);
    lua_pop(m->L, 1);
    m->errors++;
}

/* Calls the function at the top-of-stack slot with nargs args. */
static int call(LuaMod *m, int nargs, int nres, const char *what) {
    arm_budget(m->L);
    if (lua_pcall(m->L, nargs, nres, 0) != LUA_OK) {
        report(m, what);
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------- payloads */

static void set_int(lua_State *L, const char *k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}

static lua_Integer get_int(lua_State *L, int t, const char *k, lua_Integer def) {
    lua_Integer v = def;
    lua_getfield(L, t, k);
    if (lua_isinteger(L, -1)) v = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return v;
}

static void push_payload(lua_State *L, BfmEvent e, void *p) {
    if (!p) { lua_pushnil(L); return; }
    lua_newtable(L);
    switch (e) {
    case BFM_EVENT_INPUT: {
        BfmEventInput *in = (BfmEventInput *)p;
        unsigned i;
        lua_newtable(L);
        for (i = 0; i < BFM_PLAT_PAD_PORTS; i++) {
            lua_newtable(L);
            set_int(L, "buttons", in->pads[i].buttons);
            set_int(L, "lx", in->pads[i].lx);
            set_int(L, "ly", in->pads[i].ly);
            set_int(L, "rx", in->pads[i].rx);
            set_int(L, "ry", in->pads[i].ry);
            set_int(L, "connected", in->pads[i].connected);
            set_int(L, "analog", in->pads[i].analog);
            lua_rawseti(L, -2, (lua_Integer)i + 1);
        }
        lua_setfield(L, -2, "pads");
        set_int(L, "hotkeys", *in->hotkeys);
        break;
    }
    case BFM_EVENT_ROOM_ENTER:
    case BFM_EVENT_ROOM_EXIT:
        set_int(L, "area", ((BfmEventRoom *)p)->area);
        set_int(L, "room", ((BfmEventRoom *)p)->room);
        set_int(L, "location", ((BfmEventRoom *)p)->location);
        break;
    case BFM_EVENT_BATTLE_START:
    case BFM_EVENT_BATTLE_END:
        set_int(L, "encounter", ((BfmEventBattle *)p)->encounter);
        set_int(L, "result", ((BfmEventBattle *)p)->result);
        break;
    case BFM_EVENT_ITEM_GET:
    case BFM_EVENT_ITEM_USE:
        set_int(L, "item", ((BfmEventItem *)p)->item);
        set_int(L, "count", ((BfmEventItem *)p)->count);
        set_int(L, "kind", ((BfmEventItem *)p)->kind);
        set_int(L, "derived", (((BfmEventItem *)p)->flags & BFM_EVENT_FLAG_DERIVED) != 0);
        break;
    case BFM_EVENT_DAMAGE:
    case BFM_EVENT_BP_USE:
    case BFM_EVENT_MONEY:
        set_int(L, "value", ((BfmEventStat *)p)->value);
        set_int(L, "previous", ((BfmEventStat *)p)->previous);
        set_int(L, "max", ((BfmEventStat *)p)->max);
        set_int(L, "derived", (((BfmEventStat *)p)->flags & BFM_EVENT_FLAG_DERIVED) != 0);
        break;
    case BFM_EVENT_SAVE:
    case BFM_EVENT_LOAD:
        set_int(L, "slot", ((BfmEventSave *)p)->slot);
        break;
    case BFM_EVENT_PROJECTION:
        set_int(L, "h", ((BfmEventProjection *)p)->h);
        set_int(L, "ofx", ((BfmEventProjection *)p)->ofx);
        set_int(L, "ofy", ((BfmEventProjection *)p)->ofy);
        break;
    default:
        break;
    }
}

static void pull_input(lua_State *L, int t, BfmEventInput *in) {
    unsigned i;
    in->hotkeys[0] = (uint32_t)get_int(L, t, "hotkeys", in->hotkeys[0]);
    lua_getfield(L, t, "pads");
    if (lua_istable(L, -1)) {
        for (i = 0; i < BFM_PLAT_PAD_PORTS; i++) {
            BfmPlatPad *pad = &in->pads[i];
            lua_rawgeti(L, -1, (lua_Integer)i + 1);
            if (lua_istable(L, -1)) {
                int pt = lua_gettop(L);
                pad->buttons = (uint16_t)get_int(L, pt, "buttons", pad->buttons);
                pad->lx = (uint8_t)get_int(L, pt, "lx", pad->lx);
                pad->ly = (uint8_t)get_int(L, pt, "ly", pad->ly);
                pad->rx = (uint8_t)get_int(L, pt, "rx", pad->rx);
                pad->ry = (uint8_t)get_int(L, pt, "ry", pad->ry);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

static void on_event(void *user, BfmEvent e, void *payload) {
    LuaHook *h = (LuaHook *)user;
    lua_State *L = h->mod->L;
    int base;
    if (!L) return;
    base = lua_gettop(L);
    push_payload(L, e, payload);                   /* base+1: kept for write-back */
    lua_rawgeti(L, LUA_REGISTRYINDEX, h->ref);
    lua_pushstring(L, event_names[e]);
    lua_pushvalue(L, base + 1);
    if (call(h->mod, 2, 0, event_names[e]) && e == BFM_EVENT_INPUT && payload)
        pull_input(L, base + 1, (BfmEventInput *)payload);
    lua_settop(L, base);
}

/* ---------------------------------------------------------- bfm.* */

static LuaHook *new_hook(LuaMod *m, int kind) {
    LuaHook *h;
    if (hook_count >= BFM_LUA_MAX_HOOKS) return NULL;
    h = (LuaHook *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->mod = m;
    h->kind = kind;
    h->ref = LUA_NOREF;
    hooks[hook_count++] = h;
    return h;
}

static int l_log(lua_State *L) {
    LuaMod *m = self_of(L);
    const char *s = luaL_checkstring(L, 1);
    int level = (int)luaL_optinteger(L, 2, 2);
    m->host->log(m->host->mod_name, level, s);
    return 0;
}

static int event_arg(lua_State *L, int idx) {
    if (lua_type(L, idx) == LUA_TNUMBER) {
        lua_Integer v = lua_tointeger(L, idx);
        if (v >= 0 && v < BFM_EVENT_COUNT) return (int)v;
    } else if (lua_type(L, idx) == LUA_TSTRING) {
        int i;
        const char *s = lua_tostring(L, idx);
        for (i = 0; i < BFM_EVENT_COUNT; i++)
            if (strcmp(s, event_names[i]) == 0) return i;
    }
    return luaL_argerror(L, idx, "unknown event");
}

static int l_on(lua_State *L) {
    LuaMod *m = self_of(L);
    int ev = event_arg(L, 1);
    LuaHook *h;
    int id;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    h = new_hook(m, 0);
    if (!h) return luaL_error(L, "too many hooks");
    lua_pushvalue(L, 2);
    h->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    id = m->host->subscribe((BfmEvent)ev, on_event, h);
    if (id <= 0) return luaL_error(L, "subscribe failed");
    h->sub = id;
    lua_pushinteger(L, id);
    return 1;
}

static int l_off(lua_State *L) {
    LuaMod *m = self_of(L);
    lua_pushboolean(L, m->host->unsubscribe((int)luaL_checkinteger(L, 1)) == BFM_PLAT_OK);
    return 1;
}

static int read_n(lua_State *L, uint32_t n) {
    LuaMod *m = self_of(L);
    uint8_t b[4];
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1), v = 0, i;
    if (m->host->guest_read(addr, b, n) != BFM_PLAT_OK) {
        lua_pushnil(L);
        return 1;
    }
    for (i = 0; i < n; i++) v |= (uint32_t)b[i] << (8u * i);
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

static int write_n(lua_State *L, uint32_t n) {
    LuaMod *m = self_of(L);
    uint8_t b[4];
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    uint32_t v = (uint32_t)luaL_checkinteger(L, 2), i;
    for (i = 0; i < n; i++) b[i] = (uint8_t)(v >> (8u * i));
    lua_pushboolean(L, m->host->guest_write(addr, b, n) == BFM_PLAT_OK);
    return 1;
}

static int l_read8(lua_State *L) { return read_n(L, 1); }
static int l_read16(lua_State *L) { return read_n(L, 2); }
static int l_read32(lua_State *L) { return read_n(L, 4); }
static int l_write8(lua_State *L) { return write_n(L, 1); }
static int l_write16(lua_State *L) { return write_n(L, 2); }
static int l_write32(lua_State *L) { return write_n(L, 4); }

static int l_read(lua_State *L) {
    LuaMod *m = self_of(L);
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_Integer n = luaL_checkinteger(L, 2);
    luaL_Buffer buf;
    char *p;
    if (n < 0 || n > 65536) return luaL_argerror(L, 2, "0..65536");
    p = luaL_buffinitsize(L, &buf, (size_t)n);
    if (m->host->guest_read(addr, p, (uint32_t)n) != BFM_PLAT_OK) {
        lua_pushnil(L);
        return 1;
    }
    luaL_pushresultsize(&buf, (size_t)n);
    return 1;
}

static int l_write(lua_State *L) {
    LuaMod *m = self_of(L);
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    size_t n;
    const char *s = luaL_checklstring(L, 2, &n);
    lua_pushboolean(L, m->host->guest_write(addr, s, (uint32_t)n) == BFM_PLAT_OK);
    return 1;
}

static void cheat_apply(void *user) {
    LuaHook *h = (LuaHook *)user;
    lua_rawgeti(h->mod->L, LUA_REGISTRYINDEX, h->ref);
    call(h->mod, 0, 0, "cheat apply");
}

static int toggle_ref_of(LuaHook *h) { return (int)h->cheat.value; }

static void cheat_toggle(void *user, int on) {
    LuaHook *h = (LuaHook *)user;
    if (toggle_ref_of(h) == LUA_NOREF) return;
    lua_rawgeti(h->mod->L, LUA_REGISTRYINDEX, toggle_ref_of(h));
    lua_pushboolean(h->mod->L, on);
    call(h->mod, 1, 0, "cheat toggle");
}

static int l_cheat(lua_State *L) {
    LuaMod *m = self_of(L);
    LuaHook *h;
    const char *name, *desc;
    if (!m->loading) return luaL_error(L, "bfm.cheat is only allowed while the script loads");
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "name");
    name = luaL_checkstring(L, -1);
    lua_getfield(L, 1, "description");
    desc = luaL_optstring(L, -1, "");
    h = new_hook(m, 1);
    if (!h) return luaL_error(L, "too many hooks");
    snprintf(h->name, sizeof h->name, "%s", name);
    snprintf(h->description, sizeof h->description, "%s", desc);
    lua_pop(L, 2);
    h->cheat.name = h->name;
    h->cheat.description = h->description;
    lua_getfield(L, 1, "apply");
    if (lua_isfunction(L, -1)) {
        h->ref = luaL_ref(L, LUA_REGISTRYINDEX);
        h->cheat.apply = cheat_apply;
        h->cheat.user = h;
        /* on_toggle ref kept in .value (unused for callback cheats) */
        lua_getfield(L, 1, "on_toggle");
        if (lua_isfunction(L, -1)) {
            h->cheat.value = (uint32_t)luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            lua_pop(L, 1);
            h->cheat.value = (uint32_t)LUA_NOREF;
        }
        h->cheat.on_toggle = cheat_toggle;
    } else {
        lua_pop(L, 1);
        h->cheat.address = (uint32_t)get_int(L, 1, "address", 0);
        h->cheat.value = (uint32_t)get_int(L, 1, "value", 0);
        h->cheat.width = (uint8_t)get_int(L, 1, "width", 4);
    }
    if (m->host->register_cheat(&h->cheat) != BFM_PLAT_OK)
        return luaL_error(L, "cheat '%s' refused", h->name);
    return 0;
}

static int l_set_cheat(lua_State *L) {
    LuaMod *m = self_of(L);
    lua_pushboolean(L, m->host->set_cheat(luaL_checkstring(L, 1), lua_toboolean(L, 2)) == BFM_PLAT_OK);
    return 1;
}

static int command_fn(void *user, int argc, const char *const *argv, char *out,
                      size_t size) {
    LuaHook *h = (LuaHook *)user;
    lua_State *L = h->mod->L;
    int i, ok;
    lua_rawgeti(L, LUA_REGISTRYINDEX, h->ref);
    for (i = 1; i < argc; i++) lua_pushstring(L, argv[i]);
    ok = call(h->mod, argc - 1, 1, h->name);
    if (!ok) {
        snprintf(out, size, "%s: script error", h->name);
        return 1;
    }
    snprintf(out, size, "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return 0;
}

static int l_command(lua_State *L) {
    LuaMod *m = self_of(L);
    LuaHook *h;
    if (!m->loading) return luaL_error(L, "bfm.command is only allowed while the script loads");
    luaL_checkstring(L, 1);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    h = new_hook(m, 3);
    if (!h) return luaL_error(L, "too many hooks");
    snprintf(h->name, sizeof h->name, "%s", lua_tostring(L, 1));
    snprintf(h->description, sizeof h->description, "%s", luaL_optstring(L, 2, ""));
    lua_pushvalue(L, 3);
    h->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (m->host->register_command(h->name, h->description, command_fn, h) != BFM_PLAT_OK)
        return luaL_error(L, "command '%s' refused", h->name);
    return 0;
}

static void on_watch(void *user, uint32_t addr, uint32_t len,
                     const uint8_t *o, const uint8_t *n) {
    LuaHook *h = (LuaHook *)user;
    lua_State *L = h->mod->L;
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, h->ref);
    lua_pushinteger(L, (lua_Integer)addr);
    lua_pushlstring(L, (const char *)o, len);
    lua_pushlstring(L, (const char *)n, len);
    call(h->mod, 3, 0, "watch");
}

/* id = bfm.watch(addr, len, fn(addr, old, new)) -- old/new are byte strings */
static int l_watch(lua_State *L) {
    LuaMod *m = self_of(L);
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_Integer len = luaL_checkinteger(L, 2);
    LuaHook *h;
    int id;
    luaL_checktype(L, 3, LUA_TFUNCTION);
    if (m->host->abi_version < 2 || !m->host->watch_add) return luaL_error(L, "no watch support");
    h = new_hook(m, 4);
    if (!h) return luaL_error(L, "too many hooks");
    lua_pushvalue(L, 3);
    h->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    id = m->host->watch_add(addr, (uint32_t)len, on_watch, h);
    if (id <= 0) return luaL_error(L, "watch refused");
    lua_pushinteger(L, id);
    return 1;
}

static int l_unwatch(lua_State *L) {
    LuaMod *m = self_of(L);
    lua_pushboolean(L, m->host->watch_remove((int)luaL_checkinteger(L, 1)) == BFM_PLAT_OK);
    return 1;
}

static int l_config(lua_State *L) {
    LuaMod *m = self_of(L);
    const char *v = m->host->config_get(luaL_checkstring(L, 1));
    if (v) lua_pushstring(L, v); else lua_pushnil(L);
    return 1;
}

static void open_sandbox(lua_State *L) {
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine},
        {NULL, NULL}
    };
    static const char *const unsafe[] = {"dofile", "loadfile", "load", "require",
                                         "collectgarbage", NULL};
    const luaL_Reg *lib;
    int i;
    for (lib = libs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    for (i = 0; unsafe[i]; i++) {
        lua_pushnil(L);
        lua_setglobal(L, unsafe[i]);
    }
}

static void push_api(lua_State *L, LuaMod *m) {
    static const luaL_Reg fns[] = {
        {"log", l_log}, {"on", l_on}, {"off", l_off},
        {"read8", l_read8}, {"read16", l_read16}, {"read32", l_read32},
        {"write8", l_write8}, {"write16", l_write16}, {"write32", l_write32},
        {"read", l_read}, {"write", l_write}, {"cheat", l_cheat},
        {"set_cheat", l_set_cheat}, {"command", l_command}, {"config", l_config},
        {"watch", l_watch}, {"unwatch", l_unwatch},
        {NULL, NULL}
    };
    static const struct { const char *name; lua_Integer v; } consts[] = {
        {"PAD_SELECT", BFM_PAD_SELECT}, {"PAD_L3", BFM_PAD_L3}, {"PAD_R3", BFM_PAD_R3},
        {"PAD_START", BFM_PAD_START}, {"PAD_UP", BFM_PAD_UP}, {"PAD_RIGHT", BFM_PAD_RIGHT},
        {"PAD_DOWN", BFM_PAD_DOWN}, {"PAD_LEFT", BFM_PAD_LEFT}, {"PAD_L2", BFM_PAD_L2},
        {"PAD_R2", BFM_PAD_R2}, {"PAD_L1", BFM_PAD_L1}, {"PAD_R1", BFM_PAD_R1},
        {"PAD_TRIANGLE", BFM_PAD_TRIANGLE}, {"PAD_CIRCLE", BFM_PAD_CIRCLE},
        {"PAD_CROSS", BFM_PAD_CROSS}, {"PAD_SQUARE", BFM_PAD_SQUARE},
        {"HOTKEY_FAST_FORWARD", BFM_HOTKEY_FAST_FORWARD}, {"HOTKEY_CONSOLE", BFM_HOTKEY_CONSOLE},
        {"HOTKEY_CHEAT_MENU", BFM_HOTKEY_CHEAT_MENU}, {"HOTKEY_PAUSE", BFM_HOTKEY_PAUSE},
        {"HOTKEY_SCREENSHOT", BFM_HOTKEY_SCREENSHOT}, {"HOTKEY_QUIT", BFM_HOTKEY_QUIT},
        {NULL, 0}
    };
    int i;
    lua_newtable(L);
    lua_pushlightuserdata(L, m);
    luaL_setfuncs(L, fns, 1);
    for (i = 0; consts[i].name; i++) set_int(L, consts[i].name, consts[i].v);
    lua_pushstring(L, m->host->mod_name);
    lua_setfield(L, -2, "mod_name");
    lua_pushstring(L, m->host->mod_dir);
    lua_setfield(L, -2, "mod_dir");
    set_int(L, "abi_version", m->host->abi_version);
    lua_setglobal(L, "bfm");
}

static int lua_load_script(const BfmPluginHost *host, const char *path) {
    LuaMod *m;
    int ok;
    if (mod_count >= BFM_LUA_MAX_STATES) return BFM_PLAT_NO_SPACE;
    m = &mods_[mod_count];
    memset(m, 0, sizeof *m);
    m->host = host;
    m->L = luaL_newstate();
    if (!m->L) return BFM_PLAT_ERROR;
    mod_count++;
    open_sandbox(m->L);
    push_api(m->L, m);
    if (luaL_loadfilex(m->L, path, "t") != LUA_OK) {   /* text only, no bytecode */
        report(m, "load");
        return BFM_PLAT_INVALID;
    }
    m->loading = 1;
    ok = call(m, 0, 0, "main chunk");
    m->loading = 0;
    return ok ? BFM_PLAT_OK : BFM_PLAT_ERROR;
}

static void lua_shutdown(void) {
    unsigned i;
    for (i = 0; i < mod_count; i++) {
        if (mods_[i].L) lua_close(mods_[i].L);
        mods_[i].L = NULL;
    }
    mod_count = 0;
    for (i = 0; i < hook_count; i++) free(hooks[i]);
    hook_count = 0;
}

static const BfmPlatScriptRuntime lua_runtime = {
    "lua", ".lua", lua_load_script, lua_shutdown
};

int bfm_plat_script_lua_register(void);
int bfm_plat_script_lua_register(void) {
    return bfm_plat_mods_register_script_runtime(&lua_runtime);
}
