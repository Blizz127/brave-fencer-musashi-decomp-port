#include "irq_cpu_fixture.h"
/* Test-owned PsyCross VBlank source. The production build resolves this symbol
 * to PsyCross, whose event loop invokes the callback. */
#include "musashi_irq_scheduler.h"

#include <SDL.h>
#include <assert.h>
#include <string.h>

void (*vsync_callback)(void);
SDL_mutex *g_intrMutex;
int VSyncCallback(void (*callback)(void)) {
    vsync_callback = callback;
    return 0;
}

static MusashiPsyCrossIrqScheduler *executing_scheduler;
static const int continuation;
static int collect_during_execute;
static MusashiIrqSourceBatch callback_batch;
static void queue_edge(void) {
    assert(SDL_LockMutex(g_intrMutex) == 0);
    vsync_callback();
    SDL_UnlockMutex(g_intrMutex);
}

typedef struct Hardware {
    uint16_t status;
    uint16_t mask;
    unsigned raised;
    unsigned calls;
} Hardware;

static int read16(void *userdata, uint32_t address, uint16_t *value) {
    Hardware *hardware = userdata;
    if (address == 0x1f801070u) *value = hardware->status;
    else if (address == 0x1f801074u) *value = hardware->mask;
    else return 0;
    return 1;
}

static int write16(void *userdata, uint32_t address, uint16_t value) {
    Hardware *hardware = userdata;
    if (address != 0x1f801070u) return 0;
    hardware->status &= value;
    return 1;
}

static int execute(void *userdata, MusashiBootMemory *memory, uint32_t target) {
    Hardware *hardware = userdata;
    if (target != 0x80042ce8u) return 0;
    if (executing_scheduler) {
        assert(executing_scheduler->active_continuation == &continuation);
        assert(executing_scheduler->delivering && !executing_scheduler->return_consumed);
        assert(!musashi_psycross_irq_scheduler_pump(executing_scheduler, &continuation));
        assert(!musashi_psycross_irq_scheduler_uninstall(executing_scheduler));
    }
    if (collect_during_execute) {
        uint16_t old_status = hardware->status;
        uint64_t old_sources = executing_scheduler->source_vblanks;
        if (collect_during_execute == 2) {
            /* Synthetic clock regression during an actual owner dispatch. */
            executing_scheduler->source_tick = UINT64_MAX;
            queue_edge();
            assert(!musashi_psycross_irq_scheduler_collect_source(executing_scheduler, &callback_batch));
            assert(executing_scheduler->faulted);
        } else {
            queue_edge();
            assert(musashi_psycross_irq_scheduler_collect_source(executing_scheduler, &callback_batch));
            assert(callback_batch.count == 1 && callback_batch.edges[0].sequence == old_sources + 1);
        }
        assert(hardware->status == old_status && executing_scheduler->delivering);
        assert(!musashi_psycross_irq_scheduler_dispatch_pending(executing_scheduler, &continuation));
        collect_during_execute = 0;
    }
    hardware->calls++;
    return musashi_boot_call_80042ce8(memory, NULL, NULL);
}

static int raise_vblank(void *userdata) {
    Hardware *hardware = userdata;
    hardware->raised++;
    hardware->status |= 1u;
    return 1;
}

static int refuse_vblank(void *userdata) {
    Hardware *hardware = userdata;
    hardware->raised++;
    return 0;
}

static void preexisting_callback(void) {
}

static void put16(MusashiBootMemory *memory, uint32_t address, uint16_t value) {
    assert(musashi_boot_write16(memory, address, value));
}

static void put32(MusashiBootMemory *memory, uint32_t address, uint32_t value) {
    assert(musashi_boot_write32(memory, address, value));
}

static void runtime_test(MusashiBootMemory *memory) {
    MusashiPsyCrossIrqRuntime runtime = {0};
    uint32_t tick, index;
    uint16_t active;
    memset(memory, 0, sizeof(*memory));
    assert(!musashi_psycross_irq_runtime_init(NULL, memory, NULL));
    assert(!musashi_psycross_irq_runtime_init(&runtime, NULL, NULL));
    assert(musashi_psycross_irq_runtime_init(&runtime, memory, NULL));
    fixture_irq_cpu_attach(&runtime.scheduler);
    put16(memory, 0x8006bafcu, 1u);
    put16(memory, 0x8006bb2cu, 1u);
    put32(memory, 0x8006cb88u, 0x1f801070u);
    put32(memory, 0x8006cb8cu, 0x1f801074u);
    put32(memory, 0x8006bb00u, 0x80042ce8u);
    put32(memory, 0x8006cbb4u, 0x800616d0u);
    put32(memory, 0x80072a34u, UINT32_MAX); /* synthetic idle fixture */
    assert(musashi_irq_controller_write16(&runtime.controller, 0x1f801074u, 1u));
    assert(musashi_psycross_irq_scheduler_hook_entry_int(
        &runtime.scheduler, 0x8006bb34u));
    assert(!musashi_psycross_irq_runtime_init(&runtime, memory, NULL));
    assert(musashi_psycross_irq_scheduler_set_enabled(&runtime.scheduler, 1));
    queue_edge();
    assert(runtime.scheduler.deliveries == 0 && runtime.controller.status == 0);
    assert(musashi_psycross_irq_scheduler_pump(&runtime.scheduler, &continuation));
    assert(runtime.scheduler.last_result == MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    assert(runtime.controller.status == 0 && runtime.failed_target == 0);
    assert(musashi_boot_read32(memory, 0x8006cbb8u, &tick) && tick == 1);

    /* Nonempty request uses its actual guest target and remains incomplete.
     * A second platform frame must not decrement the index or clear the fault. */
    put32(memory, 0x80072a34u, 0);
    put32(memory, 0x80078d78u, 0x80012340u);
    queue_edge();
    assert(!musashi_psycross_irq_scheduler_pump(&runtime.scheduler, &continuation));
    assert(runtime.scheduler.faulted && runtime.failed_target == 0x80012340u);
    assert(runtime.scheduler.last_result == 0);
    assert(musashi_boot_read32(memory, 0x8006cbb8u, &tick) && tick == 2);
    assert(musashi_boot_read32(memory, 0x80072a34u, &index) && index == 0);
    assert(musashi_boot_read16(memory, 0x8006bafeu, &active) && active == 1);
    queue_edge();
    assert(!musashi_psycross_irq_scheduler_pump(&runtime.scheduler, &continuation));
    assert(runtime.scheduler.active_continuation == &continuation && !runtime.scheduler.return_consumed);
    assert(musashi_boot_read32(memory, 0x8006cbb8u, &tick) && tick == 2);
    assert(runtime.scheduler.deliveries == 2 && runtime.scheduler.last_result == 0);
    assert(musashi_psycross_irq_scheduler_uninstall(&runtime.scheduler));
    assert(!musashi_psycross_irq_scheduler_hook_entry_int(
        &runtime.scheduler, 0x8006bb34u));

    /* Both levels of unknown slot targets refuse before calling game code. */
    assert(!runtime.device.execute(runtime.device.userdata, memory, 0x80012344u));
    assert(runtime.failed_target == 0x80012344u);
    put32(memory, 0x8006cbb4u, 0x80012348u);
    assert(!runtime.device.execute(runtime.device.userdata, memory, 0x80042ce8u));
    assert(runtime.failed_target == 0x80012348u);
    assert(musashi_psycross_irq_runtime_close(&runtime));
    assert(!musashi_psycross_irq_runtime_init(&runtime, memory, NULL));
    assert(!musashi_psycross_irq_runtime_close(&runtime));
}

typedef struct DmaDicr {
    uint32_t value;
} DmaDicr;

static int dma_dicr_read32(void *userdata, uint32_t address, uint32_t *value) {
    DmaDicr *dicr = userdata;
    if (address != 0x1f8010f4u) return 0;
    *value = dicr->value;
    return 1;
}

static int dma_dicr_write32(void *userdata, uint32_t address, uint32_t value) {
    DmaDicr *dicr = userdata;
    uint32_t next;
    if (address != 0x1f8010f4u) return 0;
    next = (value & 0x00ff807fu) | (dicr->value & 0x7f000000u & ~value);
    if ((next & 0x8000u) || ((next & 0x800000u) && (next & 0x7f000000u)))
        next |= 0x80000000u;
    dicr->value = next;
    return 1;
}

static void dma_irq_test(MusashiBootMemory *memory) {
    MusashiPsyCrossIrqRuntime runtime = {0};
    MusashiDmaCallbackRegistrationDevice dma = {0};
    DmaDicr dicr = {0x00900000u};
    memset(memory, 0, sizeof(*memory));
    assert(musashi_psycross_irq_runtime_init(&runtime, memory, NULL));
    fixture_irq_cpu_attach(&runtime.scheduler);
    dma.userdata = &dicr;
    dma.read32 = dma_dicr_read32;
    dma.write32 = dma_dicr_write32;
    assert(musashi_psycross_irq_runtime_bind_dma(&runtime, &dma));
    assert(!musashi_psycross_irq_runtime_bind_dma(&runtime, &dma));
    put16(memory, 0x8006bafcu, 1u);
    put16(memory, 0x8006bb2cu, 8u);
    put32(memory, 0x8006cb88u, 0x1f801070u);
    put32(memory, 0x8006cb8cu, 0x1f801074u);
    put32(memory, 0x8006bb0cu, 0x80042e08u);
    put32(memory, 0x8006cbc0u, 0x1f8010f4u);
    assert(musashi_irq_controller_write16(&runtime.controller, 0x1f801074u, 8u));
    runtime.controller.status = 8u;
    assert(musashi_psycross_irq_scheduler_hook_entry_int(
        &runtime.scheduler, 0x8006bb34u));
    assert(musashi_psycross_irq_scheduler_set_enabled(&runtime.scheduler, 1));
    assert(musashi_psycross_irq_scheduler_dispatch_pending(
        &runtime.scheduler, &continuation));
    assert(runtime.scheduler.last_result == MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    assert(runtime.controller.status == 0 && runtime.failed_target == 0);
    assert(!runtime.scheduler.faulted && runtime.scheduler.deliveries == 1);
    assert(dicr.value == 0x00900000u);
    assert(musashi_psycross_irq_scheduler_uninstall(&runtime.scheduler));
    assert(musashi_psycross_irq_runtime_close(&runtime));
}

static int stub_source_execute(void *userdata, MusashiBootMemory *memory, uint32_t target) {
    (void)userdata;
    (void)memory;
    return target == 0x8002cdd8u;
}

static void vblank_root_test(MusashiBootMemory *memory) {
    MusashiPsyCrossIrqRuntime runtime = {0};
    uint32_t tick;
    memset(memory, 0, sizeof(*memory));
    assert(musashi_psycross_irq_runtime_init(&runtime, memory, NULL));
    fixture_irq_cpu_attach(&runtime.scheduler);
    assert(musashi_psycross_irq_runtime_bind_source_execute(
        &runtime, stub_source_execute, NULL));
    put16(memory, 0x8006bafcu, 1u);
    put16(memory, 0x8006bb2cu, 1u);
    put32(memory, 0x8006cb88u, 0x1f801070u);
    put32(memory, 0x8006cb8cu, 0x1f801074u);
    put32(memory, 0x8006bb00u, 0x80042ce8u);
    put32(memory, 0x8006cbb4u, 0x800616d0u);
    put32(memory, 0x8006cba8u, 0x8001096cu);
    put32(memory, 0x80072a34u, UINT32_MAX);
    assert(musashi_irq_controller_write16(&runtime.controller, 0x1f801074u, 1u));
    assert(musashi_psycross_irq_scheduler_hook_entry_int(
        &runtime.scheduler, 0x8006bb34u));
    assert(musashi_psycross_irq_scheduler_set_enabled(&runtime.scheduler, 1));
    queue_edge();
    assert(musashi_psycross_irq_scheduler_pump(&runtime.scheduler, &continuation));
    assert(runtime.scheduler.last_result == MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    assert(!runtime.scheduler.faulted && runtime.failed_target == 0);
    assert(musashi_boot_read32(memory, 0x8006cbb8u, &tick) && tick == 1);
    assert(musashi_psycross_irq_scheduler_uninstall(&runtime.scheduler));
    assert(musashi_psycross_irq_runtime_close(&runtime));
}

static int foreign_thread(void *userdata) {
    MusashiPsyCrossIrqScheduler *scheduler = userdata;
    MusashiIrqSourceBatch batch, unchanged;
    memset(&batch, 0xa5, sizeof(batch)); unchanged = batch;
    assert(!musashi_psycross_irq_scheduler_collect_source(scheduler, &batch));
    assert(!memcmp(&batch, &unchanged, sizeof(batch)));
    assert(!musashi_psycross_irq_scheduler_dispatch_pending(scheduler, &continuation));
    assert(!musashi_psycross_irq_scheduler_pump(scheduler, &continuation));
    assert(!musashi_psycross_irq_scheduler_set_enabled(scheduler, 1));
    assert(!musashi_psycross_irq_scheduler_uninstall(scheduler));
    assert(!musashi_psycross_irq_scheduler_hook_entry_int(scheduler, 0x8006bb34u));
    queue_edge(); /* Source work is permitted from the worker. */
    return 0;
}

static int before_dispatch(void *userdata, uint16_t pending) {
    Hardware *hardware = userdata;
    assert(pending == 1);
    assert(!musashi_psycross_irq_scheduler_pump(executing_scheduler, &continuation));
    assert(!musashi_psycross_irq_scheduler_uninstall(executing_scheduler));
    if (hardware->mask == 1) return 0;
    assert(fixture_irq_kernel_entry(executing_scheduler, pending));
    hardware->status = 0;
    return 1;
}

/* Synthetic source fixture; collection publishes metadata only. */
static void metadata_test(MusashiBootMemory *memory) {
    Hardware hardware = {0};
    MusashiIrqDispatchDevice device = {&hardware, read16, write16, execute, NULL, NULL};
    MusashiPsyCrossIrqScheduler scheduler = {0};
    MusashiIrqSourceBatch batch, before;
    uint64_t first_cut;
    memset(memory, 0, sizeof(*memory));
    put16(memory, 0x8006bafcu, 1); put16(memory, 0x8006bb2cu, 1);
    put32(memory, 0x8006cb88u, 0x1f801070u); put32(memory, 0x8006cb8cu, 0x1f801074u);
    put32(memory, 0x8006bb00u, 0x80042ce8u);
    hardware.mask = 1;
    scheduler.memory = memory; scheduler.device = &device;
    scheduler.raise_vblank = raise_vblank; scheduler.raise_userdata = &hardware;
    fixture_irq_cpu_attach(&scheduler);
    executing_scheduler = &scheduler;
    memset(&batch, 0xa5, sizeof(batch)); before = batch;
    assert(!musashi_psycross_irq_scheduler_collect_source(&scheduler, &batch));
    assert(!memcmp(&batch, &before, sizeof(batch)));
    assert(musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34u));
    assert(musashi_psycross_irq_scheduler_collect_source(&scheduler, &batch));
    assert(batch.count == 0 && batch.frequency == SDL_GetPerformanceFrequency());
    first_cut = batch.cut_tick;
    queue_edge(); queue_edge();
    assert(musashi_psycross_irq_scheduler_collect_source(&scheduler, &batch));
    assert(batch.count == 2 && batch.cut_tick >= first_cut);
    assert(batch.edges[0].sequence == 1 && batch.edges[1].sequence == 2);
    assert(batch.edges[0].tick >= first_cut && batch.edges[0].tick <= batch.edges[1].tick);
    assert(batch.edges[1].tick <= batch.cut_tick && !scheduler.pending_vblanks);
    assert(!hardware.status && !hardware.calls && !hardware.raised && scheduler.source_vblanks == 2);
    queue_edge();
    assert(musashi_psycross_irq_scheduler_dispatch_pending(&scheduler, &continuation));
    assert(scheduler.pending_vblanks == 1 && !hardware.status && !hardware.raised);
    assert(musashi_psycross_irq_scheduler_set_enabled(&scheduler, 1));
    collect_during_execute = 1;
    assert(musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(!collect_during_execute && callback_batch.count == 1 && scheduler.source_vblanks == 4);
    assert(hardware.calls == 1 && hardware.raised == 1 && !hardware.status);
    assert(!scheduler.active_continuation && !scheduler.return_consumed);
    scheduler.collecting = 1;
    before = batch;
    assert(!musashi_psycross_irq_scheduler_collect_source(&scheduler, &batch));
    assert(!memcmp(&batch, &before, sizeof(batch)) && !scheduler.faulted);
    scheduler.collecting = 0;
    collect_during_execute = 2;
    queue_edge();
    assert(!musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(scheduler.faulted && scheduler.active_continuation == &continuation && !scheduler.return_consumed);
    assert(hardware.calls == 2 && hardware.raised == 2 && !hardware.status);
    assert(scheduler.deliveries == 2 && scheduler.last_result == 0);
    assert(musashi_psycross_irq_scheduler_uninstall(&scheduler));
    fixture_irq_cpu_close(&scheduler);
    assert(!musashi_psycross_irq_scheduler_collect_source(&scheduler, &batch));
    assert(!memcmp(&batch, &before, sizeof(batch)));
    executing_scheduler = NULL;
}

static void metadata_fault_test(MusashiBootMemory *memory) {
    unsigned kind, i;
    for (kind = 0; kind < 8; ++kind) {
        Hardware hardware = {0};
        MusashiIrqDispatchDevice device = {&hardware, read16, write16, execute, NULL, NULL};
        MusashiPsyCrossIrqScheduler scheduler = {0};
        MusashiIrqSourceBatch batch, before;
        uint32_t queued;
        scheduler.memory = memory; scheduler.device = &device;
        scheduler.raise_vblank = raise_vblank; scheduler.raise_userdata = &hardware;
    fixture_irq_cpu_attach(&scheduler);
        assert(musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34u));
        memset(&batch, 0x5a, sizeof(batch)); before = batch;
        if (kind == 0) {
            for (i = 0; i < MUSASHI_IRQ_SOURCE_CAPACITY; ++i) queue_edge();
            assert(scheduler.pending_vblanks == MUSASHI_IRQ_SOURCE_CAPACITY);
            queue_edge();
        } else if (kind == 1) {
            scheduler.source_tick = UINT64_MAX; queue_edge();
        } else if (kind == 2) {
            scheduler.source_sequence = UINT64_MAX; queue_edge();
        } else if (kind == 3) {
            queue_edge(); ++scheduler.source_edges[0].sequence;
        } else if (kind == 4) {
            scheduler.source_cut_tick = UINT64_MAX;
        } else if (kind == 5) {
            ++scheduler.source_frequency;
        } else if (kind == 6) {
            queue_edge(); scheduler.source_edges[0].tick = UINT64_MAX;
        } else {
            queue_edge(); queue_edge();
            assert(scheduler.source_edges[0].tick > 0);
            scheduler.source_edges[1].tick = scheduler.source_edges[0].tick - 1;
        }
        queued = scheduler.pending_vblanks;
        assert(!musashi_psycross_irq_scheduler_collect_source(&scheduler, &batch));
        assert(!memcmp(&batch, &before, sizeof(batch)));
        assert(scheduler.faulted && scheduler.source_overflow && !scheduler.collecting);
        assert(scheduler.pending_vblanks == queued && !scheduler.source_vblanks);
        assert(!hardware.raised && !hardware.calls && !hardware.status);
        assert(!musashi_psycross_irq_scheduler_dispatch_pending(&scheduler, &continuation));
        assert(!musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
        assert(musashi_psycross_irq_scheduler_uninstall(&scheduler));
    fixture_irq_cpu_close(&scheduler);
        assert(!musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34u));
    }
}

/* New CPU integration controls exercise exact entry/return boundaries. */
static int cpu_case;
static Hardware *cpu_hardware;
static int boundary_context(void *userdata, const void *identity, MusashiCpuContext *out) {
    (void)userdata;
    assert(fixture_irq_context(NULL, identity, out));
    if (cpu_case == 1) return 0;
    if (cpu_case == 2) out->identity = NULL;
    if (cpu_case == 3 || cpu_case == 8) {
        out->provenance = MUSASHI_CPU_CONTEXT_SOURCE;
        out->pc = 0x80012344; out->npc = out->pc + 4;
        out->instruction_valid = cpu_case == 8;
        out->delay_slot = 1; out->branch_pc = out->pc - 4;
    }
    return 1;
}
static int boundary_kernel(void *userdata, uint16_t pending) {
    MusashiPsyCrossIrqScheduler *s = userdata;
    MusashiCpuStatusSnapshot state;
    assert(pending == 1 && s->delivering && s->pumping);
    assert(s->active_continuation == &continuation);
    assert(musashi_cpu_status_inspect(s->cpu_status, &s->active_exception, &state));
    assert(state.sr == 0x40000404 && state.active);
    if (cpu_case == 5) return 1; /* Cannot replace kernel entry with acceptance. */
    assert(musashi_cpu_status_accept_kernel_entry(s->cpu_status, &s->active_exception));
    if (cpu_case == 6) return 0; /* Accepted entry is retained on refusal. */
    if (cpu_case == 8) {
        assert(state.cause_epc_valid && state.cause == 0x80000400);
        assert(state.epc == 0x80012340 && state.delay_slot);
    }
    cpu_hardware->status = 0; /* Explicit synthetic BIOS-only acknowledgement. */
    return 1;
}
static void cpu_boundary_test(MusashiBootMemory *memory) {
    for (cpu_case = 0; cpu_case < 9; ++cpu_case) {
        Hardware hardware = {1, 1, 0, 0};
        MusashiIrqDispatchDevice device = {&hardware, read16, write16, execute, NULL, NULL};
        MusashiPsyCrossIrqScheduler scheduler = {0};
        MusashiCpuStatusSnapshot state;
        int enabled = -1;
        scheduler.memory = memory; scheduler.device = &device;
        scheduler.raise_vblank = raise_vblank; scheduler.raise_userdata = &hardware;
        fixture_irq_cpu_attach(&scheduler);
        scheduler.context_provider = cpu_case == 0 ? NULL : boundary_context;
        scheduler.before_dispatch = cpu_case == 4 ? NULL : boundary_kernel;
        scheduler.before_dispatch_userdata = &scheduler;
        assert(musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34));
        assert(musashi_psycross_irq_scheduler_set_enabled(&scheduler, 1));
        if (cpu_case) assert(!musashi_psycross_irq_scheduler_bind_context(&scheduler, boundary_context, NULL));
        /* Hook reinstall preserves full SR; it does not restore initial disabled. */
        assert(musashi_psycross_irq_scheduler_uninstall(&scheduler));
        assert(musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34));
        assert(musashi_psycross_irq_scheduler_get_enabled(&scheduler, &enabled) && enabled);
        cpu_hardware = &hardware;
        if (cpu_case >= 7) {
            assert(musashi_psycross_irq_scheduler_dispatch_pending(&scheduler, &continuation));
            assert(!scheduler.active_continuation && !scheduler.active_exception.owner);
            assert(!scheduler.deliveries && !scheduler.faulted && !hardware.status);
            assert(musashi_cpu_status_snapshot(scheduler.cpu_status, &state));
            assert(state.sr == 0x40000401 && !state.active && state.sequence == 1);
        } else {
            assert(!musashi_psycross_irq_scheduler_dispatch_pending(&scheduler, &continuation));
            assert(scheduler.faulted && !scheduler.delivering && !scheduler.pumping);
            assert(hardware.status == 1 && !hardware.calls && !scheduler.deliveries);
            assert(musashi_cpu_status_snapshot(scheduler.cpu_status, &state));
            assert(state.active == (cpu_case == 5 || cpu_case == 6));
            assert(state.sr == (state.active ? 0x40000404u : 0x40000401u));
            if (state.active) assert(state.faulted && scheduler.active_exception.owner);
        }
        assert(musashi_psycross_irq_scheduler_uninstall(&scheduler));
        fixture_irq_cpu_close(&scheduler);
    }
}
int main(void) {
    MusashiBootMemory memory, snapshot;
    Hardware hardware = {0};
    MusashiIrqDispatchDevice device = {
        &hardware, read16, write16, execute, NULL, NULL
    };
    MusashiPsyCrossIrqScheduler scheduler = {0};
    SDL_Thread *thread;
    uint32_t tick;
    memset(&memory, 0, sizeof(memory));
    assert(SDL_Init(0) == 0);
    g_intrMutex = SDL_CreateMutex();
    assert(g_intrMutex != NULL);
    put16(&memory, 0x8006bafcu, 1u);
    put16(&memory, 0x8006bb2cu, 1u);
    put32(&memory, 0x8006cb88u, 0x1f801070u);
    put32(&memory, 0x8006cb8cu, 0x1f801074u);
    put32(&memory, 0x8006bb00u, 0x80042ce8u);
    hardware.mask = 1;
    scheduler.memory = &memory;
    scheduler.device = &device;
    scheduler.raise_vblank = raise_vblank;
    scheduler.raise_userdata = &hardware;
    fixture_irq_cpu_attach(&scheduler);
    executing_scheduler = &scheduler;
    assert(musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34u));
    assert(scheduler.installed && scheduler.context_address == 0x8006bb34u);
    { int enabled = -1; assert(musashi_psycross_irq_scheduler_get_enabled(&scheduler, &enabled) && !enabled); }
    snapshot = memory;
    thread = SDL_CreateThread(foreign_thread, "irq_source_test", &scheduler);
    assert(thread);
    SDL_WaitThread(thread, NULL);
    queue_edge();
    queue_edge();
    assert(!memcmp(&memory, &snapshot, sizeof(memory)));
    assert(hardware.raised == 0 && hardware.calls == 0 && hardware.status == 0);
    assert(scheduler.pending_vblanks == 3);
    assert(!musashi_psycross_irq_scheduler_pump(&scheduler, NULL));
    assert(scheduler.pending_vblanks == 3 && !scheduler.faulted);
    assert(musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(hardware.raised == 1 && hardware.status == 1 && hardware.calls == 0);
    assert(scheduler.source_vblanks == 3 && scheduler.deliveries == 0);
    assert(!memcmp(&memory, &snapshot, sizeof(memory)));
    hardware.mask = 0;
    assert(musashi_psycross_irq_scheduler_set_enabled(&scheduler, 1));
    assert(musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(hardware.status == 1 && hardware.calls == 0);
    hardware.mask = 1;
    assert(musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(hardware.raised == 1 && hardware.calls == 1 && hardware.status == 0);
    assert(scheduler.deliveries == 1 && scheduler.last_result ==
           MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED);
    assert(!scheduler.active_continuation && !scheduler.delivering && !scheduler.return_consumed);
    assert(musashi_boot_read32(&memory, 0x8006cbb8u, &tick) && tick == 1);
    assert(musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(scheduler.deliveries == 1);

    /* A BIOS owner can consume pending work before the game, or refuse it. */
    scheduler.before_dispatch = before_dispatch;
    scheduler.before_dispatch_userdata = &hardware;
    hardware.mask = 3;
    queue_edge();
    assert(musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(hardware.status == 0 && scheduler.deliveries == 1);
    hardware.mask = 1;
    queue_edge();
    assert(!musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(scheduler.faulted && hardware.status == 1 && scheduler.deliveries == 1);
    assert(musashi_psycross_irq_scheduler_uninstall(&scheduler));
    fixture_irq_cpu_close(&scheduler);
    assert(!musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34u));

    /* Refused raise and overflow retain a terminal failure without guest work. */
    for (unsigned kind = 0; kind < 3; ++kind) {
        memset(&scheduler, 0, sizeof(scheduler));
        scheduler.memory = &memory;
        scheduler.device = &device;
        scheduler.raise_userdata = &hardware;
        scheduler.raise_vblank = kind ? raise_vblank : refuse_vblank;
        fixture_irq_cpu_attach(&scheduler);
        assert(musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34u));
        if (kind == 1) scheduler.pending_vblanks = UINT32_MAX;
        if (kind == 2) scheduler.source_vblanks = UINT64_MAX;
        queue_edge();
        assert(!musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
        assert(scheduler.faulted && scheduler.deliveries == 0);
        assert(musashi_psycross_irq_scheduler_uninstall(&scheduler));
    fixture_irq_cpu_close(&scheduler);
    }
    memset(&scheduler, 0, sizeof(scheduler));
    scheduler.memory = &memory;
    scheduler.device = &device;
    scheduler.raise_userdata = &hardware;
    scheduler.raise_vblank = raise_vblank;
    fixture_irq_cpu_attach(&scheduler);
    /* The child callback's unknown guest slot faults after its tick write. */
    put32(&memory, 0x8006cb98u, 0x80012340u);
    assert(musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34u));
    assert(musashi_psycross_irq_scheduler_set_enabled(&scheduler, 1));
    queue_edge();
    assert(!musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(scheduler.faulted && scheduler.active_continuation == &continuation && !scheduler.pumping);
    assert(musashi_boot_read32(&memory, 0x8006cbb8u, &tick) && tick == 2);
    queue_edge();
    assert(!musashi_psycross_irq_scheduler_pump(&scheduler, &continuation));
    assert(musashi_boot_read32(&memory, 0x8006cbb8u, &tick) && tick == 2);
    assert(musashi_psycross_irq_scheduler_uninstall(&scheduler));
    fixture_irq_cpu_close(&scheduler);
    memset(&scheduler, 0, sizeof(scheduler));
    scheduler.memory = &memory;
    scheduler.device = &device;
    scheduler.raise_userdata = &hardware;
    scheduler.raise_vblank = raise_vblank;
    fixture_irq_cpu_attach(&scheduler);
    vsync_callback = preexisting_callback;
    assert(!musashi_psycross_irq_scheduler_hook_entry_int(&scheduler, 0x8006bb34u));
    assert(vsync_callback == preexisting_callback);
    vsync_callback = NULL;
    fixture_irq_cpu_close(&scheduler);
    executing_scheduler = NULL;
    cpu_boundary_test(&memory);
    dma_irq_test(&memory);
    vblank_root_test(&memory);
    runtime_test(&memory);
    metadata_test(&memory);
    metadata_fault_test(&memory);
    SDL_DestroyMutex(g_intrMutex);
    g_intrMutex = NULL;
    SDL_Quit();
    return 0;
}
