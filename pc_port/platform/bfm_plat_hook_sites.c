/* Known retail hook sites for SLUS-00726 (US Brave Fencer Musashi).
 *
 * Addresses and field offsets only (facts about the program, no retail
 * bytes). Evidence paths are relative to vendor/bfm-decomp (Druthulu's
 * cleaned decomp of the same build, gitignored, fetched separately) or to
 * this repository. tests/test_bfm_plat.py pins each site to its symbol and,
 * when the vendor tree is present, checks that the cited lines still say
 * what the evidence claims. */
#include "bfm_plat_events.h"
#include "bfm_plat_mods.h"
#include "bfm_plat_widescreen.h"

#include <stdlib.h>
#include <string.h>

static int read_u16(uint32_t a, uint32_t *out) {
    uint8_t b[2];
    int r = bfm_plat_guest_read(a, b, 2);
    if (r == BFM_PLAT_OK) *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8);
    return r;
}

static int read_u32(uint32_t a, uint32_t *out) {
    uint8_t b[4];
    int r = bfm_plat_guest_read(a, b, 4);
    if (r == BFM_PLAT_OK)
        *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
               ((uint32_t)b[3] << 24);
    return r;
}

/* "SC02_031" / "sc02_0031" -> area 2, room 31; location from guest RAM. */
static void overlay_room(BfmEventRoom *room) {
    const char *o = bfm_plat_hooks_overlay();
    const char *us;
    uint32_t loc;
    room->area = room->room = 0;
    room->location = read_u16(BFM_GUEST_LOCATION, &loc) == BFM_PLAT_OK ? (int16_t)(uint16_t)loc : -1;
    if (!o || (o[0] != 'S' && o[0] != 's') || (o[1] != 'C' && o[1] != 'c')) return;
    room->area = (uint32_t)strtoul(o + 2, NULL, 10);
    us = strchr(o, '_');
    if (us) room->room = (uint32_t)strtoul(us + 1, NULL, 10);
}

/* Edge state, reset by bfm_plat_hooks_register_known. */
static uint32_t last_phase = 0xFFFFFFFFu;
static char last_overlay[32];
static uint32_t last_card_state_save = 0xFFFFFFFFu;
static uint32_t last_card_state_load = 0xFFFFFFFFu;

/* Location-overlay entry stub: fires once when the scene phase is 0 (init)
 * after being non-zero, or when a different overlay reaches phase 0. */
static int fill_room_enter(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    uint32_t phase;
    const char *o = bfm_plat_hooks_overlay();
    int changed;
    (void)u; (void)e;
    if (read_u16(BFM_GUEST_SCENE_PHASE, &phase) != BFM_PLAT_OK) return 1;
    changed = strcmp(last_overlay, o ? o : "") != 0;
    if (phase == 0 && (last_phase != 0 || changed)) {
        last_phase = phase;
        strncpy(last_overlay, o ? o : "", sizeof last_overlay - 1);
        overlay_room(&p->room);
        return 0;
    }
    last_phase = phase;
    strncpy(last_overlay, o ? o : "", sizeof last_overlay - 1);
    return 1;
}

/* func_80011B7C(mode): the scene loop leaves with mode 8. */
static int fill_room_exit(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    uint32_t a0;
    (void)u; (void)e;
    if (bfm_plat_guest_reg(BFM_GUEST_REG_A0, &a0) != BFM_PLAT_OK) return 1;
    if ((a0 & 0xFFFFu) != 8u) return 1;
    if (!bfm_plat_overlay_matches("SC*", bfm_plat_hooks_overlay())) return 1;
    overlay_room(&p->room);
    last_phase = 0xFFFFFFFFu;
    return 0;
}

static int card_edge(uint32_t *last, int (*want)(uint32_t), BfmPlatEventPayload *p) {
    uint32_t st, a1;
    if (read_u32(BFM_GUEST_CARD_STATE, &st) != BFM_PLAT_OK) return 1;
    if (!want(st) || st == *last) {
        *last = st;
        return 1;
    }
    *last = st;
    if (bfm_plat_guest_reg(BFM_GUEST_REG_A1, &a1) != BFM_PLAT_OK) a1 = 0;
    p->save.slot = a1;
    return 0;
}

static int is_save_state(uint32_t s) { return s == 13u; }
static int is_load_state(uint32_t s) { return s == 17u || s == 32u; }

static int fill_save(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    (void)u; (void)e;
    return card_edge(&last_card_state_save, is_save_state, p);
}

static int fill_load(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    (void)u; (void)e;
    return card_edge(&last_card_state_load, is_load_state, p);
}

/* libgte SetGeomScreen(h): ctc2 $a0, $26 (H is 16 bits). */
static int fill_geom_screen(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    uint32_t a0;
    BfmPlatProjection cur;
    (void)u; (void)e;
    if (bfm_plat_guest_reg(BFM_GUEST_REG_A0, &a0) != BFM_PLAT_OK) return 1;
    bfm_plat_projection_get(&cur);
    bfm_plat_projection_record((int32_t)(a0 & 0xFFFFu), cur.ofx, cur.ofy);
    p->projection.h = (int32_t)(a0 & 0xFFFFu);
    p->projection.ofx = cur.ofx;
    p->projection.ofy = cur.ofy;
    return 0;
}

/* libgte SetGeomOffset(ofx, ofy): each shifted <<16 into OFX/OFY. */
static int fill_geom_offset(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    uint32_t a0, a1;
    BfmPlatProjection cur;
    (void)u; (void)e;
    if (bfm_plat_guest_reg(BFM_GUEST_REG_A0, &a0) != BFM_PLAT_OK ||
        bfm_plat_guest_reg(BFM_GUEST_REG_A1, &a1) != BFM_PLAT_OK)
        return 1;
    bfm_plat_projection_get(&cur);
    bfm_plat_projection_record(cur.h, (int16_t)(a0 & 0xFFFFu), (int16_t)(a1 & 0xFFFFu));
    p->projection.h = cur.h;
    p->projection.ofx = (int16_t)(a0 & 0xFFFFu);
    p->projection.ofy = (int16_t)(a1 & 0xFFFFu);
    return 0;
}

/* Location overlays (plus MAIN_012) that carry the shared player-stat
 * helpers at the same address: config/dedup.us.yaml entries
 * E_func_8014BC80 and E_func_8014BD60 (identical member lists). */
static const char gauge_overlays[] =
    "MAIN_012,SC01_000,SC01_001,SC01_004,SC01_005,SC01_006,SC01_008,"
    "SC01_009,SC01_074,SC01_077,SC01_080,SC01_084,SC02_000,SC02_003,"
    "SC02_004,SC02_005,SC02_011,SC02_015,SC02_016,SC02_017,SC02_021,"
    "SC02_026,SC02_027,SC02_028,SC02_031,SC02_035,SC02_037,SC02_039,"
    "SC02_041,SC03_001,SC03_002,SC03_003,SC03_006,SC03_007,SC03_010,"
    "SC03_011,SC03_012,SC03_013,SC03_014,SC03_015,SC03_023,SC03_024,"
    "SC03_028,SC03_029,SC03_030,SC03_031,SC03_089,SC03_090,SC03_091,"
    "SC03_092,SC03_093,SC03_094,SC03_095,SC03_096,SC03_097,SC03_098,"
    "SC03_099,SC03_100,SC03_101,SC03_102,SC03_103,SC03_104,SC03_105,"
    "SC03_107,SC03_108,SC03_109,SC03_110,SC03_111,SC03_112,SC03_113,"
    "SC03_114,SC03_115,SC03_116,SC03_117,SC03_118,SC03_119,SC03_121,"
    "SC03_124,SC03_125,SC03_126,SC04_000,SC04_002,SC04_003,SC04_004,"
    "SC04_005,SC04_006,SC04_007,SC04_008,SC04_009,SC04_010,SC04_011,"
    "SC04_012,SC04_015,SC04_016,SC04_018,SC04_019,SC04_020,SC04_021,"
    "SC05_000,SC05_001,SC05_002,SC05_003,SC05_004,SC05_005,SC05_006,"
    "SC05_007,SC05_008,SC05_009,SC05_010,SC05_011,SC05_017,SC05_018,"
    "SC05_019,SC06_000,SC06_006,SC06_008,SC06_010,SC06_011,SC06_013,"
    "SC06_014,SC06_015,SC06_016,SC06_018,SC06_020,SC06_022,SC06_024,"
    "SC06_025,SC06_027,SC06_029,SC06_030,SC06_032,SC06_033,SC07_000,"
    "SC07_001,SC07_002,SC07_006,SC07_007,SC07_008,SC07_009,SC07_010,"
    "SC07_011";

static int read_s16(uint32_t a, int32_t *out) {
    uint32_t v;
    int r = read_u16(a, &v);
    if (r == BFM_PLAT_OK) *out = (int16_t)(uint16_t)v;
    return r;
}

/* func_8014BC80(obj, amount): HP -= amount, or HP = 0 when amount > HP. */
static int fill_damage(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    uint32_t a1, max = 0;   /* max stays 0 if the guest read fails */
    int32_t hp;
    (void)u;
    if (bfm_plat_guest_reg(BFM_GUEST_REG_A1, &a1) != BFM_PLAT_OK ||
        read_s16(e == BFM_EVENT_DAMAGE ? BFM_GUEST_HP : BFM_GUEST_BP, &hp) != BFM_PLAT_OK)
        return 1;
    read_u16(e == BFM_EVENT_DAMAGE ? BFM_GUEST_HP_MAX : BFM_GUEST_BP_MAX, &max);
    p->stat.previous = (uint16_t)hp;
    p->stat.value = (int32_t)a1 > (uint16_t)hp ? 0 : (uint16_t)hp - (int32_t)a1;
    p->stat.max = (int32_t)max;
    p->stat.flags = 0;
    bfm_plat_events_mark_code_site(e);
    return 0;
}

/* func_800D0F0C(slot_var, item): stores the item id in the slot. */
static int fill_item_add(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    uint32_t a0, a1;
    (void)u; (void)e;
    if (bfm_plat_guest_reg(BFM_GUEST_REG_A0, &a0) != BFM_PLAT_OK ||
        bfm_plat_guest_reg(BFM_GUEST_REG_A1, &a1) != BFM_PLAT_OK)
        return 1;
    if ((a1 & 0xFFu) == 0) return 1;             /* clearing a slot */
    p->item.item = a1 & 0xFFu;
    p->item.count = 1;
    p->item.kind = BFM_ITEM_KIND_INVENTORY;
    p->item.flags = 0;
    bfm_plat_events_mark_code_site(BFM_EVENT_ITEM_GET);
    return 0;
}

/* func_800D128C(item, age): the item's effect. */
static int fill_item_use(void *u, BfmEvent e, BfmPlatEventPayload *p) {
    uint32_t a0;
    (void)u; (void)e;
    if (bfm_plat_guest_reg(BFM_GUEST_REG_A0, &a0) != BFM_PLAT_OK) return 1;
    p->item.item = a0 & 0xFFu;
    p->item.count = 1;
    p->item.kind = BFM_ITEM_KIND_INVENTORY;
    p->item.flags = 0;
    return 0;
}

static const BfmPlatKnownSite known[] = {
    {0x800189A8u, NULL, BFM_EVENT_FRAME_BEGIN, "func_800189A8",
     "src/boot.c:310 first call in main's per-frame inner loop (main @ src/boot.c:277); "
     "src/800.c:6042 body polls both pads (0x4C-byte records at D_80078D98, PadGetState at src/800.c:6094); "
     "also called once per frame by the resident modal loop src/md_MAIN_003/md_MAIN_003_jr_800D1E18.c:236",
     "high: runs exactly once per main-loop frame, before GameModeDispatch; the modal loop "
     "caller also gets FRAME_BEGIN but no FRAME_END", NULL},
    {0x800184F0u, NULL, BFM_EVENT_FRAME_END, "func_800184F0",
     "src/boot.c:372 called right before VSync(*(s32 *)(p + 0xA3E8)) at src/boot.c:374; "
     "definition src/800.c:5761; only caller in vendor/bfm-decomp/src",
     "high: last game call of each main-loop frame before the VSync wait", NULL},
    {0x80128158u, "SC*", BFM_EVENT_ROOM_ENTER, "func_80128158",
     "location-overlay export stub 0 -> func_80128288 (src/overlays/main_0012/80128288.c in this repo: "
     "dispatch D_8017E618[D_800B99F6] for phase < 9); called every scene-loop frame by resident "
     "func_800CF238 at src/resident/resident.c:255; phase 0 reached the SC02/31 init routine "
     "80128420 in docs/MENU-BOOT-CONTINUATION.md (\"SC02 initialization routine\")",
     "medium: phase 0 = scene init is observed for SC02/31 only; fill fires on the edge into "
     "phase 0 or on an overlay change. area/room come from the overlay name the native lane "
     "reports, not from guest RAM", fill_room_enter},
    {0x80011B7Cu, NULL, BFM_EVENT_ROOM_EXIT, "func_80011B7C",
     "src/boot.c:950 sets major mode (base+0xA3C0) and clears the scene phase; the resident "
     "scene loop leaves with func_80011B7C(8) at src/resident/resident.c:261",
     "low: func_80011B7C has ~400 call sites; the fill only fires for mode 8 while a location "
     "overlay is selected, which matches the scene-loop exit but is not proven unique", fill_room_exit},
    {0x8002B0B4u, NULL, BFM_EVENT_SAVE, "func_8002B0B4",
     "memory-card save/load state machine (src/800_b.c:87, 'SaveLoadRoutine' is its case 0); "
     "state 13 writes the slot's 0x300-byte game-state block D_80075CC0 at idx*0x300+0x480 "
     "(src/800_b.c:301, src/800_b.c:318) then advances D_800760AC",
     "medium: fires on entry when D_800760AC becomes 13 (slot = $a1 idx); the state switch sits "
     "under a card-event poll, so a retried write can fire twice", fill_save},
    {0x8002B0B4u, NULL, BFM_EVENT_LOAD, "func_8002B0B4",
     "same state machine; states 17 and 32 read the slot's game-state block with "
     "func_80060404(D_80075CC0, idx*0x300+0x480, 0x300) (src/800_b.c:486-488)",
     "medium: as for save; states 17/32 are the two read paths", fill_load},
    {0x8004923Cu, NULL, BFM_EVENT_PROJECTION, "func_8004923C",
     "libgte SetGeomScreen: body is `ctc2 $a0, $26; jr $ra` (repo src/main/8004923c.c); "
     "game init func_80014444 calls InitGeom (func_80047CB4, which sets H=0x3E8, OFX=OFY=0) then "
     "func_8004923C(0x3E8) and stores 0x3E8 in D_80126950 (src/800.c:1787; repo src/main/80014444.c)",
     "high: instruction shape is exactly SetGeomScreen", fill_geom_screen},
    {0x8004921Cu, NULL, BFM_EVENT_PROJECTION, "func_8004921C",
     "libgte SetGeomOffset: `sll $a0,16; sll $a1,16; ctc2 $a0,$24; ctc2 $a1,$25` (repo src/main/8004921c.c); "
     "called per draw buffer by the libgs offset routine func_80052BEC (repo src/main/80052bec.c) "
     "and by the overlays' camera code (src/shared/ov/func_8017C530.h:14)",
     "high: instruction shape is exactly SetGeomOffset", fill_geom_offset},
    {0x8014BC80u, gauge_overlays, BFM_EVENT_DAMAGE, "func_8014BC80",
     "shared player-HP subtract: HP -= amount, or HP = 0 and D_800B9A17 = 0 when amount > HP "
     "(src/shared/ov/func_8014BC80.h); func_8014BC44 routes instant kills through it with "
     "amount = max HP; present at this address in the 141 overlays of "
     "config/dedup.us.yaml E_func_8014BC80",
     "high: the lethal player-damage helper; non-lethal func_8014BCC0 (clamps at 1) is not "
     "hooked, so its drops arrive only as derived damage", fill_damage},
    {0x8014BD60u, gauge_overlays, BFM_EVENT_BP_USE, "func_8014BD60",
     "shared BP subtract with clamp at 0 (src/shared/ov/func_8014BD60.h), same 141 overlays "
     "(config/dedup.us.yaml E_func_8014BD60)",
     "high", fill_damage},
    {0x800D0F0Cu, NULL, BFM_EVENT_ITEM_GET, "func_800D0F0C",
     "resident inventory add: var[slot] = item, time var = D_80078EAC "
     "(src/resident/resident_jr_800D00E4.c:965); slots are vars 0x2F..0x3A "
     "(free-slot scan func_800D0EC4, :950); called by the shops after payment "
     "(src/ov_SC03_124/ov_SC03_124_jr_80188544.c:3526)",
     "high for shop and pickup adds that use it; items placed by other paths "
     "arrive as derived item_get from the slot watch", fill_item_add},
    {0x800D128Cu, NULL, BFM_EVENT_ITEM_USE, "func_800D128C",
     "resident item-effect handler: switch on item id 1..119, heals via func_8014BB24, BP "
     "via func_8014BD24 (src/resident/resident_jr_800D128C.c:242); called by the inventory "
     "use path func_800D11F0 (src/resident/resident_jr_800D00E4.c:1197), which then empties "
     "the slot, and by shops for buy-and-use",
     "high", fill_item_use},
};

static const BfmEvent unsited[] = {
    BFM_EVENT_BATTLE_START, BFM_EVENT_BATTLE_END
};

size_t bfm_plat_known_sites(const BfmPlatKnownSite **out) {
    if (out) *out = known;
    return sizeof known / sizeof known[0];
}

size_t bfm_plat_unsited_events(const BfmEvent **out) {
    if (out) *out = unsited;
    return sizeof unsited / sizeof unsited[0];
}

int bfm_plat_hooks_register_known(void) {
    BfmPlatHookSite s[sizeof known / sizeof known[0]];
    size_t i;
    last_phase = 0xFFFFFFFFu;
    last_overlay[0] = '\0';
    last_card_state_save = last_card_state_load = 0xFFFFFFFFu;
    memset(s, 0, sizeof s);
    for (i = 0; i < sizeof known / sizeof known[0]; i++) {
        s[i].guest_pc = known[i].guest_pc;
        s[i].event = known[i].event;
        s[i].label = known[i].symbol;
        s[i].fill = known[i].fill;
        s[i].overlay = known[i].overlay;
    }
    return bfm_plat_hooks_register(s, i);
}
