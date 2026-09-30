#include "bfm_plat_events.h"
#include "bfm_plat.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const BfmPlatEventInfo catalog[BFM_EVENT_COUNT] = {
    {BFM_EVENT_BOOT, "boot", "", "platform", "all mods loaded"},
    {BFM_EVENT_SHUTDOWN, "shutdown", "", "platform", "before mods unload"},
    {BFM_EVENT_FRAME_BEGIN, "frame_begin", "", "platform (bfm_plat_frame_begin)", "start of a game frame, after input"},
    {BFM_EVENT_FRAME_END, "frame_end", "", "platform (bfm_plat_frame_end)", "frame tick; cheats apply here"},
    {BFM_EVENT_VSYNC, "vsync", "", "platform (bfm_plat_timing_vsync)", "each NTSC field waited"},
    {BFM_EVENT_INPUT, "input", "BfmEventInput", "platform (bfm_plat_input_poll)", "pads/hotkeys, editable"},
    {BFM_EVENT_BEFORE_PRESENT, "before_present", "", "platform (renderer present)", "before the frame is shown"},
    {BFM_EVENT_AFTER_PRESENT, "after_present", "", "platform (renderer present)", "after the frame is shown"},
    {BFM_EVENT_ROOM_ENTER, "room_enter", "BfmEventRoom", "location-overlay entry stub (bfm_plat_hook_sites.c)", "a new room/map became active"},
    {BFM_EVENT_ROOM_EXIT, "room_exit", "BfmEventRoom", "scene-loop exit (bfm_plat_hook_sites.c)", "the current room is being left"},
    {BFM_EVENT_BATTLE_START, "battle_start", "BfmEventBattle", "native lane: boss/scripted fight setup (guest site TBD)", "a boss or scripted fight began"},
    {BFM_EVENT_BATTLE_END, "battle_end", "BfmEventBattle", "native lane: fight resolution (guest site TBD)", "that fight ended (result: 1 won, 0 lost/fled)"},
    {BFM_EVENT_ITEM_GET, "item_get", "BfmEventItem", "resident func_800D0F0C (inventory add); derived: slot vars 0x2F..0x3A, figure owned bits", "an item was added to the inventory"},
    {BFM_EVENT_SAVE, "save", "BfmEventSave", "memory-card state machine (bfm_plat_hook_sites.c)", "the game saved to a slot"},
    {BFM_EVENT_LOAD, "load", "BfmEventSave", "native lane: memory-card load", "the game loaded from a slot"},
    {BFM_EVENT_PROJECTION, "projection", "BfmEventProjection", "native lane: libgte SetGeomScreen/SetGeomOffset", "the game changed GTE H or OFX/OFY"},
    {BFM_EVENT_DAMAGE, "damage", "BfmEventStat", "derived: watch on gauge-1 (HP) current 0x80078EB4", "HP went down"},
    {BFM_EVENT_BP_USE, "bp_use", "BfmEventStat", "derived: watch on gauge-2 (BP) current 0x80078EB8", "BP went down"},
    {BFM_EVENT_MONEY, "money", "BfmEventStat", "derived: watch on money 0x80078E8C", "money changed"},
    {BFM_EVENT_ITEM_USE, "item_use", "BfmEventItem", "resident item-effect handler func_800D128C", "an inventory item was used"},
};

const BfmPlatEventInfo *bfm_plat_event_info(BfmEvent e) {
    return (unsigned)e < BFM_EVENT_COUNT ? &catalog[e] : NULL;
}

const char *bfm_plat_event_name(BfmEvent e) {
    return (unsigned)e < BFM_EVENT_COUNT ? catalog[e].name : "unknown";
}

BfmEvent bfm_plat_event_from_name(const char *name) {
    unsigned i;
    for (i = 0; name && i < BFM_EVENT_COUNT; i++)
        if (strcmp(catalog[i].name, name) == 0) return (BfmEvent)i;
    return BFM_EVENT_COUNT;
}

/* Sorted by guest_pc for binary search. */
static BfmPlatHookSite sites[BFM_PLAT_HOOK_SITES_MAX];
static size_t site_count;
static uint64_t site_hits[BFM_PLAT_HOOK_SITES_MAX];

int bfm_plat_hooks_register(const BfmPlatHookSite *in, size_t n) {
    size_t i;
    if (n && !in) return BFM_PLAT_INVALID;
    for (i = 0; i < n; i++)
        if ((unsigned)in[i].event >= BFM_EVENT_COUNT || (in[i].guest_pc & 3u) ||
            (in[i].guest_pc >= BFM_PLAT_OVERLAY_WINDOW && !in[i].overlay))
            return BFM_PLAT_INVALID;
    if (site_count + n > BFM_PLAT_HOOK_SITES_MAX) return BFM_PLAT_NO_SPACE;
    for (i = 0; i < n; i++) {
        size_t j = site_count++;
        while (j > 0 && sites[j - 1].guest_pc > in[i].guest_pc) {
            sites[j] = sites[j - 1];
            site_hits[j] = site_hits[j - 1];
            j--;
        }
        sites[j] = in[i];
        site_hits[j] = 0;
    }
    return BFM_PLAT_OK;
}

static size_t lower_bound(uint32_t pc) {
    size_t lo = 0, hi = site_count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2u;
        if (sites[mid].guest_pc < pc) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

int bfm_plat_hooks_has(uint32_t pc) {
    size_t i;
    if (!site_count) return 0;
    i = lower_bound(pc);
    return i < site_count && sites[i].guest_pc == pc;
}

static char current_overlay[32];
static int have_overlay;

void bfm_plat_hooks_set_overlay(const char *name) {
    have_overlay = name && *name;
    snprintf(current_overlay, sizeof current_overlay, "%s", have_overlay ? name : "");
}

const char *bfm_plat_hooks_overlay(void) { return have_overlay ? current_overlay : NULL; }

/* "ov_SC02_031" / "sc02_0031" -> prefix "SC", numbers 2 and 31. */
static int split_name(const char *s, size_t n, char *prefix, long *a, long *b) {
    size_t i = 0, k = 0;
    if (n >= 3 && (s[0] == 'o' || s[0] == 'O') && (s[1] == 'v' || s[1] == 'V') && s[2] == '_') {
        s += 3;
        n -= 3;
    }
    while (i < n && isalpha((unsigned char)s[i]) && k < 7) prefix[k++] = (char)toupper((unsigned char)s[i++]);
    prefix[k] = '\0';
    if (i >= n) return 0;
    *a = -1;                                   /* "MAIN_012": no archive number */
    if (isdigit((unsigned char)s[i])) {
        *a = 0;
        while (i < n && isdigit((unsigned char)s[i])) *a = *a * 10 + (s[i++] - '0');
    }
    if (i >= n || s[i] != '_') return 0;
    i++;
    if (i >= n || !isdigit((unsigned char)s[i])) return 0;
    *b = 0;
    while (i < n && isdigit((unsigned char)s[i])) *b = *b * 10 + (s[i++] - '0');
    return i == n;
}

static int match_one(const char *pat, size_t plen, const char *name) {
    size_t i;
    char p1[8], p2[8];
    long a1, b1, a2, b2;
    if (plen && pat[plen - 1] == '*') {
        for (i = 0; i + 1 < plen; i++)
            if (!name[i] || tolower((unsigned char)pat[i]) != tolower((unsigned char)name[i])) return 0;
        return 1;
    }
    if (split_name(pat, plen, p1, &a1, &b1) && split_name(name, strlen(name), p2, &a2, &b2))
        return strcmp(p1, p2) == 0 && a1 == a2 && b1 == b2;
    if (strlen(name) != plen) return 0;
    for (i = 0; i < plen; i++)
        if (tolower((unsigned char)pat[i]) != tolower((unsigned char)name[i])) return 0;
    return 1;
}

int bfm_plat_overlay_matches(const char *pat, const char *name) {
    if (!pat) return 1;
    if (!name) return 0;
    for (;;) {
        const char *comma = strchr(pat, ',');
        size_t len = comma ? (size_t)(comma - pat) : strlen(pat);
        if (len && match_one(pat, len, name)) return 1;
        if (!comma) return 0;
        pat = comma + 1;
    }
}

static uint32_t code_marks;

void bfm_plat_events_mark_code_site(BfmEvent e) {
    if ((unsigned)e < 32) code_marks |= 1u << e;
}

int bfm_plat_events_take_code_mark(BfmEvent e) {
    int had;
    if ((unsigned)e >= 32) return 0;
    had = (code_marks >> e) & 1u;
    code_marks &= ~(1u << e);
    return had;
}

void bfm_plat_events_clear_code_marks(void) { code_marks = 0; }

int bfm_plat_hooks_at(uint32_t pc) {
    size_t i;
    int fired = 0;
    for (i = lower_bound(pc); i < site_count && sites[i].guest_pc == pc; i++) {
        BfmPlatEventPayload payload;
        if (!bfm_plat_overlay_matches(sites[i].overlay, bfm_plat_hooks_overlay()))
            continue;
        memset(&payload, 0, sizeof payload);
        if (sites[i].fill && sites[i].fill(sites[i].user, sites[i].event, &payload) != 0)
            continue;
        site_hits[i]++;
        if (sites[i].event == BFM_EVENT_FRAME_BEGIN) bfm_plat_frame_wait();
        else if (sites[i].event == BFM_EVENT_FRAME_END) bfm_plat_frame_hook_end();
        else bfm_plat_mods_emit(sites[i].event, &payload);
        fired++;
    }
    return fired;
}

size_t bfm_plat_hooks_count(void) { return site_count; }

void bfm_plat_hooks_reset(void) {
    site_count = 0;
    have_overlay = 0;
    current_overlay[0] = '\0';
    code_marks = 0;
    memset(site_hits, 0, sizeof site_hits);
}

static int cmd_events(void *u, int argc, const char *const *argv, char *out,
                      size_t n) {
    unsigned i;
    size_t len;
    (void)u; (void)argc; (void)argv;
    if (!out || !n) return 0;
    out[0] = '\0';
    for (i = 0; i < BFM_EVENT_COUNT; i++) {
        len = strlen(out);
        snprintf(out + len, n - len, "%-15s %8llu  %s\n", catalog[i].name,
                 (unsigned long long)bfm_plat_mods_event_count((BfmEvent)i),
                 catalog[i].payload);
    }
    for (i = 0; i < site_count; i++) {
        len = strlen(out);
        snprintf(out + len, n - len, "site %08x%s%s %s %s hits=%llu\n",
                 (unsigned)sites[i].guest_pc, sites[i].overlay ? "@" : "",
                 sites[i].overlay ? sites[i].overlay : "",
                 bfm_plat_event_name(sites[i].event),
                 sites[i].label ? sites[i].label : "",
                 (unsigned long long)site_hits[i]);
    }
    return 0;
}

void bfm_plat_events_register_console(void) {
    bfm_plat_console_register("events", "list game events, counts and hook sites",
                              cmd_events, NULL);
}
