#include "musashi_native_lane.h"
#include "musashi_irq_scheduler.h"

#include <SDL.h>
#include <string.h>

/* PsyCross owns one global VBlank callback. Its return value contains a
 * truncated former callback on 64-bit hosts, so this bridge deliberately
 * never reads it or attempts to restore an existing registration. */
extern void (*vsync_callback)(void);
extern int VSyncCallback(void (*callback)(void));
/* PsyCross invokes vsync_callback while holding this mutex. Registering and
 * removing under the same lock makes active_scheduler lifetime explicit. */
extern SDL_mutex *g_intrMutex;

static MusashiPsyCrossIrqScheduler *active_scheduler;

static int scheduler_lock(void) {
    return g_intrMutex != NULL && SDL_LockMutex(g_intrMutex) == 0;
}

static void scheduler_unlock(void) {
    SDL_UnlockMutex(g_intrMutex);
}

static int scheduler_read16(void *userdata, uint32_t address, uint16_t *value) {
    MusashiPsyCrossIrqScheduler *scheduler = userdata;
    return scheduler->device->read16(scheduler->device->userdata, address, value);
}

static int scheduler_write16(void *userdata, uint32_t address, uint16_t value) {
    MusashiPsyCrossIrqScheduler *scheduler = userdata;
    return scheduler->device->write16(scheduler->device->userdata, address, value);
}

static int scheduler_execute(void *userdata, MusashiBootMemory *memory,
                             uint32_t target) {
    MusashiPsyCrossIrqScheduler *scheduler = userdata;
    return scheduler->device->execute(scheduler->device->userdata, memory, target);
}

static int scheduler_diagnostic(void *userdata, MusashiBootMemory *memory,
                                uint32_t format, uint32_t first,
                                uint32_t second, uint32_t count) {
    MusashiPsyCrossIrqScheduler *scheduler = userdata;
    if (!scheduler->device->diagnostic) return 0;
    return scheduler->device->diagnostic(scheduler->device->userdata, memory,
                                         format, first, second, count);
}

static int scheduler_is_owner(const MusashiPsyCrossIrqScheduler *scheduler) {
    return scheduler && scheduler->owner_thread == (uint64_t)SDL_ThreadID();
}

static int scheduler_return_from_exception(void *userdata) {
    MusashiPsyCrossIrqScheduler *scheduler = userdata;
    if (!scheduler_is_owner(scheduler) || !scheduler->delivering ||
        !scheduler->active_continuation || scheduler->return_consumed ||
        !scheduler->installed || active_scheduler != scheduler)
        return 0;
    {
        MusashiCpuStatusSnapshot state;
        if (!musashi_cpu_status_inspect(scheduler->cpu_status,
                &scheduler->active_exception, &state) || state.faulted ||
            !state.kernel_entered || state.kind != MUSASHI_CPU_EXCEPTION_IRQ)
            return 0;
    }
    scheduler->return_consumed = 1;
    return 1;
}

/* PsyCross already holds g_intrMutex. Only arrival metadata is published;
 * there is no device/RAM work and no catchup for missed source intervals. */
static void scheduler_vblank(void) {
    MusashiPsyCrossIrqScheduler *scheduler = active_scheduler;
    uint64_t tick;
    if (!scheduler || !scheduler->installed || scheduler->source_overflow)
        return;
    tick = SDL_GetPerformanceCounter();
    if (scheduler->host_edges_advisory &&
        scheduler->pending_vblanks >= MUSASHI_IRQ_SOURCE_CAPACITY) {
        if (scheduler->dropped_host_vblanks != UINT64_MAX) ++scheduler->dropped_host_vblanks;
        return;
    }
    if (scheduler->pending_vblanks >= MUSASHI_IRQ_SOURCE_CAPACITY ||
        scheduler->source_sequence == UINT64_MAX ||
        tick < scheduler->source_tick || tick < scheduler->source_cut_tick) {
        scheduler->source_overflow = 1;
        return;
    }
    scheduler->source_tick = tick;
    scheduler->source_edges[scheduler->pending_vblanks++] =
        (MusashiIrqSourceEdge){++scheduler->source_sequence, tick};
}

int musashi_psycross_irq_scheduler_collect_source(
    MusashiPsyCrossIrqScheduler *scheduler, MusashiIrqSourceBatch *batch) {
    MusashiIrqSourceBatch result = {0};
    uint64_t previous_tick, sequence;
    uint32_t i;
    if (!scheduler_is_owner(scheduler) || !batch || scheduler->collecting ||
        scheduler->faulted || !scheduler_lock()) return 0;
    if (active_scheduler != scheduler || !scheduler->installed ||
        vsync_callback != scheduler_vblank) {
        scheduler_unlock();
        return 0;
    }
    scheduler->collecting = 1;
    result.cut_tick = SDL_GetPerformanceCounter();
    result.frequency = SDL_GetPerformanceFrequency();
    result.count = scheduler->pending_vblanks;
    if (scheduler->source_overflow || result.count > MUSASHI_IRQ_SOURCE_CAPACITY ||
        !result.frequency || result.frequency != scheduler->source_frequency ||
        result.cut_tick < scheduler->source_cut_tick ||
        result.cut_tick < scheduler->source_tick ||
        scheduler->source_vblanks > UINT64_MAX - result.count)
        goto fault;
    previous_tick = scheduler->source_cut_tick;
    sequence = scheduler->source_vblanks;
    for (i = 0; i < result.count; ++i) {
        MusashiIrqSourceEdge edge = scheduler->source_edges[i];
        if (edge.sequence != ++sequence || edge.tick < previous_tick ||
            edge.tick > result.cut_tick) goto fault;
        previous_tick = edge.tick;
        result.edges[i] = edge;
    }
    if (sequence != scheduler->source_sequence) goto fault;
    scheduler->source_cut_tick = result.cut_tick;
    scheduler->source_vblanks = sequence;
    scheduler->pending_vblanks = 0;
    scheduler_unlock();
    *batch = result;
    scheduler->collecting = 0;
    return 1;
fault:
    scheduler->source_overflow = 1;
    scheduler_unlock();
    scheduler->collecting = 0;
    scheduler->faulted = 1;
    scheduler->last_result = 0;
    return 0;
}

int musashi_psycross_irq_scheduler_set_enabled(
    MusashiPsyCrossIrqScheduler *scheduler, int enabled) {
    int owned;
    if (!scheduler_is_owner(scheduler) || (enabled != 0 && enabled != 1) ||
        scheduler->pumping || scheduler->collecting || scheduler->faulted || !scheduler_lock())
        return 0;
    owned = active_scheduler == scheduler && scheduler->installed &&
        vsync_callback == scheduler_vblank;
    scheduler_unlock();
    if (!owned) return 0;
    return musashi_cpu_status_set_enabled(scheduler->cpu_status, enabled);
}

/* Acquire the owner dispatch lease without consuming source metadata. */
static int scheduler_begin_dispatch(MusashiPsyCrossIrqScheduler *scheduler,
                                     const void *continuation) {
    if (!scheduler_is_owner(scheduler)) { scheduler->fault_reason = "not-owner-thread"; return 0; }
    if (!continuation) { scheduler->fault_reason = "no-continuation"; return 0; }
    if (scheduler->pumping || scheduler->collecting) { scheduler->fault_reason = "reentered"; return 0; }
    if (scheduler->faulted) {
        if (!scheduler->fault_reason) scheduler->fault_reason = "already-faulted";
        return 0;
    }
    if (!scheduler_lock()) { scheduler->fault_reason = "lock"; return 0; }
    if (active_scheduler != scheduler || !scheduler->installed ||
        vsync_callback != scheduler_vblank) {
        scheduler_unlock();
        scheduler->fault_reason = "not-installed";
        return 0;
    }
    if (scheduler->source_overflow) {
        scheduler_unlock();
        scheduler->faulted = 1;
        scheduler->last_result = 0;
        /* The VBlank source queued more edges than the CPU thread consumed:
         * with host-paced time a starved CPU thread falls behind real time. */
        scheduler->fault_reason = "source-overflow";
        return 0;
    }
    scheduler->pumping = 1;
    scheduler_unlock();
    return 1;
}

/* Caller holds the owner dispatch lease, never the source mutex. */
static int scheduler_dispatch(MusashiPsyCrossIrqScheduler *scheduler,
                              const void *continuation) {
    MusashiIrqDispatchDevice dispatch_device;
    MusashiCpuContext context = {0};
    MusashiCpuStatusSnapshot state;
    const char *why = "dispatch";
    uint16_t status, mask;
    int enabled;
    if (scheduler->faulted ||
        !musashi_cpu_status_irq_eligible(scheduler->cpu_status, &enabled) ||
        !scheduler->device->read16(scheduler->device->userdata, 0x1f801070u, &status) ||
        !scheduler->device->read16(scheduler->device->userdata, 0x1f801074u, &mask) || scheduler->faulted)
        { why = "irq-status-read"; goto fault; }
    if (!enabled || !(status & mask)) {
        scheduler->pumping = 0;
        return 1;
    }
    if (!scheduler->context_provider || !scheduler->before_dispatch ||
        !scheduler->context_provider(scheduler->context_userdata, continuation, &context) ||
        context.identity != continuation || scheduler->faulted) { why = "no-cpu-context"; goto fault; }
    if (context.provenance == MUSASHI_CPU_CONTEXT_SOURCE &&
        ((context.instruction >> 24) & 0xfeu) == 0x4au) {
        scheduler->pumping = 0;
        return 1;
    }
    if (!musashi_cpu_status_begin(scheduler->cpu_status, MUSASHI_CPU_EXCEPTION_IRQ,
            &context, 0x400u, &scheduler->active_exception)) { why = "exception-begin"; goto fault; }
    scheduler->active_continuation = continuation;
    scheduler->delivering = 1;
    scheduler->return_consumed = 0;
    if (!scheduler->before_dispatch(scheduler->before_dispatch_userdata, status & mask) ||
        scheduler->faulted ||
        !musashi_cpu_status_inspect(scheduler->cpu_status, &scheduler->active_exception, &state) ||
        state.faulted || !state.kernel_entered ||
        !scheduler->device->read16(scheduler->device->userdata, 0x1f801070u, &status) ||
        !scheduler->device->read16(scheduler->device->userdata, 0x1f801074u, &mask) || scheduler->faulted)
        { why = "kernel-entry"; goto fault; }
    if (!(status & mask)) goto finish;
    if (scheduler->deliveries == UINT32_MAX) { why = "delivery-count"; goto fault; }
    dispatch_device = (MusashiIrqDispatchDevice){
        scheduler, scheduler_read16, scheduler_write16, scheduler_execute,
        scheduler_diagnostic, scheduler_return_from_exception
    };
    /* IRQ callbacks stay interpreted: the dispatcher's return and the CD
     * IRQ frame account for the guest calls they see. */
    musashi_native_lane_suspend(1);
    scheduler->last_result = musashi_boot_call_800427f4(scheduler->memory, &dispatch_device);
    musashi_native_lane_suspend(0);
    scheduler->deliveries++;
    if (scheduler->faulted ||
        scheduler->last_result != MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED ||
        !scheduler->return_consumed) { why = "dispatcher-return"; goto fault; }
finish:
    if (!musashi_cpu_status_finish(scheduler->cpu_status, &scheduler->active_exception)) { why = "exception-finish"; goto fault; }
    memset(&scheduler->active_exception, 0, sizeof scheduler->active_exception);
    scheduler->active_continuation = NULL;
    scheduler->delivering = 0;
    scheduler->return_consumed = 0;
    scheduler->pumping = 0;
    return 1;
fault:
    if (scheduler->active_exception.owner)
        (void)musashi_cpu_status_fault(scheduler->cpu_status, &scheduler->active_exception);
    scheduler->delivering = 0;
    scheduler->return_consumed = 0;
    scheduler->pumping = 0;
    scheduler->last_result = 0;
    scheduler->faulted = 1;
    scheduler->fault_reason = why;
    return 0;
}

int musashi_psycross_irq_scheduler_dispatch_pending(
    MusashiPsyCrossIrqScheduler *scheduler, const void *continuation) {
    if (!scheduler_begin_dispatch(scheduler, continuation)) return 0;
    return scheduler_dispatch(scheduler, continuation);
}

int musashi_psycross_irq_scheduler_pump(MusashiPsyCrossIrqScheduler *scheduler,
                                       const void *continuation) {
    MusashiIrqSourceBatch batch;
    if (!scheduler_begin_dispatch(scheduler, continuation)) return 0;
    if (!musashi_psycross_irq_scheduler_collect_source(scheduler, &batch) ||
        (batch.count && !scheduler->raise_vblank(scheduler->raise_userdata))) {
        scheduler->pumping = 0;
        scheduler->faulted = 1;
        scheduler->last_result = 0;
        return 0;
    }
    return scheduler_dispatch(scheduler, continuation);
}

int musashi_psycross_irq_scheduler_hook_entry_int(void *userdata,
                                                  uint32_t context_address) {
    MusashiPsyCrossIrqScheduler *scheduler = userdata;
    uint64_t tick, frequency;
    MusashiCpuStatusSnapshot cpu;
    if (!scheduler || (scheduler->owner_thread && !scheduler_is_owner(scheduler)) ||
        !scheduler->cpu_status ||
        !musashi_cpu_status_snapshot(scheduler->cpu_status, &cpu) || cpu.faulted || cpu.active ||
        !scheduler->memory || !scheduler->device ||
        !scheduler->device->read16 || !scheduler->device->write16 ||
        !scheduler->device->execute || !scheduler->raise_vblank ||
        scheduler->installed || scheduler->faulted || scheduler->collecting || scheduler->pumping)
        return 0;
    if (!scheduler_lock())
        return 0;
    tick = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    if (active_scheduler || vsync_callback || scheduler->source_overflow ||
        scheduler->pending_vblanks || !frequency ||
        (scheduler->source_frequency && scheduler->source_frequency != frequency) ||
        tick < scheduler->source_cut_tick || tick < scheduler->source_tick ||
        scheduler->source_sequence != scheduler->source_vblanks) {
        scheduler_unlock();
        return 0;
    }
    scheduler->source_frequency = frequency;
    scheduler->source_cut_tick = scheduler->source_tick = tick;
    scheduler->owner_thread = (uint64_t)SDL_ThreadID();
    scheduler->context_address = context_address;
    scheduler->last_result = MUSASHI_IRQ_CONTEXT_RETURN_PENDING;
    active_scheduler = scheduler;
    VSyncCallback(scheduler_vblank);
    if (vsync_callback != scheduler_vblank) {
        active_scheduler = NULL;
        scheduler_unlock();
        return 0;
    }
    scheduler->installed = 1;
    scheduler_unlock();
    return 1;
}

int musashi_psycross_irq_scheduler_uninstall(MusashiPsyCrossIrqScheduler *scheduler) {
    if (!scheduler_is_owner(scheduler) || scheduler->pumping || scheduler->collecting || !scheduler_lock())
        return 0;
    if (active_scheduler != scheduler || !scheduler->installed ||
        vsync_callback != scheduler_vblank) {
        scheduler_unlock();
        return 0;
    }
    VSyncCallback(NULL);
    if (vsync_callback != NULL) {
        scheduler_unlock();
        return 0;
    }
    scheduler->installed = 0;
    active_scheduler = NULL;
    scheduler_unlock();
    return 1;
}

static int runtime_read16(void *userdata, uint32_t address, uint16_t *value) {
    MusashiPsyCrossIrqRuntime *runtime = userdata;
    return musashi_irq_controller_read16(&runtime->controller, address, value);
}

static int runtime_write16(void *userdata, uint32_t address, uint16_t value) {
    MusashiPsyCrossIrqRuntime *runtime = userdata;
    return musashi_irq_controller_write16(&runtime->controller, address, value);
}

static int runtime_call_one(void *userdata, MusashiBootMemory *memory,
    uint32_t target, uint32_t argument, uint32_t *result) {
    MusashiPsyCrossIrqRuntime *runtime = userdata;
    const MusashiVblankExecutor *requests = runtime->requests;
    if (requests && requests->call_one &&
        requests->call_one(requests->userdata, memory, target, argument, result))
        return 1;
    runtime->failed_target = target;
    return 0;
}

static int runtime_call_two(void *userdata, MusashiBootMemory *memory,
    uint32_t target, uint32_t first, uint32_t second) {
    MusashiPsyCrossIrqRuntime *runtime = userdata;
    const MusashiVblankExecutor *requests = runtime->requests;
    if (requests && requests->call_two &&
        requests->call_two(requests->userdata, memory, target, first, second))
        return 1;
    runtime->failed_target = target;
    return 0;
}

static int runtime_source_execute(MusashiPsyCrossIrqRuntime *runtime,
    MusashiBootMemory *memory, uint32_t target) {
    if (runtime->source_execute &&
        runtime->source_execute(runtime->source_execute_userdata, memory, target))
        return 1;
    runtime->failed_target = target;
    return 0;
}

static int runtime_vblank_callback(void *userdata, MusashiBootMemory *memory,
    uint32_t target) {
    MusashiPsyCrossIrqRuntime *runtime = userdata;
    if (target == 0x800616d0u)
        return musashi_boot_call_800616d0(memory, &runtime->vblank);
    if (target == 0x8001096cu)
        return musashi_boot_call_8001096c(memory, runtime_vblank_callback, userdata);
    return runtime_source_execute(runtime, memory, target);
}

static int runtime_dma_channel_callback(void *userdata, MusashiBootMemory *memory,
    uint32_t target) {
    return runtime_source_execute(userdata, memory, target);
}

static int runtime_irq_callback(void *userdata, MusashiBootMemory *memory,
    uint32_t target) {
    MusashiPsyCrossIrqRuntime *runtime = userdata;
    if (target == 0x80042ce8u)
        return musashi_boot_call_80042ce8(memory, runtime_vblank_callback, userdata);
    if (target == 0x80042e08u && runtime->dma_device &&
        scheduler_is_owner(&runtime->scheduler) && runtime->scheduler.delivering &&
        runtime->scheduler.active_continuation && !runtime->scheduler.return_consumed &&
        musashi_boot_call_80042e08(memory, runtime->dma_device,
            runtime_dma_channel_callback, userdata))
        return 1;
    if (target == 0x80045290u && runtime->cd_registers && runtime->cd_clock &&
        scheduler_is_owner(&runtime->scheduler) && runtime->scheduler.delivering &&
        runtime->scheduler.active_continuation && !runtime->scheduler.return_consumed &&
        musashi_boot_execute_cd_irq_with_services(memory, runtime->cd_registers, runtime->cd_clock,
            runtime->cd_services,
            &runtime->cd_frame, runtime->scheduler.context_address, target, &runtime->cd_stop))
        return 1;
    runtime->failed_target = target;
    return 0;
}

int musashi_psycross_irq_runtime_bind_cd(MusashiPsyCrossIrqRuntime *runtime,
    const MusashiCdRegisterDevice *registers, const MusashiExecutionClock *clock) {
    if (!runtime || !runtime->scheduler.memory || !registers || !registers->read8 ||
        !registers->write8 || !clock || !clock->advance || runtime->cd_registers ||
        runtime->cd_clock || runtime->cd_frame.busy || runtime->cd_frame.faulted ||
        runtime->scheduler.pumping || runtime->scheduler.delivering ||
        runtime->scheduler.collecting || runtime->scheduler.faulted ||
        (runtime->scheduler.owner_thread && !scheduler_is_owner(&runtime->scheduler))) return 0;
    runtime->cd_registers = registers;
    runtime->cd_clock = clock;
    return 1;
}

int musashi_psycross_irq_runtime_bind_source_execute(
    MusashiPsyCrossIrqRuntime *runtime,
    int (*execute)(void *, MusashiBootMemory *, uint32_t), void *userdata) {
    if (!runtime || !runtime->scheduler.memory || !execute ||
        runtime->source_execute || runtime->scheduler.pumping ||
        runtime->scheduler.delivering || runtime->scheduler.collecting ||
        runtime->scheduler.faulted ||
        (runtime->scheduler.owner_thread && !scheduler_is_owner(&runtime->scheduler)))
        return 0;
    runtime->source_execute = execute;
    runtime->source_execute_userdata = userdata;
    return 1;
}

int musashi_psycross_irq_runtime_bind_dma(MusashiPsyCrossIrqRuntime *runtime,
    const MusashiDmaCallbackRegistrationDevice *device) {
    if (!runtime || !runtime->scheduler.memory || !device || !device->read32 ||
        !device->write32 || runtime->dma_device ||
        runtime->scheduler.pumping || runtime->scheduler.delivering ||
        runtime->scheduler.collecting || runtime->scheduler.faulted ||
        (runtime->scheduler.owner_thread && !scheduler_is_owner(&runtime->scheduler)))
        return 0;
    runtime->dma_device = device;
    return 1;
}

int musashi_psycross_irq_runtime_bind_cd_services(MusashiPsyCrossIrqRuntime *runtime,
    const MusashiCdIrqServices *services) {
    if (!runtime || !runtime->scheduler.memory || !runtime->cd_registers ||
        !runtime->cd_clock || !services || !services->deliver_event ||
        runtime->cd_services || runtime->cd_frame.busy || runtime->cd_frame.faulted ||
        runtime->scheduler.pumping || runtime->scheduler.delivering ||
        runtime->scheduler.collecting || runtime->scheduler.faulted ||
        (runtime->scheduler.owner_thread && !scheduler_is_owner(&runtime->scheduler))) return 0;
    runtime->cd_services = services;
    return 1;
}

static uint64_t runtime_thread(void *unused) {
    (void)unused; return (uint64_t)SDL_ThreadID();
}

int musashi_psycross_irq_scheduler_bind_cpu(MusashiPsyCrossIrqScheduler *s,
                                            MusashiCpuStatus *cpu) {
    MusashiCpuStatusSnapshot state;
    if (!s || !cpu || s->cpu_status || s->installed || s->pumping ||
        s->delivering || s->collecting || s->faulted ||
        (s->owner_thread && !scheduler_is_owner(s)) ||
        !musashi_cpu_status_snapshot(cpu, &state) || state.active || state.faulted)
        return 0;
    s->cpu_status = cpu; s->owner_thread = (uint64_t)SDL_ThreadID(); return 1;
}
int musashi_psycross_irq_scheduler_bind_context(MusashiPsyCrossIrqScheduler *s,
    int (*provider)(void *, const void *, MusashiCpuContext *), void *userdata) {
    MusashiCpuStatusSnapshot state;
    if (!scheduler_is_owner(s) || !provider || !s->cpu_status || s->context_provider ||
        s->pumping || s->delivering || s->collecting || s->faulted ||
        !musashi_cpu_status_snapshot(s->cpu_status, &state) || state.active || state.faulted)
        return 0;
    s->context_provider = provider; s->context_userdata = userdata; return 1;
}
int musashi_psycross_irq_scheduler_get_enabled(MusashiPsyCrossIrqScheduler *s, int *out) {
    if (!scheduler_is_owner(s)) return 0;
    return musashi_cpu_status_irq_eligible(s->cpu_status, out);
}
int musashi_psycross_irq_runtime_close(MusashiPsyCrossIrqRuntime *r) {
    if (!r || !r->initialized || !r->cpu_status ||
        !scheduler_is_owner(&r->scheduler) || r->scheduler.installed ||
        r->scheduler.pumping || r->scheduler.delivering || r->scheduler.collecting ||
        r->cd_frame.busy || !musashi_cpu_status_close(r->cpu_status)) return 0;
    r->cpu_status = NULL; r->scheduler.cpu_status = NULL;
    r->scheduler.context_provider = NULL; r->scheduler.context_userdata = NULL;
    r->scheduler.active_continuation = NULL;
    memset(&r->scheduler.active_exception, 0, sizeof r->scheduler.active_exception);
    return 1;
}

int musashi_psycross_irq_runtime_init(MusashiPsyCrossIrqRuntime *runtime,
    MusashiBootMemory *memory, const MusashiVblankExecutor *requests) {
    MusashiCpuStatus *cpu = NULL;
    if (!runtime || !memory || runtime->initialized || runtime->cpu_status ||
        (runtime->scheduler.owner_thread && !scheduler_is_owner(&runtime->scheduler)) ||
        runtime->scheduler.installed)
        return 0;
    if (!musashi_cpu_status_open(&cpu, runtime_thread, NULL)) return 0;
    memset(runtime, 0, sizeof(*runtime));
    runtime->initialized = 1;
    runtime->cpu_status = cpu;
    runtime->scheduler.cpu_status = cpu;
    runtime->scheduler.owner_thread = (uint64_t)SDL_ThreadID();
    musashi_irq_controller_init(&runtime->controller);
    runtime->requests = requests;
    runtime->vblank = (MusashiVblankExecutor){
        runtime, runtime_call_one, runtime_call_two
    };
    runtime->device = (MusashiIrqDispatchDevice){
        runtime, runtime_read16, runtime_write16, runtime_irq_callback, NULL, NULL
    };
    runtime->scheduler.memory = memory;
    runtime->scheduler.device = &runtime->device;
    runtime->scheduler.raise_vblank = musashi_irq_controller_raise_vblank;
    runtime->scheduler.raise_userdata = &runtime->controller;
    return 1;
}
