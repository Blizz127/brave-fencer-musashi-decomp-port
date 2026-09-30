#include "musashi_gte_owner.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "psx/gtereg.h"
#include "psx/inline_c.h"

/* A single owner is intentional: PsyCross exposes one process-wide GTE bank,
 * and concurrent owners would make a valid CTC2 transfer observationally
 * ambiguous.  This pointer is only a lease marker, never a second bank. */
static MusashiGteOwner *g_active_owner;

static int owner_reentry(MusashiGteOwner *owner) {
    if (owner != NULL && owner->executing) {
        owner->faulted = 1;
        owner->reentry_violation = 1;
        return 1;
    }
    return 0;
}

static int owner_thread_matches(MusashiGteOwner *owner) {
    uint64_t token;

    if (owner == NULL || owner->current_thread == NULL) return 0;
    token = owner->current_thread(owner->thread_userdata);
    if (owner->reentry_violation) return 0;
    return token == owner->owner_thread;
}

static int owner_begin(MusashiGteOwner *owner, int allow_faulted) {
    if (owner_reentry(owner)) return 0;
    if (owner == NULL || !owner->initialized) return 0;
    if (owner->faulted && !allow_faulted) return 0;
    owner->reentry_violation = 0;
    owner->executing = 1;
    if (!owner_thread_matches(owner)) {
        owner->executing = 0;
        return 0;
    }
    return 1;
}

static int owner_context_matches(const MusashiGteOwner *owner,
                                 const MusashiCpuContext *context) {
    return context != NULL &&
           context->provenance == MUSASHI_CPU_CONTEXT_SOURCE &&
           context->identity == owner->context_identity;
}

static int gte_control_in_write_profile(uint32_t selector) {
    /* Rotation 0..4 and translation 5..7 used by SetTransMatrix. */
    return selector <= 7u ||
           (selector >= MUSASHI_GTE_CONTROL_BACKGROUND_FIRST &&
            selector <= MUSASHI_GTE_CONTROL_BACKGROUND_LAST) ||
           (selector >= MUSASHI_GTE_CONTROL_LIGHTCOLOR_FIRST &&
            selector <= MUSASHI_GTE_CONTROL_LIGHTCOLOR_LAST) ||
           (selector >= MUSASHI_GTE_CONTROL_FIRST &&
            selector <= MUSASHI_GTE_CONTROL_LAST);
}

int musashi_gte_owner_init(MusashiGteOwner *owner,
                           MusashiCpuStatus *cpu_status,
                           MusashiGteThreadToken current_thread,
                           void *thread_userdata,
                           const void *context_identity) {
    uint64_t thread;

    if (owner_reentry(owner)) return 0;
    if (owner == NULL || cpu_status == NULL || current_thread == NULL ||
        context_identity == NULL) return 0;
    if (owner->initialized || owner->executing || g_active_owner != NULL) {
        if (owner->executing) {
            owner->faulted = 1;
            owner->reentry_violation = 1;
        }
        return 0;
    }

    /* Reserve the lease while asking the caller's thread adapter.  A nested
     * init from that callback sees executing and cannot publish a half-owner. */
    g_active_owner = owner;
    owner->executing = 1;
    owner->reentry_violation = 0;
    thread = current_thread(thread_userdata);
    if (owner->reentry_violation) {
        owner->faulted = 1;
        owner->executing = 0;
        g_active_owner = NULL;
        return 0;
    }

    owner->cpu_status = cpu_status;
    owner->current_thread = current_thread;
    owner->thread_userdata = thread_userdata;
    owner->owner_thread = thread;
    owner->context_identity = context_identity;
    owner->write_count = 0;
    owner->data_write_count = 0;
    owner->data_read_count = 0;
    owner->command_count = 0;
    owner->initialized = 1;
    owner->faulted = 0;
    owner->executing = 0;
    owner->reentry_violation = 0;
    return 1;
}

int musashi_gte_owner_write_control(MusashiGteOwner *owner,
                                    const MusashiCpuContext *context,
                                    uint32_t selector, uint32_t value) {
    uint32_t status;

    if (owner_reentry(owner)) return 0;
    if (!gte_control_in_write_profile(selector)) return 0;
    if (owner == NULL || owner->write_count == UINT64_MAX) return 0;
    if (!owner_begin(owner, 0)) return 0;
    if (!owner_context_matches(owner, context)) {
        owner->executing = 0;
        return 0;
    }
    status = 0;
    if (!musashi_cpu_status_read(owner->cpu_status, &status) ||
        owner->reentry_violation || owner->faulted ||
        (status & MUSASHI_CPU_STATUS_CU2) == 0) {
        owner->executing = 0;
        return 0;
    }

    /* CTC2 performs the source-backed sign extension/masking rules for H,
     * DQA, ZSF3, ZSF4 and FLAG.  No shadow is updated before this call. */
    CTC2(value, (int)selector);
    if (owner->reentry_violation || owner->faulted) {
        owner->executing = 0;
        return 0;
    }
    owner->write_count++;
    owner->executing = 0;
    return 1;
}

int musashi_gte_owner_read_control(MusashiGteOwner *owner,
                                   const MusashiCpuContext *context,
                                   uint32_t selector, uint32_t *value) {
    uint32_t status;
    unsigned int live;

    if (owner_reentry(owner)) return 0;
    if (owner == NULL || value == NULL) return 0;
    /* FLAG31 is read-only here; CFC2 must observe control, not LZCR. */
    if (selector != 31u && selector != MUSASHI_GTE_CONTROL_LIGHT_READ &&
        !gte_control_in_write_profile(selector)) return 0;
    if (!owner_begin(owner, 0)) return 0;
    if (!owner_context_matches(owner, context)) {
        owner->executing = 0;
        return 0;
    }
    status = 0;
    if (!musashi_cpu_status_read(owner->cpu_status, &status) ||
        owner->reentry_violation || owner->faulted ||
        (status & MUSASHI_CPU_STATUS_CU2) == 0) {
        owner->executing = 0;
        return 0;
    }

    /* CFC2 reads the shared control bank with no side effects. (MFC2 reads
     * the data bank and can clobber it, so it must not back control reads.)
     * No counter advances: this is an observation, and SWC2-style stores
     * compose it with a RAM write at the execution layer. */
    live = CFC2((int)selector);
    if (owner->reentry_violation || owner->faulted) {
        owner->executing = 0;
        return 0;
    }
    *value = (uint32_t)live;
    owner->executing = 0;
    return 1;
}

/* The source profile owns CPU instruction validation; here validate the live
 * lease/context and CU2 before touching either canonical data register. */
static int owner_begin_data(MusashiGteOwner *owner,
                            const MusashiCpuContext *context) {
    uint32_t status = 0;
    if (!owner_begin(owner, 0)) return 0;
    if (!owner_context_matches(owner, context) || context->gpr == NULL ||
        context->instruction_valid != 1 ||
        !musashi_cpu_status_read(owner->cpu_status, &status) ||
        owner->reentry_violation || owner->faulted ||
        (status & MUSASHI_CPU_STATUS_CU2) == 0) {
        owner->executing = 0;
        return 0;
    }
    return 1;
}

int musashi_gte_owner_write_data(MusashiGteOwner *owner,
                                const MusashiCpuContext *context,
                                uint32_t selector, uint32_t value) {
    uint32_t normalized;
    uint32_t count = 0;
    if (owner_reentry(owner)) return 0;
    if (owner == NULL ||
        (selector != 0u && selector != 1u && selector != 2u && selector != 3u &&
         selector != 4u && selector != 5u && selector != 6u && selector != 8u &&
         selector != 9u && selector != 10u && selector != 11u &&
         selector != 19u && selector != 28u && selector != 30u &&
         selector != 31u) ||
        owner->data_write_count == UINT64_MAX) return 0;
    if (!owner_begin_data(owner, context)) return 0;
    /* Plain 32-bit MTC2 moves: vectors V0/V1, the 80047F48 write of the
     * func_80047EC8 interpolation factor into IR0 (the GPF at 80047F60
     * scales IR1..IR3 by it), plus the 800484EC sign and magnitude loads
     * into IR1/IR2/IR3. Retail MVMVA with v=3 then reads those IR halves as
     * its vector. IR0's own reads are the low signed halfword of the
     * register, so a full-width store is the hardware behaviour. */
    if (selector == 0u || selector == 1u || selector == 2u || selector == 3u ||
        selector == 4u || selector == 5u || selector == 6u || selector == 8u ||
        selector == 9u || selector == 10u || selector == 11u) {
        gteRegs.CP2D.p[selector].d = value;
        owner->data_write_count++;
        owner->executing = 0;
        return 1;
    }

    /* LZCR is read-only: the vendor helper ignores the write, and so does the
     * hardware, but the site is still a legal LWC2. */
    if (selector == 31u) {
        owner->data_write_count++;
        owner->executing = 0;
        return 1;
    }

    /* IRGB is not a plain store: MTC2/LWC2 unpack it into IR1..IR3, so the
     * vendor helper performs that split rather than a raw word write. */
    if (selector == 28u) {
        MTC2(value, 28);
        owner->data_write_count++;
        owner->executing = 0;
        return 1;
    }

    /* Count leading sign bits, including the sign itself. The pristine
     * PsyCross MTC2 helper only counts zero bits on its active fast path, so
     * do not delegate negative LZCS inputs to that helper. Unsigned shifts
     * make all-zero/all-one and bit31 edges defined without builtin clz(0). */
    normalized = (value & 0x80000000u) != 0 ? ~value : value;
    while (count < 32u && (normalized & 0x80000000u) == 0) {
        normalized <<= 1;
        ++count;
    }
    /* All fallible callbacks precede this indivisible owner-thread commit.
     * These are the renderer's actual CP2D registers, never a shadow bank. */
    gteRegs.CP2D.p[30].d = value;
    gteRegs.CP2D.p[31].d = count;
    owner->data_write_count++;
    owner->executing = 0;
    return 1;
}

int musashi_gte_owner_read_data(MusashiGteOwner *owner,
                               const MusashiCpuContext *context,
                               uint32_t selector, uint32_t *value) {
    uint32_t local;
    if (owner_reentry(owner)) return 0;
    if (owner == NULL || value == NULL ||
        (selector != 0u && selector != 4u && selector != 5u &&
         selector != 6u && selector != 7u && selector != 8u && selector != 9u &&
         selector != 10u && selector != 11u && selector != 12u &&
         selector != 13u && selector != 14u && selector != 16u &&
         selector != 17u && selector != 18u && selector != 19u &&
         selector != 20u && selector != 21u && selector != 22u &&
         selector != 24u && selector != 25u && selector != 26u &&
         selector != 27u && selector != 29u && selector != 30u &&
         selector != 31u) ||
        owner->data_read_count == UINT64_MAX) return 0;
    if (!owner_begin_data(owner, context)) return 0;
    /* IR1..3 reads sign-extend their low half; commands may leave stale
     * upper halves. MFC2 also preserves raw MAC/LZCS/LZCR read semantics.
     * The selector list is the union of the registers retail MFC2/SWC2 sites
     * actually read: VZ2, RGB, OTZ, IR0, SXY0/1/2, SZ3, MAC1..3, ORGB, LZCS and
     * LZCR, plus the RGB0..2 colour FIFO: libgs GsTMDfastG3GL stores RGB2 with
     * SWC2 (80057A58, reached at the opening scene's mode 7->8 transition).
     * RGB0..2 read back as plain words, as PsyCross's MFC2 does. */
    local = MFC2((int)selector);
    owner->data_read_count++;
    *value = local;
    owner->executing = 0;
    return 1;
}

int musashi_gte_owner_command(MusashiGteOwner *owner,
                              const MusashiCpuContext *context,
                              uint32_t word) {
    int accepted;
    if (owner_reentry(owner)) return 0;
    if (owner == NULL || owner->command_count == UINT64_MAX) return 0;
    if (!owner_begin_data(owner, context)) return 0;
    /* The operator runs synchronously against the shared bank; an
     * unhandled function code leaves the bank and counters untouched. */
    accepted = doCOP2((int)word);
    if (owner->reentry_violation || owner->faulted || !accepted) {
        owner->executing = 0;
        return 0;
    }
    owner->command_count++;
    owner->executing = 0;
    return 1;
}

/* Native-lane lease (port-plat's BfmPsyqGteBridge): host libgte wrappers
 * run PsyCross GTE code on this owner's shared gteRegs bank. begin succeeds
 * only on the owner thread with CU2 set, initialized and not faulted or
 * already executing, and marks the owner executing, so an interpreter COP2
 * operation nested inside the native call trips the reentry check. A
 * native call is not a source context, so no context identity is checked. */
int musashi_gte_owner_native_begin(MusashiGteOwner *owner) {
    uint32_t status = 0;
    if (owner == NULL || !owner->initialized || owner->faulted || owner->executing)
        return 0;
    owner->reentry_violation = 0;
    owner->executing = 1;
    if (!owner_thread_matches(owner) ||
        !musashi_cpu_status_read(owner->cpu_status, &status) ||
        (status & MUSASHI_CPU_STATUS_CU2) == 0 || owner->faulted) {
        owner->executing = 0;
        return 0;
    }
    return 1;
}

void musashi_gte_owner_native_end(MusashiGteOwner *owner, uint32_t commands) {
    if (owner == NULL || !owner->executing) return;
    if (owner->command_count <= UINT64_MAX - commands) owner->command_count += commands;
    else owner->faulted = 1;
    owner->executing = 0;
}

int musashi_gte_owner_snapshot(MusashiGteOwner *owner,
                               MusashiGteSnapshot *snapshot) {
    MusashiGteSnapshot local;
    unsigned i;

    if (owner_reentry(owner) || snapshot == NULL ||
        !owner_begin(owner, 1)) return 0;
    memset(&local, 0, sizeof(local));
    for (i = 0; i < MUSASHI_GTE_CONTROL_COUNT; ++i)
        local.control[i] = gteRegs.CP2C.p[i].d;
    for (i = 0; i < MUSASHI_GTE_DATA_COUNT; ++i)
        local.data[i] = gteRegs.CP2D.p[i].d;
    local.write_count = owner->write_count;
    local.data_write_count = owner->data_write_count;
    local.data_read_count = owner->data_read_count;
    local.command_count = owner->command_count;
    local.initialized = owner->initialized;
    local.faulted = owner->faulted;
    if (owner->reentry_violation) {
        owner->executing = 0;
        return 0;
    }
    *snapshot = local;
    owner->executing = 0;
    return 1;
}

int musashi_gte_owner_close(MusashiGteOwner *owner) {
    if (owner_reentry(owner)) return 0;
    if (owner == NULL || !owner->initialized) return 0;
    owner->reentry_violation = 0;
    owner->executing = 1;
    if (!owner_thread_matches(owner) || owner->reentry_violation) {
        owner->executing = 0;
        return 0;
    }
    owner->executing = 0;
    owner->initialized = 0;
    if (g_active_owner == owner) g_active_owner = NULL;
    return 1;
}
