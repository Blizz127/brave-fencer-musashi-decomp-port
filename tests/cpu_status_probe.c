/* Synthetic thread/context fixtures; equations independently model the selected
 * ROM's saved Status manipulation. No emulator state or host BIOS completion. */
#include "musashi_cpu_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct Fixture { uint64_t thread; MusashiCpuStatus *owner; int reenter; } Fixture;
static uint64_t thread_id(void *p) {
    Fixture *f = p;
    if (f->reenter) {
        MusashiCpuStatusSnapshot s;
        f->reenter = 0;
        assert(!musashi_cpu_status_snapshot(f->owner, &s));
    }
    return f->thread;
}
static MusashiCpuContext context(uint32_t *g, int *identity) {
    MusashiCpuContext c;
    memset(&c, 0, sizeof c);
    c.identity = identity; c.gpr = g;
    c.pc = 0x8005cf0c; c.npc = c.pc + 4;
    c.instruction = 12; c.instruction_valid = 1;
    c.provenance = MUSASHI_CPU_CONTEXT_SOURCE;
    return c;
}
static uint32_t pushed(uint32_t s) {
    uint32_t upper = s & ~63u;
    return upper | ((s % 16u) * 4u);
}
static uint32_t returned(uint32_t s) {
    return (s & ~15u) | ((s / 4u) % 16u);
}
static void equations(void) {
    unsigned low, enabled, selector;
    for (low = 0; low < 64; ++low) for (enabled = 0; enabled < 2; ++enabled)
    for (selector = 1; selector <= 2; ++selector) {
        Fixture f = {7, NULL, 0}; uint32_t g[32] = {0}; int id;
        uint32_t initial = 0x40000000u | low | (enabled << 10), readback;
        MusashiCpuContext c = context(g, &id);
        MusashiCpuExceptionToken t; MusashiCpuStatusSnapshot s;
        int32_t result = 91; uint32_t saved = pushed(initial);
        if (low & 2u) continue; /* user mode not owned by this profile */
        g[1] = 0x12345678; g[2] = 0x87654321; g[3] = 0x11223344;
        g[31] = 0x8004d178; g[4] = selector;
        assert(musashi_cpu_status_open_fixture(&f.owner, thread_id, &f, initial));
        assert(musashi_cpu_status_begin(f.owner, selector == 1 ?
            MUSASHI_CPU_EXCEPTION_SYS1 : MUSASHI_CPU_EXCEPTION_SYS2, &c, 0x400, &t));
        g[1] = 99; /* token owns entry snapshot, not a late reread */
        assert(musashi_cpu_status_inspect(f.owner, &t, &s));
        assert(s.gpr[1] == 0x12345678 && s.gpr[31] == 0x8004d178);
        assert(s.sr == saved && s.saved_sr == saved);
        assert(s.cause == 0x420 && s.epc == c.pc && s.cause_epc_valid);
        assert(musashi_cpu_status_accept_kernel_entry(f.owner, &t));
        assert(musashi_cpu_status_apply_sys(f.owner, &t, &result));
        if (selector == 1) {
            assert(result == ((initial & 0x401) == 0x401)); saved &= ~0x404u;
        } else { assert(result == 91); saved |= 0x404; }
        assert(musashi_cpu_status_finish(f.owner, &t));
        assert(musashi_cpu_status_read(f.owner, &readback));
        assert(readback == returned(saved));
        assert(musashi_cpu_status_close(f.owner));
    }
}
static void normal(void) {
    Fixture f = {7, NULL, 0}; uint32_t g[32] = {0}; int id, eligible;
    MusashiCpuContext c = context(g, &id);
    MusashiCpuExceptionToken t; MusashiCpuStatusSnapshot s; uint32_t sr;
    assert(musashi_cpu_status_open(&f.owner, thread_id, &f));
    assert(musashi_cpu_status_read(f.owner, &sr) && sr == 0x40000000);
    assert(musashi_cpu_status_irq_eligible(f.owner, &eligible) && !eligible);
    assert(musashi_cpu_status_set_enabled(f.owner, 1));
    c.pc = 0x80012344; c.npc = 0x80023450; c.branch_pc = c.pc - 4;
    c.delay_slot = 1; c.instruction = 0;
    assert(musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_IRQ, &c, 0x400, &t));
    assert(musashi_cpu_status_inspect(f.owner, &t, &s));
    assert(s.sr == 0x40000404 && s.epc == 0x80012340 && s.cause == 0x80000400);
    assert(!musashi_cpu_status_close(f.owner)); /* no fabricated RFE */
    assert(musashi_cpu_status_accept_kernel_entry(f.owner, &t));
    assert(musashi_cpu_status_finish(f.owner, &t));
    assert(musashi_cpu_status_read(f.owner, &sr) && sr == 0x40000401);
    c = context(g, &id); c.provenance = MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE;
    c.pc = c.npc = c.instruction = 0; c.instruction_valid = 0;
    assert(musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_SYS1, &c, 0x400, &t));
    assert(musashi_cpu_status_inspect(f.owner, &t, &s));
    assert(!s.cause_epc_valid && !s.cause && !s.epc);
    assert(musashi_cpu_status_accept_kernel_entry(f.owner, &t));
    { int32_t prior = -1; assert(musashi_cpu_status_apply_sys(f.owner, &t, &prior)); assert(prior == 1); }
    assert(musashi_cpu_status_finish(f.owner, &t));
    assert(musashi_cpu_status_close(f.owner));
    f.owner = NULL;
    assert(musashi_cpu_status_open_fixture(&f.owner, thread_id, &f, 0x401));
    c = context(g, &id); c.pc = 0x80047cdc; c.npc = c.pc + 4;
    c.instruction = 0x40826000; g[2] = 0x40000401;
    assert(musashi_cpu_status_write_source(f.owner, &c, g[2]));
    assert(musashi_cpu_status_read(f.owner, &sr) && sr == g[2]);
    c.pc = 0x800d3534; c.npc = c.pc + 4; c.instruction = 0x40896000;
    g[9] = sr | 0x20000;
    assert(musashi_cpu_status_write_source(f.owner, &c, g[9]));
    assert(musashi_cpu_status_read(f.owner, &sr) && sr == 0x40020401);
    assert(musashi_cpu_status_close(f.owner));
}
static void refusal(unsigned which) {
    Fixture f = {7, NULL, 0}; uint32_t g[32] = {0}; int id, other;
    MusashiCpuContext c = context(g, &id);
    MusashiCpuExceptionToken t = {0}, bad; MusashiCpuStatusSnapshot before, after;
    int32_t result = 313; int ok = 1; g[4] = 1;
    assert(musashi_cpu_status_open(&f.owner, thread_id, &f));
    assert(musashi_cpu_status_set_enabled(f.owner, 1));
    if (which <= 10) {
        assert(musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_SYS1, &c, 0, &t));
        assert(musashi_cpu_status_snapshot(f.owner, &before));
        bad = t;
        switch (which) {
        case 0: ok = musashi_cpu_status_finish(f.owner, &t); break;
        case 1: ++bad.sequence; ok = musashi_cpu_status_inspect(f.owner, &bad, &after); break;
        case 2: bad.identity = &other; ok = musashi_cpu_status_inspect(f.owner, &bad, &after); break;
        case 3: bad.owner = NULL; ok = musashi_cpu_status_finish(f.owner, &bad); break;
        case 4: ok = musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_SYS1, &c, 0, &bad); break;
        case 5: ok = musashi_cpu_status_apply_sys(f.owner, &t, &result); break;
        case 6: assert(musashi_cpu_status_accept_kernel_entry(f.owner, &t));
                ok = musashi_cpu_status_accept_kernel_entry(f.owner, &t); break;
        case 7: assert(musashi_cpu_status_accept_kernel_entry(f.owner, &t));
                ok = musashi_cpu_status_finish(f.owner, &t); break;
        case 8: assert(musashi_cpu_status_fault(f.owner, &t));
                ok = musashi_cpu_status_finish(f.owner, &t); break;
        case 9: ok = musashi_cpu_status_set_enabled(f.owner, 0); break;
        case 10: f.reenter = 1; ok = musashi_cpu_status_snapshot(f.owner, &after); break;
        }
        assert(!ok && result == 313);
        assert(musashi_cpu_status_snapshot(f.owner, &after));
        assert(after.active && after.faulted && after.sr == before.sr);
        assert(after.saved_sr == before.saved_sr && after.sequence == before.sequence);
        assert(!memcmp(after.gpr, before.gpr, sizeof after.gpr));
    } else {
        assert(musashi_cpu_status_snapshot(f.owner, &before));
        switch (which) {
        case 11: c.delay_slot = 1; break;
        case 12: c.instruction_valid = 0; break;
        case 13: c.gpr = NULL; break;
        case 14: c.identity = NULL; break;
        case 15: c.pc |= 1; break;
        case 16: c.instruction = 0; break;
        case 17: g[4] = 2; break;
        case 18: c.provenance = MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE; break;
        case 19: c.provenance = 0; break;
        case 20: c.delay_slot = 2; break;
        case 21: c.branch_pc = c.pc - 4; break;
        }
        assert(!musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_SYS1, &c, 0, &t));
        assert(musashi_cpu_status_snapshot(f.owner, &after));
        assert(after.sr == before.sr && !after.active && !t.owner);
    }
    assert(musashi_cpu_status_close(f.owner));
}
static void boundaries(void) {
    unsigned which;
    for (which = 0; which < 16; ++which) {
        Fixture f = {7, NULL, 0}; uint32_t g[32] = {0}; int id;
        MusashiCpuContext c = context(g, &id);
        MusashiCpuExceptionToken t = {0}, old; MusashiCpuStatusSnapshot s;
        uint32_t value = 0x11111111; int ok = 1;
        if (which == 0) { f.thread = 0;
            assert(!musashi_cpu_status_open(&f.owner, thread_id, &f));
            assert(!f.owner); continue; }
        assert(musashi_cpu_status_open(&f.owner, thread_id, &f));
        if (which == 1) {
            f.thread = 8;
            assert(!musashi_cpu_status_read(f.owner, &value));
            assert(value == 0x11111111 && !musashi_cpu_status_close(f.owner));
            f.thread = 7;
            assert(musashi_cpu_status_snapshot(f.owner, &s) && !s.faulted);
            assert(musashi_cpu_status_close(f.owner)); continue;
        }
        if (which == 2) {
            assert(!musashi_cpu_status_open(&f.owner, thread_id, &f));
            assert(musashi_cpu_status_close(f.owner)); continue;
        }
        if (which != 3) assert(musashi_cpu_status_set_enabled(f.owner, 1));
        c.instruction = 0;
        if (which == 4) c.instruction = 0x4a000001;
        if (which == 5) c.instruction = 0x4bffffff;
        if (which <= 6) {
            ok = musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_IRQ,
                &c, which == 6 ? 4u : 0x400u, &t);
            assert(!ok && !t.owner);
        } else if (which == 7) {
            c.provenance = MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE;
            c.pc = c.npc = 0; c.instruction_valid = 0;
            assert(musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_IRQ, &c, 0x400, &t));
            assert(musashi_cpu_status_inspect(f.owner, &t, &s));
            assert(!s.cause_epc_valid && !s.cause && !s.epc);
            assert(musashi_cpu_status_accept_kernel_entry(f.owner, &t));
            assert(musashi_cpu_status_finish(f.owner, &t));
        } else if (which == 15) {
            assert(musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_IRQ, &c, 0x400, &t));
            assert(!musashi_cpu_status_finish(f.owner, &t));
        } else if (which == 8 || which == 9) {
            assert(musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_IRQ, &c, 0x400, &t));
            assert(musashi_cpu_status_accept_kernel_entry(f.owner, &t));
            if (which == 8) { int32_t v = 12;
                assert(!musashi_cpu_status_apply_sys(f.owner, &t, &v) && v == 12);
            } else {
                assert(musashi_cpu_status_finish(f.owner, &t)); old = t;
                assert(musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_IRQ, &c, 0x400, &t));
                assert(t.sequence == old.sequence + 1);
                assert(!musashi_cpu_status_finish(f.owner, &old));
            }
        } else if (which == 10) {
            assert(musashi_cpu_status_begin(f.owner, MUSASHI_CPU_EXCEPTION_IRQ, &c, 0x400, &t));
            assert(musashi_cpu_status_accept_kernel_entry(f.owner, &t));
            assert(musashi_cpu_status_finish(f.owner, &t));
            assert(!musashi_cpu_status_finish(f.owner, &t));
        } else {
            c.instruction = 0x40826000; g[2] = 0x40000401;
            if (which == 11) { g[2] = 0x40000001; }
            if (which == 12) { c.instruction = 0x40826800; }
            if (which == 13) { c.instruction = 0x40826001; }
            if (which == 14) { g[2] = 0; }
            assert(!musashi_cpu_status_write_source(f.owner, &c,
                which == 14 ? 0x40000401u : g[2]));
            assert(musashi_cpu_status_snapshot(f.owner, &s) && s.sr == 0x40000401);
        }
        assert(musashi_cpu_status_close(f.owner));
    }
}
int main(void) {
    unsigned i; equations(); normal(); boundaries();
    for (i = 0; i <= 21; ++i) refusal(i);
    puts("CPU_STATUS_PASS synthetic_contexts=1"); return 0;
}
