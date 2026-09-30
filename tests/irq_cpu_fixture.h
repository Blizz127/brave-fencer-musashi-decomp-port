#ifndef MUSASHI_TEST_IRQ_CPU_FIXTURE_H
#define MUSASHI_TEST_IRQ_CPU_FIXTURE_H
/* Explicit synthetic CPU bank and native-service boundary for existing device
 * fixtures. This is not the live formatter context or real BIOS-image proof. */
#include "musashi_irq_scheduler.h"
#include <SDL.h>
#include <assert.h>
#include <string.h>
static uint64_t fixture_irq_thread(void *unused) {
    (void)unused; return (uint64_t)SDL_ThreadID();
}
static int fixture_irq_context(void *unused, const void *identity, MusashiCpuContext *out) {
    static const uint32_t fixture_gpr[32] = {0};
    (void)unused;
    memset(out, 0, sizeof *out);
    out->identity = identity; out->gpr = fixture_gpr;
    out->provenance = MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE;
    return 1;
}
static int fixture_irq_kernel_entry(void *userdata, uint16_t pending) {
    MusashiPsyCrossIrqScheduler *s = userdata;
    MusashiCpuStatusSnapshot state;
    assert(pending && s->active_continuation && s->delivering);
    assert(musashi_cpu_status_inspect(s->cpu_status, &s->active_exception, &state));
    assert(state.active && !state.faulted && state.kind == MUSASHI_CPU_EXCEPTION_IRQ);
    assert(!state.cause_epc_valid && state.sr == 0x40000404u);
    return musashi_cpu_status_accept_kernel_entry(s->cpu_status, &s->active_exception);
}
static void fixture_irq_cpu_attach(MusashiPsyCrossIrqScheduler *s) {
    if (!s->cpu_status) {
        MusashiCpuStatus *cpu = NULL;
        assert(musashi_cpu_status_open(&cpu, fixture_irq_thread, NULL));
        assert(musashi_psycross_irq_scheduler_bind_cpu(s, cpu));
    }
    assert(musashi_psycross_irq_scheduler_bind_context(s, fixture_irq_context, NULL));
    s->before_dispatch = fixture_irq_kernel_entry;
    s->before_dispatch_userdata = s;
}
/* Standalone fixtures own their allocation; runtime fixtures use runtime_close. */
static inline void fixture_irq_cpu_close(MusashiPsyCrossIrqScheduler *s) {
    assert(musashi_cpu_status_close(s->cpu_status)); s->cpu_status = NULL;
}
#endif
