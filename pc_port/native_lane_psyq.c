/* PsyQ entry points in the native lane: port-plat's compat table
 * (pc_port/platform/psyq/bfm_psyq_compat.def) instantiated as lane entries,
 * so a guest jal/jalr into a Sony library address runs the HLE/PsyCross
 * wrapper instead of the retail words. A wrapper that refuses leaves the
 * interpreter to run the code from the user's disc, exactly as without it.
 *
 * Classes (MUSASHI_PSYQ_LANE):
 *   pure   (default) register-file and guest-RAM work only: libc, packet
 *          helpers, OT clears, CD position maths, stateless matrix maths,
 *          and the GTE-state libgte wrappers behind the gte_owner bridge.
 *   all    pure + the device-level wrappers (VSync, DrawSync, DrawOTag,
 *          LoadImage/StoreImage/MoveImage, PutDrawEnv/PutDispEnv,
 *          SetDispMask, CdSearchFile, putchar). These bypass the emulated
 *          GPU, CD and IRQ timing, so they are accepted only with
 *          MUSASHI_GPU=bfm_plat (the caller decides) and never draw twice:
 *          the HLE DrawOTag replaces the retail DMA2 walk for that call.
 *   0      none.
 * STUB entries are never installed; they would only refuse. Nor are the
 * held-back entries below. */
#include "musashi_native_lane_psyq.h"

#include <stdlib.h>
#include <string.h>

#include "psyq/bfm_psyq_compat.h"

#define BFM_PSYQ(pc, n, st, ac, ret, w) \
    static int lane_psyq_##n(MusashiNativeLaneCall *c) { return w(c->r); }
#include "psyq/bfm_psyq_compat.def"
#undef BFM_PSYQ

typedef struct PsyqRow {
    MusashiNativeLaneEntry entry;
    BfmPsyqStatus status;
} PsyqRow;

#define BFM_PSYQ(pc, n, st, ac, ret, w) {{pc, 2u * 8u, lane_psyq_##n, #n, ac, ret}, st},
static const PsyqRow kPsyqRows[] = {
#include "psyq/bfm_psyq_compat.def"
};
#undef BFM_PSYQ

static const char *const kDeviceNames[] = {
    "VSync", "DrawSync", "DrawOTag", "LoadImage", "StoreImage", "MoveImage",
    "PutDrawEnv", "PutDispEnv", "SetDispMask", "CdSearchFile", "putchar",
};

/* Entries the differential probe (tests/native_lane_probe.c) found to
 * differ from the retail code are held back here by PC with the reason,
 * until port-plat's table is fixed; the interpreter runs them. The first 7
 * (srand, strcpy, SetLineG4, CdIntToPos, RotMatrixYXZ, Push/PopMatrix) were
 * fixed in port/platform-layer 7935600a2. */
static const struct { uint32_t pc; const char *why; } kHeldBack[] = {
    {0u, NULL},
};

/* Generated with the lane table (tools/native_lane_gen.py psyq_hooked):
 * entries whose body holds a PC the port sequences or observes (startup
 * gates, IRQ/callback routers, GTE load sites). Native execution would skip
 * those host checkpoints, so the interpreter keeps them. */
extern const uint32_t musashi_native_lane_hooked_psyq[];
extern const size_t musashi_native_lane_hooked_psyq_count;

const char *musashi_native_lane_psyq_held_back(uint32_t pc) {
    size_t i;
    for (i = 0; i < sizeof kHeldBack / sizeof kHeldBack[0]; ++i)
        if (kHeldBack[i].why && kHeldBack[i].pc == pc) return kHeldBack[i].why;
    for (i = 0; i < musashi_native_lane_hooked_psyq_count; ++i)
        if (musashi_native_lane_hooked_psyq[i] == pc) return "body holds a port-sequenced PC";
    return NULL;
}

int musashi_native_lane_psyq_is_device(const char *name) {
    size_t i;
    for (i = 0; i < sizeof kDeviceNames / sizeof kDeviceNames[0]; ++i)
        if (strcmp(name, kDeviceNames[i]) == 0) return 1;
    return 0;
}

unsigned musashi_native_lane_psyq_classes(void) {
    const char *v = getenv("MUSASHI_PSYQ_LANE");
    if (!v || !v[0] || strcmp(v, "pure") == 0) return MUSASHI_PSYQ_LANE_PURE;
    if (strcmp(v, "all") == 0) return MUSASHI_PSYQ_LANE_PURE | MUSASHI_PSYQ_LANE_DEVICE;
    return 0u;
}

size_t musashi_native_lane_psyq_entries(MusashiNativeLaneEntry *out, size_t cap,
                                        unsigned classes) {
    size_t i, n = 0;
    for (i = 0; i < sizeof kPsyqRows / sizeof kPsyqRows[0]; ++i) {
        const PsyqRow *row = &kPsyqRows[i];
        unsigned cls;
        if (row->status == BFM_PSYQ_STUB) continue;
        if (musashi_native_lane_psyq_held_back(row->entry.guest_pc)) continue;
        cls = musashi_native_lane_psyq_is_device(row->entry.name) ? MUSASHI_PSYQ_LANE_DEVICE
                                                                  : MUSASHI_PSYQ_LANE_PURE;
        if (!(classes & cls)) continue;
        if (out && n < cap) out[n] = row->entry;
        ++n;
    }
    return n;
}

MusashiNativeLaneEntry *musashi_native_lane_psyq_merge(const MusashiNativeLaneEntry *base,
                                                       size_t base_count, unsigned classes,
                                                       size_t *count) {
    size_t extra = musashi_native_lane_psyq_entries(NULL, 0, classes);
    MusashiNativeLaneEntry *all = malloc((base_count + extra + 1u) * sizeof *all);
    if (!all) return NULL;
    if (base_count) memcpy(all, base, base_count * sizeof *all);
    *count = base_count + musashi_native_lane_psyq_entries(all + base_count, extra, classes);
    return all;
}

/* BfmPsyqGteBridge over the single cop2 owner (gte_owner.c). */
static int bridge_begin(void *user) { return musashi_gte_owner_native_begin(user); }
static void bridge_end(void *user, uint32_t commands) {
    musashi_gte_owner_native_end(user, commands);
}

void musashi_native_lane_psyq_bind(MusashiBootMemory *memory, MusashiGteOwner *gte) {
    if (!memory) {
        bfm_psyq_set_gte_bridge(NULL);
        return;
    }
    /* Lane design (a): guest physical 0 is host 0x80000000 (memory->bytes). */
    bfm_psyq_set_ram(memory->bytes, MUSASHI_RAM_SIZE);
    bfm_psyq_set_scratchpad(memory->scratchpad);
    if (gte) {
        const BfmPsyqGteBridge bridge = {bridge_begin, bridge_end, gte};
        bfm_psyq_set_gte_bridge(&bridge);
    } else {
        bfm_psyq_set_gte_bridge(NULL);
    }
}
