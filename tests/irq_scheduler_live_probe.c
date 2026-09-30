#include "irq_cpu_fixture.h"
/* Observe PsyCross worker source publication and owner-thread IRQ delivery.
 *
 * The RAM/device fixture is deliberately synthetic. The event source is not:
 * PsyX_Initialise owns the SDL VBlank thread that invokes the scheduler. This
 * verifies a platform boundary, not native startup or title/menu behavior. */
#include "musashi_irq_scheduler.h"
#include "musashi_irq_controller.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "PsyX/PsyX_public.h"
#include "psx/libetc.h"
void musashi_psyx_shutdown(void); /* pc_port/psyx_shutdown.cpp: GL teardown before window */

extern SDL_mutex *g_intrMutex;

static int put16(MusashiBootMemory *memory, uint32_t address, uint16_t value) {
    return musashi_boot_write16(memory, address, value);
}

static int put32(MusashiBootMemory *memory, uint32_t address, uint32_t value) {
    return musashi_boot_write32(memory, address, value);
}

int main(void) {
    MusashiBootMemory memory, snapshot;
    MusashiPsyCrossIrqRuntime runtime = {0};
    uint32_t tick = 0;
    int observed = 0;
    int attempt;
    const int continuation = 0;
    int queued = 0;
    MusashiIrqSourceBatch batch;
    uint64_t install_cut;

    SetVideoMode(MODE_NTSC);
    PsyX_Initialise("musashi_irq_scheduler_live", 320, 240, 0);
    if (SDL_GL_GetCurrentWindow() == NULL || SDL_GL_GetCurrentContext() == NULL) {
        fputs("irq_scheduler_live: PsyCross initialisation failed, skipping\n", stderr);
        return 77;
    }
    memset(&memory, 0, sizeof(memory));
    assert(musashi_psycross_irq_runtime_init(&runtime, &memory, NULL));
    fixture_irq_cpu_attach(&runtime.scheduler);
    assert(put16(&memory, 0x8006bafcu, 1u));
    assert(put16(&memory, 0x8006bb2cu, 1u));
    assert(put32(&memory, 0x8006cb88u, 0x1f801070u));
    assert(put32(&memory, 0x8006cb8cu, 0x1f801074u));
    assert(put32(&memory, 0x8006bb00u, 0x80042ce8u));
    assert(musashi_irq_controller_write16(&runtime.controller, 0x1f801074u, 1u));
    if (!musashi_psycross_irq_scheduler_hook_entry_int(&runtime.scheduler, 0x8006bb34u)) {
        fputs("irq_scheduler_live: scheduler installation refused\n", stderr);
        assert(musashi_psycross_irq_runtime_close(&runtime));
        musashi_psyx_shutdown();
        return 1;
    }
    assert(GetVideoMode() == MODE_NTSC);
    install_cut = runtime.scheduler.source_cut_tick;
    snapshot = memory;
    for (attempt = 0; attempt < 100; attempt++) {
        SDL_Delay(10);
        /* PsyCross invokes the callback while holding this mutex; taking it
         * here gives the observation the same lifetime boundary. */
        if (SDL_LockMutex(g_intrMutex) != 0)
            break;
        queued = runtime.scheduler.pending_vblanks > 0;
        assert(runtime.scheduler.deliveries == 0 && runtime.controller.status == 0);
        assert(memcmp(&snapshot, &memory, sizeof(memory)) == 0);
        SDL_UnlockMutex(g_intrMutex);
        if (queued) break;
    }
    if (queued) {
        uint32_t i;
        assert(musashi_psycross_irq_scheduler_collect_source(&runtime.scheduler, &batch));
        assert(batch.count > 0 && batch.frequency == SDL_GetPerformanceFrequency());
        assert(batch.cut_tick >= install_cut);
        for (i = 0; i < batch.count; ++i) {
            assert(batch.edges[i].sequence == (uint64_t)i + 1);
            assert(batch.edges[i].tick >= install_cut && batch.edges[i].tick <= batch.cut_tick);
            if (i) assert(batch.edges[i].tick >= batch.edges[i-1].tick);
        }
        /* Even dispatch cannot turn collected metadata into a hardware edge.
         * This fixture's owner explicitly latches its real collected batch. */
        assert(musashi_psycross_irq_scheduler_dispatch_pending(&runtime.scheduler, &continuation));
        assert(runtime.scheduler.deliveries == 0 && runtime.controller.status == 0);
        assert(memcmp(&snapshot, &memory, sizeof(memory)) == 0);
        assert(runtime.scheduler.raise_vblank(runtime.scheduler.raise_userdata));
        assert(musashi_psycross_irq_scheduler_set_enabled(&runtime.scheduler, 1));
        assert(musashi_psycross_irq_scheduler_dispatch_pending(&runtime.scheduler, &continuation));
        printf("LIVE SOURCE BATCH count=%u frequency=%llu first=%llu last=%llu cut=%llu\n",
            batch.count, (unsigned long long)batch.frequency,
            (unsigned long long)batch.edges[0].tick,
            (unsigned long long)batch.edges[batch.count-1].tick,
            (unsigned long long)batch.cut_tick);
        observed = runtime.scheduler.deliveries == 1 &&
                   runtime.scheduler.source_vblanks > 0 &&
                   runtime.scheduler.owner_thread == (uint64_t)SDL_ThreadID() &&
                   !runtime.scheduler.active_continuation && !runtime.scheduler.pumping &&
                   runtime.controller.status == 0 && runtime.scheduler.last_result ==
                   MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED &&
                   musashi_boot_read32(&memory, 0x8006cbb8u, &tick) && tick == 1;
    }
    if (!musashi_psycross_irq_scheduler_uninstall(&runtime.scheduler)) {
        fputs("irq_scheduler_live: scheduler removal refused\n", stderr);
        musashi_psyx_shutdown();
        return 1;
    }
    assert(musashi_psycross_irq_runtime_close(&runtime));
    musashi_psyx_shutdown();
    if (!observed) {
        fputs("irq_scheduler_live: no PsyCross VBlank delivery observed\n", stderr);
        return 1;
    }
    puts("LIVE IRQ OBSERVED: PsyCross VBlank metadata -> OWNER collect/latch/dispatch -> 800427F4 -> 80042CE8 -> B0:17");
    return 0;
}
