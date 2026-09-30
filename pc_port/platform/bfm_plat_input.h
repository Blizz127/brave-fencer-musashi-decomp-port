#ifndef BFM_PLAT_INPUT_H
#define BFM_PLAT_INPUT_H

/* Input interface: controller state by meaning (PS1 pad buttons, analog
 * sticks) plus port hotkeys (fast-forward, console, cheat menu).
 *
 * Buttons are active-high in this struct. bfm_plat_pad_to_wire() produces
 * the active-low bytes a PS1 pad returns on the SIO bus, for the libpad /
 * BIOS pad shim that the game's own pad code reads. */

#include "bfm_plat_types.h"
#include "bfm_plat_config.h"

#define BFM_PAD_SELECT   0x0001u
#define BFM_PAD_L3       0x0002u
#define BFM_PAD_R3       0x0004u
#define BFM_PAD_START    0x0008u
#define BFM_PAD_UP       0x0010u
#define BFM_PAD_RIGHT    0x0020u
#define BFM_PAD_DOWN     0x0040u
#define BFM_PAD_LEFT     0x0080u
#define BFM_PAD_L2       0x0100u
#define BFM_PAD_R2       0x0200u
#define BFM_PAD_L1       0x0400u
#define BFM_PAD_R1       0x0800u
#define BFM_PAD_TRIANGLE 0x1000u
#define BFM_PAD_CIRCLE   0x2000u
#define BFM_PAD_CROSS    0x4000u
#define BFM_PAD_SQUARE   0x8000u

#define BFM_HOTKEY_FAST_FORWARD 0x0001u
#define BFM_HOTKEY_CONSOLE      0x0002u
#define BFM_HOTKEY_CHEAT_MENU   0x0004u
#define BFM_HOTKEY_PAUSE        0x0008u
#define BFM_HOTKEY_SCREENSHOT   0x0010u
#define BFM_HOTKEY_QUIT         0x0020u

#define BFM_PLAT_PAD_PORTS 2

typedef struct BfmPlatPad {
    uint16_t buttons;          /* BFM_PAD_* active-high */
    uint8_t lx, ly, rx, ry;    /* 0x80 = centre */
    uint8_t connected;
    uint8_t analog;            /* 1 = DualShock analog mode */
} BfmPlatPad;

/* A host binding: backend-defined host code (e.g. SDL scancode, or
 * joystick button | 0x10000) mapped to a pad button or hotkey. */
typedef struct BfmPlatBinding {
    int32_t host_code;
    uint8_t port;
    uint8_t is_hotkey;
    uint16_t mask;
} BfmPlatBinding;

typedef struct BfmPlatInputBackend {
    const char *name;
    int (*open)(void *self);
    void (*close)(void *self);
    /* Pumps host events; fills both ports and the hotkey mask. */
    int (*poll)(void *self, BfmPlatPad pads[BFM_PLAT_PAD_PORTS],
                uint32_t *hotkeys);
    /* Replace bindings (backend may ignore codes it doesn't understand). */
    int (*set_bindings)(void *self, const BfmPlatBinding *b, size_t n);
    void *self;
} BfmPlatInputBackend;

int bfm_plat_input_register(const BfmPlatInputBackend *backend);
int bfm_plat_input_open(const char *name, const char **selected);
void bfm_plat_input_close(void);
const char *bfm_plat_input_active(void);

/* Once per frame. Applies cheat/mod overrides (BFM_EVENT_INPUT) after the
 * backend, then latches the result for bfm_plat_input_pad(). */
int bfm_plat_input_poll(void);
int bfm_plat_input_pad(unsigned port, BfmPlatPad *out);
uint32_t bfm_plat_input_hotkeys(void);          /* held this frame */
uint32_t bfm_plat_input_hotkeys_pressed(void);  /* rising edge this frame */
int bfm_plat_input_set_bindings(const BfmPlatBinding *b, size_t n);

/* PS1 wire format: out[0]=0x41 digital / 0x73 analog, out[1]=0x5A,
 * out[2..3] = ~buttons (lo, hi), out[4..7] = rx, ry, lx, ly (analog only).
 * Returns bytes written (0 if disconnected). */
size_t bfm_plat_pad_to_wire(const BfmPlatPad *pad, uint8_t out[8]);

/* ---- Console text input ------------------------------------------------
 * Backends push typed text and editing keys; the console front end drains
 * them each frame while it is open (bfm_plat_console_ui). Queue holds 64
 * events; overflow drops the newest. */
typedef enum BfmPlatKey {
    BFM_KEY_NONE = 0,
    BFM_KEY_ENTER,
    BFM_KEY_BACKSPACE,
    BFM_KEY_ESCAPE,
    BFM_KEY_UP,
    BFM_KEY_DOWN,
    BFM_KEY_LEFT,
    BFM_KEY_RIGHT,
    BFM_KEY_PAGE_UP,
    BFM_KEY_PAGE_DOWN,
    BFM_KEY_TAB
} BfmPlatKey;

typedef struct BfmPlatTextEvent {
    uint8_t key;               /* BfmPlatKey; BFM_KEY_NONE = text */
    char text[8];              /* UTF-8, NUL-terminated (text events) */
} BfmPlatTextEvent;

int bfm_plat_input_push_text(const char *utf8);
int bfm_plat_input_push_key(BfmPlatKey key);
size_t bfm_plat_input_take_events(BfmPlatTextEvent *out, size_t max);

/* While suppressed (console open), bfm_plat_input_pad reports neutral pads
 * so typing never reaches the game; hotkeys still work. */
void bfm_plat_input_suppress(int suppressed);

/* ---- Bindings from config -----------------------------------------------
 * [input] keys name a target and list host inputs, comma-separated:
 *   p1.cross = key:X, pad:a           p2.start = pad:start
 *   hotkey.fast_forward = key:Tab     p1.l2 = pad:lefttrigger
 * Targets: p1./p2. + select l3 r3 start up right down left l2 r2 l1 r1
 * triangle circle cross square; hotkey. + fast_forward console cheat_menu
 * pause screenshot quit. Targets without a config key use the defaults
 * below. The backend's resolver turns "key:X" / "pad:a" into host codes
 * (return < 0 for names it doesn't know; those entries are skipped and
 * counted in *unknown). */
typedef int32_t (*BfmPlatBindingResolver)(void *user, const char *device,
                                          const char *name);

typedef struct BfmPlatBindingTarget {
    const char *key;           /* "p1.cross", "hotkey.console" */
    uint8_t port;
    uint8_t is_hotkey;
    uint16_t mask;
    const char *default_value; /* "key:X, pad:a" */
} BfmPlatBindingTarget;

size_t bfm_plat_input_binding_targets(const BfmPlatBindingTarget **out);

int bfm_plat_input_bindings_from_config(const BfmPlatConfig *cfg,
                                        BfmPlatBindingResolver resolve,
                                        void *user, BfmPlatBinding *out,
                                        size_t max, size_t *count,
                                        size_t *unknown);

/* Built-in "scripted" backend (the default "null"): state is set by code, so
 * tests and the route autopilot can drive the game. */
void bfm_plat_input_scripted_set(unsigned port, const BfmPlatPad *pad);
void bfm_plat_input_scripted_hotkeys(uint32_t hotkeys);

void bfm_plat_input_reset(void);

#endif
