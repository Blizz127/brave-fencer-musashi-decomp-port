#ifndef BFM_PLAT_EVENTS_H
#define BFM_PLAT_EVENTS_H

/* Game-event hook points.
 *
 * The events themselves (BfmEvent, payload structs) are ABI and live in
 * bfm_plugin.h. This header adds:
 *
 *  - a catalog: stable names ("room_enter") and payload descriptions for
 *    every event, for logs, the console `events` command and a future
 *    script runtime;
 *  - hook sites: a table the native lane fills with the guest function
 *    addresses where each game event happens. The dispatcher calls
 *    bfm_plat_hooks_at(pc) on entry to a guest function. If a site is
 *    registered there, its `fill` callback reads guest state (registers /
 *    RAM via bfm_plat_guest_read) into the payload, and the event fires.
 *
 * The retail addresses are not known yet: the catalog lists the hook
 * points with guest_site "TBD" until the native lane identifies them.
 * The direct emitters in bfm_plat.h (bfm_plat_event_room_enter, ...) stay
 * available for hand-ported code. */

#include "bfm_plugin.h"

typedef struct BfmPlatEventInfo {
    BfmEvent event;
    const char *name;          /* "room_enter" */
    const char *payload;       /* "BfmEventRoom" or "" */
    const char *fired_by;      /* who emits it */
    const char *description;
} BfmPlatEventInfo;

const BfmPlatEventInfo *bfm_plat_event_info(BfmEvent event);
const char *bfm_plat_event_name(BfmEvent event);
/* Returns BFM_EVENT_COUNT if unknown. */
BfmEvent bfm_plat_event_from_name(const char *name);

typedef union BfmPlatEventPayload {
    BfmEventStat stat;
    BfmEventRoom room;
    BfmEventBattle battle;
    BfmEventItem item;
    BfmEventSave save;
    BfmEventProjection projection;
} BfmPlatEventPayload;

/* Reads guest state for the event at a hook site. Return 0 to fire, nonzero
 * to skip this hit (e.g. the call was not a real room change). */
typedef int (*BfmPlatHookFill)(void *user, BfmEvent event,
                               BfmPlatEventPayload *payload);

typedef struct BfmPlatHookSite {
    uint32_t guest_pc;         /* function entry (KSEG0 address) */
    BfmEvent event;
    const char *label;         /* e.g. "func_80012345 room loader" */
    BfmPlatHookFill fill;      /* NULL: fire with a zeroed payload */
    void *user;
    /* Which binary the pc belongs to. NULL = main exe or resident (fixed
     * addresses). Pcs in the location-overlay window (>= 0x80128158) mean
     * different code per overlay and MUST name one: "SC02_031" exact, or a
     * prefix pattern ending in '*' ("SC*"). Case-insensitive. */
    const char *overlay;
} BfmPlatHookSite;

#define BFM_PLAT_HOOK_SITES_MAX 64
#define BFM_PLAT_OVERLAY_WINDOW 0x80128158u

/* Frame sites are actions, not just events: a FRAME_BEGIN site runs
 * bfm_plat_frame_begin() (input poll, hotkeys, pause) and a FRAME_END site
 * runs bfm_plat_frame_hook_end() (FRAME_END hook = cheats, overlays; no
 * present/pacing, since the game's own VSync path does that). */

/* Registers sites (copied; label/overlay must outlive the registration).
 * A pc may carry several events. */
int bfm_plat_hooks_register(const BfmPlatHookSite *sites, size_t count);
/* Cheap check for the dispatcher's hot path. */
int bfm_plat_hooks_has(uint32_t guest_pc);
/* Fires every event registered at pc; returns how many fired. */
int bfm_plat_hooks_at(uint32_t guest_pc);
size_t bfm_plat_hooks_count(void);
void bfm_plat_hooks_reset(void);

/* The native lane reports which location overlay is loaded ("SC02_031",
 * or NULL for none). Overlay-qualified sites only fire while it matches. */
void bfm_plat_hooks_set_overlay(const char *name);
const char *bfm_plat_hooks_overlay(void);
/* Patterns: NULL = any; "SC*" prefix; "SC02_031" exact; a comma-separated
 * list of those. Exact names compare by prefix and numbers, so "SC02_031",
 * "sc02_0031" and "ov_SC02_031" are the same overlay. */
int bfm_plat_overlay_matches(const char *pattern, const char *name);

/* A derived watch event is suppressed when a code site already reported
 * the same change (bfm_plat_hook_sites.c marks it; bfm_plat_watch clears
 * the marks after each poll). */
void bfm_plat_events_mark_code_site(BfmEvent event);
int bfm_plat_events_take_code_mark(BfmEvent event);
void bfm_plat_events_clear_code_marks(void);

/* ---- Known retail hook sites (SLUS-00726) ------------------------------
 * Evidence-backed guest sites for the hook points, with their payload
 * fills. Evidence cites vendor/bfm-decomp (Druthulu's cleaned decomp of
 * the same build) and this repository's runtime notes. No retail bytes are
 * involved: these are addresses and field offsets only. */
#define BFM_GUEST_STATE_BASE   0x800AF630u  /* D_800AF630 main state block */
#define BFM_GUEST_MAJOR_MODE   0x800B99F0u  /* u16 base+0xA3C0 (func_80011B7C arg) */
#define BFM_GUEST_SCENE_PHASE  0x800B99F6u  /* u16 base+0xA3C6 = D_800B99F6 */
#define BFM_GUEST_CARD_STATE   0x800760ACu  /* s32 D_800760AC memcard state */
/* Live game state (the 0x98-byte struct D_80078E78 and friends that
 * func_80029774 snapshots into each 0x2DC-byte save record; see
 * docs/ARCHITECTURE-PORT.md "Live game state" for the evidence). */
#define BFM_GUEST_LIVE_STRUCT   0x80078E78u  /* 0x98 bytes */
#define BFM_GUEST_PLAY_TIME     0x80078E7Cu  /* u8 frames(/30), s, min, h (<=99:59:59) */
#define BFM_GUEST_MONEY         0x80078E8Cu  /* s32, 0..99999 */
#define BFM_GUEST_HP_MAX        0x80078EB2u  /* u16 gauge 1 max, <= 500 */
#define BFM_GUEST_HP            0x80078EB4u  /* s16 gauge 1 current */
#define BFM_GUEST_BP_MAX        0x80078EB6u  /* u16 gauge 2 max, <= 0x662 */
#define BFM_GUEST_BP            0x80078EB8u  /* s16 gauge 2 current */
#define BFM_GUEST_STORY_FLAGS   0x800AE648u  /* 64 bytes = 512 bit flags */
#define BFM_GUEST_SCRIPT_VARS   0x800BA1B8u  /* 256 bytes, byte/halfword vars */
#define BFM_GUEST_FIGURE_VAR0   0x63u        /* var index of figure 0 (43 of them) */
#define BFM_GUEST_FIGURE_COUNT  43u
#define BFM_GUEST_FIGURE_OWNED  0x40u        /* var bit: bought */
#define BFM_GUEST_FIGURE_UNLOCK 0x80u        /* var bit: in the shop */
#define BFM_GUEST_FIGURE_PRICES 0x800A6588u  /* u16[64]; figure n at [n] */
#define BFM_GUEST_INV_VAR0      0x2Fu        /* var index of inventory slot 0 */
#define BFM_GUEST_INV_SLOTS     12u          /* vars 0x2F..0x3A: item id, 0 = empty */
#define BFM_GUEST_INV_TIME_VAR0 0x14u        /* halfword vars 0x14 + 2*slot: acquired at */
#define BFM_GUEST_DAY           0x80078EACu  /* u16 day counter (<= 999 shown) */
#define BFM_GUEST_MINUTE        0x80078EB0u  /* u8 time of day: minute */
#define BFM_GUEST_HOUR          0x80078EB1u  /* u8 time of day: hour (0..23) */
#define BFM_GUEST_LOCATION      0x800B9A08u  /* s16 currentLocationId; region = id & 0xF000 */
#define BFM_GUEST_REGION        0x800B9A0Au  /* u16 id & 0xF000 of the current location */
#define BFM_GUEST_NPC_TIMES     0x800BA2B8u  /* 24 x 4 bytes; s16 at +2 = day*24+hour stamp */
#define BFM_GUEST_CLOCK         BFM_GUEST_DAY  /* (older name) */

#define BFM_GUEST_REG_A0 4u
#define BFM_GUEST_REG_A1 5u

typedef struct BfmPlatKnownSite {
    uint32_t guest_pc;
    const char *overlay;       /* NULL / "SC*" */
    BfmEvent event;
    const char *symbol;        /* function name at guest_pc */
    const char *evidence;      /* file:line references */
    const char *confidence;    /* "high" / "medium" / "low" + why */
    BfmPlatHookFill fill;
} BfmPlatKnownSite;

size_t bfm_plat_known_sites(const BfmPlatKnownSite **out);
/* Registers every known site (resets the fills' edge state). */
int bfm_plat_hooks_register_known(void);
/* Events with no evidence-backed site yet (battle, item get). */
size_t bfm_plat_unsited_events(const BfmEvent **out);

/* Registers the `events` console command (list events + counts + sites). */
void bfm_plat_events_register_console(void);

#endif
