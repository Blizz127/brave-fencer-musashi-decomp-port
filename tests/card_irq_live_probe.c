#include "irq_cpu_fixture.h"
/* Real PsyCross VBlank source with real native BIOS kernel/card/policy/SIO
 * owners. RAM and IRQ devices are synthetic test fixtures; this is not native
 * startup or menu proof. The source thread only queues an edge. The owner
 * thread pumps kernel -> live pending -> custom-policy ordering. */
#include "musashi_bios_card.h"
#include "musashi_bios_kernel.h"
#include "musashi_boot_memory.h"
#include "musashi_irq_policy.h"
#include "musashi_irq_scheduler.h"
#include "musashi_sio_controller.h"

#include <SDL.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "PsyX/PsyX_public.h"
void musashi_psyx_shutdown(void); /* pc_port/psyx_shutdown.cpp: GL teardown before window */

extern SDL_mutex *g_intrMutex;

static int put16(MusashiBootMemory *memory, uint32_t address, uint16_t value) {
    return musashi_boot_write16(memory, address, value);
}

static int put32(MusashiBootMemory *memory, uint32_t address, uint32_t value) {
    return musashi_boot_write32(memory, address, value);
}

typedef struct CardOwnerFixture {
    MusashiBiosCard *card;
    MusashiBiosKernel *kernel;
    MusashiIrqPolicy *policy;
    MusashiSioController *sio;
    MusashiIrqController *irq;
    MusashiPsyCrossIrqScheduler *scheduler;
    unsigned sio_writes;
    unsigned irq_reads;
    unsigned irq_writes;
    unsigned critical_enters;
    unsigned critical_exits;
    unsigned maintenance_calls;
} CardOwnerFixture;

static int write_sio16(void *userdata, uint32_t address, uint16_t value) {
    CardOwnerFixture *fixture = userdata;
    fixture->sio_writes++;
    return musashi_sio_controller_write16(fixture->sio, address, value);
}

static int read_irq32(void *userdata, uint32_t address, uint32_t *value) {
    CardOwnerFixture *fixture = userdata;
    uint16_t word;
    fixture->irq_reads++;
    if (!value || !musashi_irq_controller_read16(fixture->irq, address, &word))
        return 0;
    *value = word;
    return 1;
}

static int write_irq32(void *userdata, uint32_t address, uint32_t value) {
    CardOwnerFixture *fixture = userdata;
    fixture->irq_writes++;
    return musashi_irq_controller_write16(fixture->irq, address,
                                           (uint16_t)value);
}

static int enter_critical(void *userdata) {
    CardOwnerFixture *fixture = userdata;
    fixture->critical_enters++;
    return musashi_bios_kernel_before_exception(fixture->kernel) &&
           musashi_psycross_irq_scheduler_set_enabled(fixture->scheduler, 0);
}

static int exit_critical(void *userdata) {
    CardOwnerFixture *fixture = userdata;
    fixture->critical_exits++;
    return musashi_bios_kernel_before_exception(fixture->kernel) &&
           musashi_psycross_irq_scheduler_set_enabled(fixture->scheduler, 1);
}

static int read_pad(void *userdata, int32_t *value) {
    CardOwnerFixture *fixture = userdata;
    if (!value) return 0;
    if (!fixture->policy->pad_initialized) return 0;
    *value = fixture->policy->pad;
    return 1;
}

static int exchange_pad(void *userdata, int32_t value, int32_t *old_value) {
    CardOwnerFixture *fixture = userdata;
    return musashi_irq_policy_exchange_pad(fixture->policy, value, old_value);
}

static int change_timer(void *userdata, int32_t channel, int32_t value) {
    CardOwnerFixture *fixture = userdata;
    return musashi_irq_policy_change_timer(fixture->policy, channel, value);
}

static int before_dispatch(void *userdata, uint16_t pending) {
    CardOwnerFixture *fixture = userdata;
    uint16_t status, mask;
    assert(pending == (pending & 1u));
    if (pending) assert(fixture_irq_kernel_entry(fixture->scheduler, pending));
    assert(musashi_bios_kernel_dispatch(fixture->kernel, pending));
    assert(musashi_irq_controller_read16(fixture->irq, 0x1f801070u, &status));
    assert(musashi_irq_controller_read16(fixture->irq, 0x1f801074u, &mask));
    assert(musashi_irq_controller_pending(fixture->irq) == (uint16_t)(status & mask));
    if (!(musashi_irq_controller_pending(fixture->irq) & 1u)) return 1;
    return musashi_irq_policy_custom_vblank(fixture->policy);
}

static int wait_for_source_edge(MusashiPsyCrossIrqScheduler *scheduler) {
    int attempt;
    for (attempt = 0; attempt < 100; ++attempt) {
        SDL_Delay(10);
        if (SDL_LockMutex(g_intrMutex) != 0) return 0;
        if (scheduler->pending_vblanks != 0) {
            SDL_UnlockMutex(g_intrMutex);
            return 1;
        }
        SDL_UnlockMutex(g_intrMutex);
    }
    return 0;
}

int main(void) {
    MusashiBootMemory memory;
    MusashiPsyCrossIrqRuntime runtime = {0};
    MusashiBiosKernel kernel;
    MusashiBiosCard card = {0};
    MusashiIrqPolicy policy;
    MusashiSioController sio;
    CardOwnerFixture fixture;
    MusashiBiosCardStartDevice start_device;
    int32_t old_initialized = -1;
    int32_t guest_result = -1;
    int32_t ready_handle = -1;
    int32_t event_test_result = -1;
    uint32_t tick = 0;
    const int continuation = 0;
    unsigned deliveries_after_first;

    PsyX_Initialise("musashi_card_irq_live", 320, 240, 0);
    if (SDL_GL_GetCurrentWindow() == NULL || SDL_GL_GetCurrentContext() == NULL) {
        fputs("card_irq_live: PsyCross initialisation failed, skipping\n", stderr);
        return 77;
    }
    memset(&memory, 0, sizeof(memory));
    assert(put16(&memory, 0x8006bafcu, 1u));
    assert(put16(&memory, 0x8006bb2cu, 1u));
    assert(put32(&memory, 0x8006cb88u, 0x1f801070u));
    assert(put32(&memory, 0x8006cb8cu, 0x1f801074u));
    assert(put32(&memory, 0x8006bb00u, 0x80042ce8u));
    musashi_bios_kernel_init(&kernel);
    musashi_irq_policy_init(&policy);
    musashi_sio_controller_init(&sio);
    assert(musashi_psycross_irq_runtime_init(&runtime, &memory, NULL));
    fixture_irq_cpu_attach(&runtime.scheduler);
    assert(musashi_bios_card_init(&card, &kernel));
    assert(musashi_bios_card_init_service(&card, 0, &old_initialized));
    assert(old_initialized == 0);
    assert(musashi_irq_policy_change_pad(&policy, 0));
    assert(musashi_irq_policy_exchange_pad(&policy, 1, &old_initialized));
    assert(old_initialized == 0);
    assert(musashi_bios_events_open(&kernel.events, 0xf0000011u, 4u,
                                    0x2000u, 0, &ready_handle));
    assert(musashi_bios_events_enable(&kernel.events, ready_handle,
                                      &old_initialized));
    assert(musashi_bios_events_deliver(&kernel.events, 0xf0000011u, 4u,
                                       NULL, NULL));
    assert(musashi_bios_events_test(&kernel.events, ready_handle,
                                    &event_test_result));
    assert(event_test_result == 1);
    assert(musashi_bios_events_deliver(&kernel.events, 0xf0000011u, 4u,
                                       NULL, NULL));

    fixture = (CardOwnerFixture){&card, &kernel, &policy, &sio,
                                 &runtime.controller, &runtime.scheduler,
                                 0, 0, 0, 0, 0, 0};
    start_device = (MusashiBiosCardStartDevice){
        .userdata = &fixture,
        .write_sio16 = write_sio16,
        .read_irq32 = read_irq32,
        .write_irq32 = write_irq32,
        .enter_critical = enter_critical,
        .exit_critical = exit_critical,
        .read_pad = read_pad,
        .exchange_pad = exchange_pad,
        .change_timer = change_timer
    };
    assert(musashi_bios_card_bind_start_device(&card, &start_device));
    assert(musashi_psycross_irq_scheduler_hook_entry_int(&runtime.scheduler,
                                                          0x8006bb34u));
    assert(musashi_psycross_irq_scheduler_set_enabled(&runtime.scheduler, 1));
    assert(musashi_bios_card_start_service(&card, &guest_result));
    assert(guest_result == 1);
    assert(card.card_started == 1 && card.serial_reset_word == 0);
    assert(card.sio_node.owner == &kernel);

    runtime.scheduler.before_dispatch = before_dispatch;
    runtime.scheduler.before_dispatch_userdata = &fixture;
    assert(wait_for_source_edge(&runtime.scheduler));
    assert(musashi_psycross_irq_scheduler_pump(&runtime.scheduler, &continuation));
    assert(runtime.scheduler.deliveries == 0);
    assert(runtime.scheduler.source_vblanks > 0);
    assert(runtime.controller.status == 0);
    assert(card.vblank_calls == 1 && card.maintenance_calls == 1);
    assert(musashi_bios_events_test(&kernel.events, ready_handle,
                                    &event_test_result));
    assert(event_test_result == 0);
    deliveries_after_first = runtime.scheduler.deliveries;

    /* A stale SIO verifier with I_STAT clear is a no-op and cannot invent a
     * game callback. Do not add a second scheduler pump: a fresh source edge
     * may already be queued asynchronously. */
    assert(before_dispatch(&fixture, 0));
    assert(card.vblank_calls == 1 && card.maintenance_calls == 1);
    assert(runtime.scheduler.deliveries == deliveries_after_first);

    /* A fresh real source edge with pad policy zero is allowed to reach the
     * recovered game callback once. The source remains PsyCross-owned. */
    assert(musashi_irq_policy_exchange_pad(&policy, 0, &old_initialized));
    assert(old_initialized == 1);
    assert(wait_for_source_edge(&runtime.scheduler));
    assert(musashi_psycross_irq_scheduler_pump(&runtime.scheduler, &continuation));
    assert(runtime.scheduler.deliveries == 1);
    assert(musashi_boot_read32(&memory, 0x8006cbb8u, &tick) && tick == 1);
    assert(card.vblank_calls == 2 && card.maintenance_calls == 2);
    assert(runtime.controller.status == 0);
    assert(runtime.scheduler.faulted == 0);
    assert(fixture.sio_writes != 0 && fixture.irq_reads != 0 &&
           fixture.irq_writes != 0 && fixture.critical_enters != 0 &&
           fixture.critical_exits != 0);

    assert(musashi_psycross_irq_scheduler_uninstall(&runtime.scheduler));
    assert(musashi_psycross_irq_runtime_close(&runtime));
    assert(musashi_bios_card_shutdown(&card));
    musashi_psyx_shutdown();
    puts("LIVE CARD IRQ OBSERVED: PsyCross edge -> card policy/maintenance -> owner pump -> game callback");
    return 0;
}
