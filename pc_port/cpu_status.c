/* Selected SCPH-5500 Status policy; see gpu-cp0-startup-authority.md.
 * This owns SR and exception transitions, not a general CP0 interpreter. */
#include "musashi_cpu_status.h"
#include <stdlib.h>
#include <string.h>

struct MusashiCpuStatus {
    uint64_t (*current_thread)(void *);
    void *userdata;
    uint64_t thread;
    int entered, violation;
    MusashiCpuStatusSnapshot state;
};
static int enter(MusashiCpuStatus *o, int diagnostics) {
    uint64_t thread;
    if (!o) return 0;
    if (o->entered) {
        o->violation = 1; o->state.faulted = 1; return 0;
    }
    o->entered = 1; o->violation = 0;
    thread = o->current_thread(o->userdata);
    if (thread != o->thread || !thread || o->violation ||
        (!diagnostics && o->state.faulted)) { o->entered = 0; return 0; }
    return 1;
}
static int leave(MusashiCpuStatus *o, int ok) {
    o->entered = 0; return ok;
}
static int refuse(MusashiCpuStatus *o) {
    o->state.faulted = 1; return leave(o, 0);
}
static int token_valid(const MusashiCpuStatus *o, const MusashiCpuExceptionToken *t) {
    return t && o->state.active && t->owner == o &&
        t->identity == o->state.identity && t->sequence == o->state.sequence;
}
static int context_valid(const MusashiCpuContext *c) {
    if (!c || !c->identity || !c->gpr || c->gpr[0] ||
        (c->instruction_valid != 0 && c->instruction_valid != 1) ||
        (c->delay_slot != 0 && c->delay_slot != 1)) return 0;
    if (c->provenance == MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE)
        return !c->pc && !c->npc && !c->instruction && !c->branch_pc &&
            !c->instruction_valid && !c->delay_slot;
    if (c->provenance != MUSASHI_CPU_CONTEXT_SOURCE || !c->instruction_valid ||
        ((c->pc | c->npc) & 3u)) return 0;
    if (c->delay_slot)
        return c->branch_pc <= UINT32_MAX - 4u && c->branch_pc + 4u == c->pc;
    return c->branch_pc == 0;
}
static int open_with_sr(MusashiCpuStatus **out, uint64_t (*thread)(void *),
                        void *userdata, uint32_t sr) {
    MusashiCpuStatus *o; uint64_t identity;
    if (!out || *out || !thread || !(identity = thread(userdata))) return 0;
    o = calloc(1, sizeof *o);
    if (!o) return 0;
    o->current_thread = thread; o->userdata = userdata; o->thread = identity;
    o->state.sr = sr; *out = o; return 1;
}
int musashi_cpu_status_open(MusashiCpuStatus **out,
                            uint64_t (*thread)(void *), void *userdata) {
    return open_with_sr(out, thread, userdata, UINT32_C(0x40000000));
}
#ifdef MUSASHI_CPU_STATUS_TESTING
int musashi_cpu_status_open_fixture(MusashiCpuStatus **out,
    uint64_t (*thread)(void *), void *userdata, uint32_t sr) {
    return open_with_sr(out, thread, userdata, sr);
}
#endif
int musashi_cpu_status_close(MusashiCpuStatus *o) {
    if (!enter(o, 1)) return 0;
    if (o->state.active && !o->state.faulted) return leave(o, 0);
    free(o); return 1;
}
int musashi_cpu_status_snapshot(MusashiCpuStatus *o, MusashiCpuStatusSnapshot *out) {
    if (!enter(o, 1)) return 0;
    if (!out) return refuse(o);
    *out = o->state; return leave(o, 1);
}
int musashi_cpu_status_inspect(MusashiCpuStatus *o,
    const MusashiCpuExceptionToken *t, MusashiCpuStatusSnapshot *out) {
    if (!enter(o, 1)) return 0;
    if (!out || !token_valid(o, t)) return refuse(o);
    *out = o->state; return leave(o, 1);
}
int musashi_cpu_status_read(MusashiCpuStatus *o, uint32_t *out) {
    if (!enter(o, 0)) return 0;
    if (!out) return refuse(o);
    *out = o->state.sr; return leave(o, 1);
}
int musashi_cpu_status_irq_eligible(MusashiCpuStatus *o, int *out) {
    if (!enter(o, 0)) return 0;
    if (!out) return refuse(o);
    *out = (o->state.sr & 0x401u) == 0x401u; return leave(o, 1);
}
int musashi_cpu_status_set_enabled(MusashiCpuStatus *o, int enabled) {
    if (!enter(o, 0)) return 0;
    if (o->state.active || (enabled != 0 && enabled != 1)) return refuse(o);
    o->state.sr = (o->state.sr & ~UINT32_C(0x401)) | (enabled ? 0x401u : 0u);
    return leave(o, 1);
}
int musashi_cpu_status_write_source(MusashiCpuStatus *o,
    const MusashiCpuContext *c, uint32_t value) {
    if (!enter(o, 0)) return 0;
    if (o->state.active || !context_valid(c) ||
        c->provenance != MUSASHI_CPU_CONTEXT_SOURCE ||
        (c->instruction & UINT32_C(0xffe0ffff)) != UINT32_C(0x40806000) ||
        c->gpr[(c->instruction >> 16) & 31u] != value ||
        (value != o->state.sr && value != (o->state.sr | UINT32_C(0x40000000)) &&
         /* PSX SwC has no observed cache effect; preserve its status bit.
          * Authority: psx-spx CPU specifications, cop0r12 bit 17. */
         !(c->pc == 0x800d3534u && c->instruction == ((0x10u << 26) | (4u << 21) | (9u << 16) | (12u << 11)) /* mtc0 $9, $12 */ &&
           value == (o->state.sr | UINT32_C(0x00020000)))))
        return refuse(o);
    o->state.sr = value; return leave(o, 1);
}
int musashi_cpu_status_begin(MusashiCpuStatus *o, MusashiCpuExceptionKind kind,
    const MusashiCpuContext *c, uint32_t pending, MusashiCpuExceptionToken *out) {
    MusashiCpuStatusSnapshot next;
    if (!enter(o, 0)) return 0;
    if (!out || o->state.active || o->state.sequence == UINT64_MAX ||
        !context_valid(c) || (pending & ~UINT32_C(0x400)) ||
        (o->state.sr & 2u) || kind < MUSASHI_CPU_EXCEPTION_IRQ ||
        kind > MUSASHI_CPU_EXCEPTION_SYS2) return refuse(o);
    if (kind == MUSASHI_CPU_EXCEPTION_IRQ) {
        if (!pending || (o->state.sr & 0x401u) != 0x401u) return refuse(o);
        /* The selected exception handler's COP2 completion/EPC adjustment
         * requires a command pipeline, outside the current owned profile. */
        if (c->provenance == MUSASHI_CPU_CONTEXT_SOURCE &&
            ((c->instruction >> 24) & 0xfeu) == 0x4au) return refuse(o);
    } else if (c->provenance == MUSASHI_CPU_CONTEXT_SOURCE &&
        (c->instruction != 12u || c->gpr[4] !=
         (kind == MUSASHI_CPU_EXCEPTION_SYS1 ? 1u : 2u))) return refuse(o);
    memset(&next, 0, sizeof next);
    next.sr = (o->state.sr & UINT32_C(0xffffffc0)) | ((o->state.sr & 15u) << 2);
    next.saved_sr = next.sr;
    memcpy(next.gpr, c->gpr, sizeof next.gpr);
    next.pc = c->pc; next.npc = c->npc; next.instruction = c->instruction;
    next.branch_pc = c->branch_pc; next.instruction_valid = c->instruction_valid;
    next.delay_slot = c->delay_slot; next.identity = c->identity;
    next.provenance = c->provenance; next.kind = kind;
    next.sequence = o->state.sequence + 1u; next.active = 1;
    if (c->provenance == MUSASHI_CPU_CONTEXT_SOURCE) {
        next.cause_epc_valid = 1;
        next.cause = pending | (kind == MUSASHI_CPU_EXCEPTION_IRQ ? 0u : 0x20u) |
            (c->delay_slot ? UINT32_C(0x80000000) : 0u);
        next.epc = c->delay_slot ? c->branch_pc : c->pc;
    }
    o->state = next;
    out->owner = o; out->identity = c->identity; out->sequence = next.sequence;
    return leave(o, 1);
}
int musashi_cpu_status_accept_kernel_entry(MusashiCpuStatus *o,
    const MusashiCpuExceptionToken *t) {
    if (!enter(o, 0)) return 0;
    if (!token_valid(o, t) || o->state.kernel_entered) return refuse(o);
    o->state.kernel_entered = 1; return leave(o, 1);
}
int musashi_cpu_status_apply_sys(MusashiCpuStatus *o,
    const MusashiCpuExceptionToken *t, int32_t *result) {
    if (!enter(o, 0)) return 0;
    if (!token_valid(o, t) || !o->state.kernel_entered || o->state.sys_applied ||
        o->state.kind == MUSASHI_CPU_EXCEPTION_IRQ ||
        (o->state.kind == MUSASHI_CPU_EXCEPTION_SYS1 && !result)) return refuse(o);
    if (o->state.kind == MUSASHI_CPU_EXCEPTION_SYS1) {
        int32_t previous = (o->state.saved_sr & 0x404u) == 0x404u;
        o->state.saved_sr &= UINT32_C(0xfffffbfb);
        *result = previous;
    } else o->state.saved_sr |= 0x404u;
    o->state.sys_applied = 1; return leave(o, 1);
}
int musashi_cpu_status_finish(MusashiCpuStatus *o, const MusashiCpuExceptionToken *t) {
    if (!enter(o, 0)) return 0;
    if (!token_valid(o, t) || !o->state.kernel_entered ||
        (o->state.kind != MUSASHI_CPU_EXCEPTION_IRQ && !o->state.sys_applied))
        return refuse(o);
    o->state.sr = (o->state.saved_sr & UINT32_C(0xfffffff0)) |
        ((o->state.saved_sr >> 2) & 15u);
    o->state.active = 0; return leave(o, 1);
}
int musashi_cpu_status_fault(MusashiCpuStatus *o, const MusashiCpuExceptionToken *t) {
    if (!enter(o, 1)) return 0;
    if (!token_valid(o, t)) return refuse(o);
    o->state.faulted = 1; return leave(o, 1);
}
