/* "sdl" input backend: keyboard + SDL GameController -> bfm_plat input.
 *
 * Bindings come from the user config's [input] section (see
 * bfm_plat_input.h); the resolver below maps names to host codes:
 *   key:NAME   SDL scancode name (SDL_GetScancodeFromName, case-insensitive)
 *   pad:NAME   SDL_GameControllerButton name ("a", "dpup", "start", ...)
 *   pad:AXIS / pad:+AXIS / pad:-AXIS   axis past half travel ("lefttrigger",
 *              "-leftx", ...)
 * Port N's pad bindings read the Nth connected game controller.
 * [input] analog = 1 reports DualShock analog mode with the sticks.
 *
 * PsyCross owns the window and pumps SDL's event queue, so this backend
 * never removes events: it reads keyboard/controller *state* after
 * SDL_PumpEvents, and gets typed text and hotplug through an event watch.
 * While the console is open, SDL text input is started and editing keys go
 * to the console queue.
 *
 * Build: -DBFM_PLAT_WITH_SDL_INPUT, `sdl2-config --cflags --libs`.
 * Status: compile-checked; runs once SDL2 is installed. */
#include "../bfm_plat.h"
#include "../bfm_plat_console_ui.h"
#include "../bfm_plat_ini.h"

#include <SDL.h>

#include <stdlib.h>
#include <string.h>

#define CODE_KIND(c) ((c) & 0x70000)
#define CODE_KEY 0x00000
#define CODE_BUTTON 0x10000
#define CODE_AXIS_POS 0x20000
#define CODE_AXIS_NEG 0x30000
#define CODE_INDEX(c) ((c) & 0xFFFF)
#define AXIS_THRESHOLD 16384
#define MAX_BINDINGS 128

typedef struct SdlInput {
    SDL_GameController *pads[BFM_PLAT_PAD_PORTS];
    BfmPlatBinding bindings[MAX_BINDINGS];
    size_t binding_count;
    int analog;
    volatile int rescan;
    int text_active;
} SdlInput;

static SdlInput state;

static int32_t resolve(void *user, const char *device, const char *name) {
    (void)user;
    if (strcmp(device, "key") == 0) {
        SDL_Scancode sc = SDL_GetScancodeFromName(name);
        return sc == SDL_SCANCODE_UNKNOWN ? -1 : (int32_t)(CODE_KEY | sc);
    }
    if (strcmp(device, "pad") == 0) {
        int kind = CODE_AXIS_POS;
        SDL_GameControllerButton b;
        SDL_GameControllerAxis a;
        if (*name == '+' || *name == '-') {
            kind = *name == '-' ? CODE_AXIS_NEG : CODE_AXIS_POS;
            name++;
        } else {
            b = SDL_GameControllerGetButtonFromString(name);
            if (b != SDL_CONTROLLER_BUTTON_INVALID) return (int32_t)(CODE_BUTTON | b);
        }
        a = SDL_GameControllerGetAxisFromString(name);
        if (a != SDL_CONTROLLER_AXIS_INVALID) return (int32_t)(kind | a);
    }
    return -1;
}

static void close_pads(SdlInput *s) {
    unsigned i;
    for (i = 0; i < BFM_PLAT_PAD_PORTS; i++) {
        if (s->pads[i]) SDL_GameControllerClose(s->pads[i]);
        s->pads[i] = NULL;
    }
}

static void scan_pads(SdlInput *s) {
    int j, n = SDL_NumJoysticks();
    unsigned port = 0;
    close_pads(s);
    for (j = 0; j < n && port < BFM_PLAT_PAD_PORTS; j++) {
        if (!SDL_IsGameController(j)) continue;
        s->pads[port] = SDL_GameControllerOpen(j);
        if (s->pads[port]) port++;
    }
}

static int watch(void *user, SDL_Event *e) {
    SdlInput *s = (SdlInput *)user;
    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
        s->rescan = 1;
        break;
    case SDL_TEXTINPUT:
        if (s->text_active) bfm_plat_input_push_text(e->text.text);
        break;
    case SDL_KEYDOWN:
        if (!s->text_active) break;
        switch (e->key.keysym.sym) {
        case SDLK_RETURN: case SDLK_KP_ENTER: bfm_plat_input_push_key(BFM_KEY_ENTER); break;
        case SDLK_BACKSPACE: bfm_plat_input_push_key(BFM_KEY_BACKSPACE); break;
        case SDLK_ESCAPE: bfm_plat_input_push_key(BFM_KEY_ESCAPE); break;
        case SDLK_UP: bfm_plat_input_push_key(BFM_KEY_UP); break;
        case SDLK_DOWN: bfm_plat_input_push_key(BFM_KEY_DOWN); break;
        case SDLK_LEFT: bfm_plat_input_push_key(BFM_KEY_LEFT); break;
        case SDLK_RIGHT: bfm_plat_input_push_key(BFM_KEY_RIGHT); break;
        case SDLK_PAGEUP: bfm_plat_input_push_key(BFM_KEY_PAGE_UP); break;
        case SDLK_PAGEDOWN: bfm_plat_input_push_key(BFM_KEY_PAGE_DOWN); break;
        case SDLK_TAB: bfm_plat_input_push_key(BFM_KEY_TAB); break;
        default: break;
        }
        break;
    default:
        break;
    }
    return 0;
}

static int sdl_open(void *self) {
    SdlInput *s = (SdlInput *)self;
    const BfmPlatConfig *cfg = bfm_plat_config();
    const char *analog = bfm_plat_config_get(cfg, "input.analog");
    memset(s, 0, sizeof *s);
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
        return BFM_PLAT_ERROR;
    if (analog) bfm_plat_ini_bool(analog, &s->analog);
    bfm_plat_input_bindings_from_config(cfg, resolve, NULL, s->bindings,
                                        MAX_BINDINGS, &s->binding_count, NULL);
    SDL_AddEventWatch(watch, s);
    scan_pads(s);
    return BFM_PLAT_OK;
}

static void sdl_close(void *self) {
    SdlInput *s = (SdlInput *)self;
    SDL_DelEventWatch(watch, s);
    if (s->text_active) SDL_StopTextInput();
    close_pads(s);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
}

static int active_code(SdlInput *s, const Uint8 *keys, int nkeys,
                       const BfmPlatBinding *b) {
    int32_t c = b->host_code;
    SDL_GameController *pad;
    if (CODE_KIND(c) == CODE_KEY)
        return CODE_INDEX(c) < nkeys && keys[CODE_INDEX(c)];
    pad = s->pads[b->is_hotkey ? 0 : b->port];
    if (!pad) return 0;
    if (CODE_KIND(c) == CODE_BUTTON)
        return SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)CODE_INDEX(c));
    {
        Sint16 v = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)CODE_INDEX(c));
        return CODE_KIND(c) == CODE_AXIS_NEG ? v < -AXIS_THRESHOLD : v > AXIS_THRESHOLD;
    }
}

static uint8_t stick(SDL_GameController *pad, SDL_GameControllerAxis a) {
    return (uint8_t)((SDL_GameControllerGetAxis(pad, a) + 32768) >> 8);
}

static int sdl_poll(void *self, BfmPlatPad pads[BFM_PLAT_PAD_PORTS],
                    uint32_t *hotkeys) {
    SdlInput *s = (SdlInput *)self;
    const Uint8 *keys;
    int nkeys = 0;
    size_t i;
    unsigned p;
    int want_text = bfm_plat_console_ui_is_open();
    if (want_text != s->text_active) {
        if (want_text) SDL_StartTextInput(); else SDL_StopTextInput();
        s->text_active = want_text;
    }
    SDL_PumpEvents();
    if (s->rescan) {
        s->rescan = 0;
        scan_pads(s);
    }
    keys = SDL_GetKeyboardState(&nkeys);
    for (p = 0; p < BFM_PLAT_PAD_PORTS; p++) {
        memset(&pads[p], 0, sizeof pads[p]);
        pads[p].lx = pads[p].ly = pads[p].rx = pads[p].ry = 0x80;
        pads[p].connected = p == 0 || s->pads[p] != NULL;
        if (s->pads[p] && s->analog) {
            pads[p].analog = 1;
            pads[p].lx = stick(s->pads[p], SDL_CONTROLLER_AXIS_LEFTX);
            pads[p].ly = stick(s->pads[p], SDL_CONTROLLER_AXIS_LEFTY);
            pads[p].rx = stick(s->pads[p], SDL_CONTROLLER_AXIS_RIGHTX);
            pads[p].ry = stick(s->pads[p], SDL_CONTROLLER_AXIS_RIGHTY);
        }
    }
    *hotkeys = 0;
    for (i = 0; i < s->binding_count; i++) {
        const BfmPlatBinding *b = &s->bindings[i];
        /* Typing into the console must not also press hotkeys bound to
         * letters; only the console key itself stays live. */
        if (s->text_active && CODE_KIND(b->host_code) == CODE_KEY &&
            !(b->is_hotkey && b->mask == BFM_HOTKEY_CONSOLE))
            continue;
        if (!active_code(s, keys, nkeys, b)) continue;
        if (b->is_hotkey) *hotkeys |= b->mask;
        else if (b->port < BFM_PLAT_PAD_PORTS) pads[b->port].buttons |= b->mask;
    }
    return BFM_PLAT_OK;
}

static int sdl_set_bindings(void *self, const BfmPlatBinding *b, size_t n) {
    SdlInput *s = (SdlInput *)self;
    if (n > MAX_BINDINGS) return BFM_PLAT_NO_SPACE;
    memcpy(s->bindings, b, n * sizeof *b);
    s->binding_count = n;
    return BFM_PLAT_OK;
}

static const BfmPlatInputBackend sdl_backend = {
    "sdl", sdl_open, sdl_close, sdl_poll, sdl_set_bindings, &state
};

int bfm_plat_backend_sdl_input_register(void);
int bfm_plat_backend_sdl_input_register(void) {
    return bfm_plat_input_register(&sdl_backend);
}
