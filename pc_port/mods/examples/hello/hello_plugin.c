/* Sample Brave Fencer Musashi port plugin.
 *
 * Build (Linux):
 *   cc -std=c99 -shared -fPIC -I pc_port/platform \
 *      pc_port/mods/examples/hello/hello_plugin.c \
 *      -o mods/hello/hello_plugin.so
 *
 * It only includes bfm_plugin.h (the plugin SDK) and touches the game only
 * through the host table. */
#include "bfm_plugin.h"

#include <stdio.h>

static const BfmPluginHost *host;
static unsigned long frames;
static unsigned long rooms;
static int turbo;

static void on_frame(void *user, BfmEvent ev, void *payload) {
    (void)user; (void)ev; (void)payload;
    frames++;
}

static void on_room(void *user, BfmEvent ev, void *payload) {
    const BfmEventRoom *r = (const BfmEventRoom *)payload;
    char msg[96];
    (void)user; (void)ev;
    rooms++;
    if (r) {
        snprintf(msg, sizeof msg, "entered area %u room %u",
                 (unsigned)r->area, (unsigned)r->room);
        host->log(host->mod_name, 2, msg);
    }
}

/* Turbo: while the cheat is on, a held Cross is pressed on alternate frames. */
static void on_input(void *user, BfmEvent ev, void *payload) {
    BfmEventInput *in = (BfmEventInput *)payload;
    (void)user; (void)ev;
    if (turbo && in && (in->pads[0].buttons & BFM_PAD_CROSS) && (frames & 1u))
        in->pads[0].buttons &= (uint16_t)~BFM_PAD_CROSS;
}

static void turbo_apply(void *user) { (void)user; }
static void turbo_toggle(void *user, int on) { (void)user; turbo = on; }

static const BfmCheat turbo_cheat = {
    "turbo_cross", "Hold Cross to auto-fire it", 0, 0, 0,
    turbo_apply, turbo_toggle, NULL
};

static int cmd_hello(void *user, int argc, const char *const *argv, char *out,
                     size_t size) {
    const char *greeting = host->config_get("mod.hello.greeting");
    (void)user; (void)argc; (void)argv;
    snprintf(out, size, "%s: %lu frames, %lu rooms",
             greeting ? greeting : "hello", frames, rooms);
    return 0;
}

BFM_PLUGIN_EXPORT int bfm_plugin_init(const BfmPluginHost *h,
                                      BfmPluginInfo *info);
BFM_PLUGIN_EXPORT void bfm_plugin_shutdown(void);

BFM_PLUGIN_EXPORT int bfm_plugin_init(const BfmPluginHost *h,
                                      BfmPluginInfo *info) {
    if (!h || h->abi_version < 1u) return -1;
    host = h;
    frames = rooms = 0;
    turbo = 0;
    info->abi_version = BFM_PLUGIN_ABI_VERSION;
    info->name = "hello";
    info->version = "1.0.0";
    if (h->subscribe(BFM_EVENT_FRAME_END, on_frame, NULL) <= 0 ||
        h->subscribe(BFM_EVENT_ROOM_ENTER, on_room, NULL) <= 0 ||
        h->subscribe(BFM_EVENT_INPUT, on_input, NULL) <= 0)
        return -1;
    h->register_cheat(&turbo_cheat);
    h->register_command("hello", "hello - sample plugin status", cmd_hello, NULL);
    h->log(h->mod_name, 2, "hello plugin loaded");
    return 0;
}

BFM_PLUGIN_EXPORT void bfm_plugin_shutdown(void) {
    if (host) host->log(host->mod_name, 2, "hello plugin unloaded");
    host = NULL;
}
