#include "bfm_plat_input.h"
#include "bfm_plat_mods.h"

#include <stdio.h>
#include <string.h>

#define MAX_BACKENDS 8

static BfmPlatPad scripted_pads[BFM_PLAT_PAD_PORTS];
static uint32_t scripted_hotkeys;

static int scripted_open(void *self) {
    unsigned i;
    (void)self;
    for (i = 0; i < BFM_PLAT_PAD_PORTS; i++) {
        memset(&scripted_pads[i], 0, sizeof scripted_pads[i]);
        scripted_pads[i].lx = scripted_pads[i].ly = 0x80;
        scripted_pads[i].rx = scripted_pads[i].ry = 0x80;
    }
    scripted_pads[0].connected = 1;
    scripted_hotkeys = 0;
    return BFM_PLAT_OK;
}

static int scripted_poll(void *self, BfmPlatPad pads[BFM_PLAT_PAD_PORTS],
                         uint32_t *hk) {
    (void)self;
    memcpy(pads, scripted_pads, sizeof scripted_pads);
    *hk = scripted_hotkeys;
    return BFM_PLAT_OK;
}

static const BfmPlatInputBackend null_backend = {
    "null", scripted_open, NULL, scripted_poll, NULL, NULL
};

static const BfmPlatInputBackend *backends[MAX_BACKENDS] = {&null_backend};
static unsigned backend_count = 1;
static const BfmPlatInputBackend *active;
static BfmPlatPad latched[BFM_PLAT_PAD_PORTS];
static uint32_t hotkeys_now, hotkeys_prev;
static int suppressed;

#define TEXT_QUEUE 64
static BfmPlatTextEvent text_queue[TEXT_QUEUE];
static size_t text_count;

void bfm_plat_input_scripted_set(unsigned port, const BfmPlatPad *pad) {
    if (port < BFM_PLAT_PAD_PORTS && pad) scripted_pads[port] = *pad;
}

void bfm_plat_input_scripted_hotkeys(uint32_t hk) { scripted_hotkeys = hk; }

int bfm_plat_input_register(const BfmPlatInputBackend *b) {
    unsigned i;
    if (!b || !b->name || !*b->name || !b->poll) return BFM_PLAT_INVALID;
    for (i = 0; i < backend_count; i++)
        if (strcmp(backends[i]->name, b->name) == 0) return BFM_PLAT_INVALID;
    if (backend_count >= MAX_BACKENDS) return BFM_PLAT_NO_SPACE;
    backends[backend_count++] = b;
    return BFM_PLAT_OK;
}

int bfm_plat_input_open(const char *name, const char **selected) {
    const BfmPlatInputBackend *b = &null_backend;
    unsigned i;
    int r;
    bfm_plat_input_close();
    for (i = 0; name && i < backend_count; i++)
        if (strcmp(backends[i]->name, name) == 0) b = backends[i];
    r = b->open ? b->open(b->self) : BFM_PLAT_OK;
    if (r != BFM_PLAT_OK && b != &null_backend) {
        b = &null_backend;
        r = b->open(b->self);
    }
    active = b;
    memset(latched, 0, sizeof latched);
    hotkeys_now = hotkeys_prev = 0;
    if (selected) *selected = b->name;
    return r;
}

void bfm_plat_input_close(void) {
    if (active && active->close) active->close(active->self);
    active = NULL;
}

const char *bfm_plat_input_active(void) { return active ? active->name : NULL; }

int bfm_plat_input_poll(void) {
    BfmPlatPad pads[BFM_PLAT_PAD_PORTS];
    uint32_t hk = 0;
    int r;
    if (!active) return BFM_PLAT_NOT_READY;
    memset(pads, 0, sizeof pads);
    r = active->poll(active->self, pads, &hk);
    if (r != BFM_PLAT_OK) return r;
    {
        BfmEventInput ev;
        ev.pads = pads;
        ev.hotkeys = &hk;
        bfm_plat_mods_emit(BFM_EVENT_INPUT, &ev);
    }
    memcpy(latched, pads, sizeof pads);
    hotkeys_prev = hotkeys_now;
    hotkeys_now = hk;
    return BFM_PLAT_OK;
}

int bfm_plat_input_pad(unsigned port, BfmPlatPad *out) {
    if (!active) return BFM_PLAT_NOT_READY;
    if (port >= BFM_PLAT_PAD_PORTS || !out) return BFM_PLAT_INVALID;
    *out = latched[port];
    if (suppressed) {
        out->buttons = 0;
        out->lx = out->ly = out->rx = out->ry = 0x80;
    }
    return BFM_PLAT_OK;
}

void bfm_plat_input_suppress(int s) { suppressed = s != 0; }

int bfm_plat_input_push_text(const char *utf8) {
    size_t n;
    if (!utf8 || !*utf8) return BFM_PLAT_INVALID;
    n = strlen(utf8);
    if (n >= sizeof text_queue[0].text) return BFM_PLAT_INVALID;
    if (text_count >= TEXT_QUEUE) return BFM_PLAT_NO_SPACE;
    text_queue[text_count].key = BFM_KEY_NONE;
    memcpy(text_queue[text_count].text, utf8, n + 1u);
    text_count++;
    return BFM_PLAT_OK;
}

int bfm_plat_input_push_key(BfmPlatKey key) {
    if (key == BFM_KEY_NONE || key > BFM_KEY_TAB) return BFM_PLAT_INVALID;
    if (text_count >= TEXT_QUEUE) return BFM_PLAT_NO_SPACE;
    text_queue[text_count].key = (uint8_t)key;
    text_queue[text_count].text[0] = '\0';
    text_count++;
    return BFM_PLAT_OK;
}

size_t bfm_plat_input_take_events(BfmPlatTextEvent *out, size_t max) {
    size_t n = text_count < max ? text_count : max;
    if (!out) return 0;
    memcpy(out, text_queue, n * sizeof *out);
    memmove(text_queue, text_queue + n, (text_count - n) * sizeof *out);
    text_count -= n;
    return n;
}

/* ---- bindings ---- */

#define PAD_TARGETS(P, PORT, K) \
    {P ".select", PORT, 0, BFM_PAD_SELECT, K("backspace", "pad:back")}, \
    {P ".l3", PORT, 0, BFM_PAD_L3, K("c", "pad:leftstick")}, \
    {P ".r3", PORT, 0, BFM_PAD_R3, K("v", "pad:rightstick")}, \
    {P ".start", PORT, 0, BFM_PAD_START, K("return", "pad:start")}, \
    {P ".up", PORT, 0, BFM_PAD_UP, K("up", "pad:dpup")}, \
    {P ".right", PORT, 0, BFM_PAD_RIGHT, K("right", "pad:dpright")}, \
    {P ".down", PORT, 0, BFM_PAD_DOWN, K("down", "pad:dpdown")}, \
    {P ".left", PORT, 0, BFM_PAD_LEFT, K("left", "pad:dpleft")}, \
    {P ".l2", PORT, 0, BFM_PAD_L2, K("q", "pad:lefttrigger")}, \
    {P ".r2", PORT, 0, BFM_PAD_R2, K("e", "pad:righttrigger")}, \
    {P ".l1", PORT, 0, BFM_PAD_L1, K("a", "pad:leftshoulder")}, \
    {P ".r1", PORT, 0, BFM_PAD_R1, K("d", "pad:rightshoulder")}, \
    {P ".triangle", PORT, 0, BFM_PAD_TRIANGLE, K("w", "pad:y")}, \
    {P ".circle", PORT, 0, BFM_PAD_CIRCLE, K("x", "pad:b")}, \
    {P ".cross", PORT, 0, BFM_PAD_CROSS, K("z", "pad:a")}, \
    {P ".square", PORT, 0, BFM_PAD_SQUARE, K("s", "pad:x")}

/* Player 1 gets keyboard + pad defaults; player 2 pad only. */
#define KEYS1(k, pad) "key:" k ", " pad
#define KEYS2(k, pad) pad

static const BfmPlatBindingTarget targets[] = {
    PAD_TARGETS("p1", 0, KEYS1),
    PAD_TARGETS("p2", 1, KEYS2),
    {"hotkey.fast_forward", 0, 1, BFM_HOTKEY_FAST_FORWARD, "key:tab"},
    {"hotkey.console", 0, 1, BFM_HOTKEY_CONSOLE, "key:`"},
    {"hotkey.cheat_menu", 0, 1, BFM_HOTKEY_CHEAT_MENU, "key:f1"},
    {"hotkey.pause", 0, 1, BFM_HOTKEY_PAUSE, "key:pause"},
    {"hotkey.screenshot", 0, 1, BFM_HOTKEY_SCREENSHOT, "key:f12"},
    {"hotkey.quit", 0, 1, BFM_HOTKEY_QUIT, ""}
};

size_t bfm_plat_input_binding_targets(const BfmPlatBindingTarget **out) {
    if (out) *out = targets;
    return sizeof targets / sizeof targets[0];
}

static char *trim_ws(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
    return s;
}

int bfm_plat_input_bindings_from_config(const BfmPlatConfig *cfg,
                                        BfmPlatBindingResolver resolve,
                                        void *user, BfmPlatBinding *out,
                                        size_t max, size_t *count,
                                        size_t *unknown) {
    size_t t, n = 0, bad = 0;
    int full = 0;
    if (!resolve || (!out && max)) return BFM_PLAT_INVALID;
    for (t = 0; t < sizeof targets / sizeof targets[0]; t++) {
        char key[48], list[BFM_PLAT_PATH_MAX];
        const char *value;
        char *tok, *next;
        snprintf(key, sizeof key, "input.%s", targets[t].key);
        value = cfg ? bfm_plat_config_get(cfg, key) : NULL;
        if (!value) value = targets[t].default_value;
        snprintf(list, sizeof list, "%s", value);
        for (tok = list; tok; tok = next) {
            char *colon, *dev, *name;
            int32_t code;
            next = strchr(tok, ',');
            if (next) *next++ = '\0';
            tok = trim_ws(tok);
            if (!*tok) continue;
            colon = strchr(tok, ':');
            if (!colon) { bad++; continue; }
            *colon = '\0';
            dev = trim_ws(tok);
            name = trim_ws(colon + 1);
            code = resolve(user, dev, name);
            if (code < 0) { bad++; continue; }
            if (n >= max) { full = 1; continue; }
            out[n].host_code = code;
            out[n].port = targets[t].port;
            out[n].is_hotkey = targets[t].is_hotkey;
            out[n].mask = targets[t].mask;
            n++;
        }
    }
    if (count) *count = n;
    if (unknown) *unknown = bad;
    return full ? BFM_PLAT_NO_SPACE : BFM_PLAT_OK;
}

uint32_t bfm_plat_input_hotkeys(void) { return hotkeys_now; }
uint32_t bfm_plat_input_hotkeys_pressed(void) {
    return hotkeys_now & ~hotkeys_prev;
}

int bfm_plat_input_set_bindings(const BfmPlatBinding *b, size_t n) {
    if (!active) return BFM_PLAT_NOT_READY;
    if (n && !b) return BFM_PLAT_INVALID;
    return active->set_bindings ? active->set_bindings(active->self, b, n)
                                : BFM_PLAT_UNSUPPORTED;
}

size_t bfm_plat_pad_to_wire(const BfmPlatPad *pad, uint8_t out[8]) {
    uint16_t low;
    if (!pad || !out || !pad->connected) return 0;
    low = (uint16_t)~pad->buttons;
    out[0] = pad->analog ? 0x73 : 0x41;
    out[1] = 0x5A;
    out[2] = (uint8_t)(low & 0xFFu);
    out[3] = (uint8_t)(low >> 8);
    if (!pad->analog) return 4;
    out[4] = pad->rx;
    out[5] = pad->ry;
    out[6] = pad->lx;
    out[7] = pad->ly;
    return 8;
}

void bfm_plat_input_reset(void) {
    bfm_plat_input_close();
    backend_count = 1;
    memset(scripted_pads, 0, sizeof scripted_pads);
    scripted_hotkeys = 0;
    memset(latched, 0, sizeof latched);
    hotkeys_now = hotkeys_prev = 0;
    suppressed = 0;
    text_count = 0;
}
