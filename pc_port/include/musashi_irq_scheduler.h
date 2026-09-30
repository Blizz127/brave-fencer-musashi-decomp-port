#ifndef MUSASHI_IRQ_SCHEDULER_H
#define MUSASHI_IRQ_SCHEDULER_H

#include <stdint.h>

#include "musashi_irq_dispatch.h"
#include "musashi_irq_controller.h"
#include "musashi_vblank.h"
#include "musashi_cpu_status.h"
#include "musashi_callback_registration.h"

/* The owner pump latches queued VBlank into the IRQ before the dispatcher samples
 * it. The provider owns the virtual device state and returns zero when it
 * cannot deliver the event; it must not manufacture a completed game frame. */
typedef int (*MusashiIrqRaiseVblank)(void *userdata);

/* Actual PsyCross callback arrivals; never reconstructed missed frames. */
#define MUSASHI_IRQ_SOURCE_CAPACITY 64u
typedef struct MusashiIrqSourceEdge {
    uint64_t sequence; /* Starts at one; retained across successful collections. */
    uint64_t tick;     /* SDL_GetPerformanceCounter domain. */
} MusashiIrqSourceEdge;

typedef struct MusashiIrqSourceBatch {
    uint64_t frequency;
    uint64_t cut_tick; /* Sampled under the source mutex after queued arrivals. */
    uint32_t count;
    MusashiIrqSourceEdge edges[MUSASHI_IRQ_SOURCE_CAPACITY];
} MusashiIrqSourceBatch;

typedef struct MusashiPsyCrossIrqScheduler {
    MusashiBootMemory *memory;
    const MusashiIrqDispatchDevice *device;
    MusashiIrqRaiseVblank raise_vblank;
    void *raise_userdata;
    uint32_t context_address;
    uint32_t deliveries;
    int last_result;
    int installed;
    int delivering;
    int pumping;
    /* Why the last dispatch refused (static string), for diagnostics. */
    const char *fault_reason;
    uint64_t owner_thread;
    uint64_t source_vblanks; /* Owner-consumed source count, not deliveries. */
    uint32_t pending_vblanks; /* Protected by PsyCross source mutex. */
    int source_overflow;     /* Sticky capacity/metadata failure; source mutex. */
    MusashiIrqSourceEdge source_edges[MUSASHI_IRQ_SOURCE_CAPACITY];
    uint64_t source_sequence;  /* Last produced sequence; source mutex. */
    uint64_t source_tick;      /* Last produced tick; source mutex. */
    uint64_t source_cut_tick;  /* Last accepted collection/install cut; mutex. */
    uint64_t source_frequency; /* Fixed at installation. */
    int collecting;           /* Owner-only collection reentry guard. */
    MusashiCpuStatus *cpu_status; /* Borrowed, same lifetime as runtime CPU. */
    int (*context_provider)(void *, const void *, MusashiCpuContext *);
    void *context_userdata;
    /* Valid from IRQ begin through before_dispatch and source B017 unwind.
     * Retained after failure for diagnosis; no manufactured RFE. */
    MusashiCpuExceptionToken active_exception;
    /* Required for an actual eligible IRQ: live BIOS handler owners run
     * before the game dispatcher. Masked/absent work needs no provider.
     * Must acknowledge actual CPU kernel entry through active_exception.
     * Refusal faults the pump; pending registers are resampled afterward. */
    int (*before_dispatch)(void *userdata, uint16_t pending);
    void *before_dispatch_userdata;
    const void *active_continuation;
    int return_consumed;
    /* A failed dispatch leaves partial guest effects. Latch it until the
     * owner destroys/reloads the runtime; uninstall/reinstall is no resume. */
    int faulted;
    /* Guest-clock owners (native_boot MUSASHI_GUEST_CLOCK=1) take no time
     * from host VBlanks and only drain them; for them a full host queue drops
     * the edge (counted) instead of latching source_overflow. Host-paced
     * owners leave this 0: an overflow there is a real lost VBlank. */
    int host_edges_advisory;
    uint64_t dropped_host_vblanks;  /* source mutex */
} MusashiPsyCrossIrqScheduler;

/* One controller is shared by startup registration and delivered interrupts.
 * Own this at a stable address, with zero-initialized storage before init.
 * The RAM and optional request executor must outlive installation; serialize
 * guest execution and observations on the installing thread. The PsyCross
 * callback queues source edges only; its mutex protects queue and registration
 * lifetime. Reinitialization is for a freshly loaded guest only.
 *
 * The built-in executor runs recovered 80042CE8 -> 800616D0 and its helpers.
 * Additional request/completion calls require an explicit executor. Missing
 * or refused targets are recorded; no game-success fallback is supplied. */
typedef struct MusashiPsyCrossIrqRuntime {
    MusashiCpuStatus *cpu_status; /* Owned, never recreated on hook install. */
    int initialized; /* Fresh init only; remains set after checked close. */
    MusashiIrqController controller;
    MusashiIrqDispatchDevice device;
    MusashiPsyCrossIrqScheduler scheduler;
    MusashiVblankExecutor vblank;
    const MusashiVblankExecutor *requests;
    uint32_t failed_target;
    const MusashiCdRegisterDevice *cd_registers;
    const MusashiExecutionClock *cd_clock;
    MusashiCdIrqFrame cd_frame;
    MusashiResetGraphPrefixStop cd_stop;
    const MusashiCdIrqServices *cd_services;
    const MusashiDmaCallbackRegistrationDevice *dma_device;
    int (*source_execute)(void *, MusashiBootMemory *, uint32_t);
    void *source_execute_userdata;
} MusashiPsyCrossIrqRuntime;

int musashi_psycross_irq_runtime_init(MusashiPsyCrossIrqRuntime *runtime,
    MusashiBootMemory *memory, const MusashiVblankExecutor *requests);

int musashi_psycross_irq_runtime_close(MusashiPsyCrossIrqRuntime *);
int musashi_psycross_irq_scheduler_bind_cpu(MusashiPsyCrossIrqScheduler *, MusashiCpuStatus *);
int musashi_psycross_irq_scheduler_bind_context(MusashiPsyCrossIrqScheduler *,
    int (*provider)(void *, const void *, MusashiCpuContext *), void *userdata);
int musashi_psycross_irq_scheduler_get_enabled(MusashiPsyCrossIrqScheduler *, int *);

/* Bind once on the runtime owner, outside dispatch. Providers must outlive
 * installation. This does not register a guest callback or manufacture IRQs. */
int musashi_psycross_irq_runtime_bind_cd(MusashiPsyCrossIrqRuntime *,
    const MusashiCdRegisterDevice *, const MusashiExecutionClock *);

/* Optional BIOS service capability for source CD callbacks. Bind once after
 * the CD device, outside dispatch; the provider must outlive installation. */
int musashi_psycross_irq_runtime_bind_cd_services(MusashiPsyCrossIrqRuntime *,
    const MusashiCdIrqServices *);

/* Bind once on the runtime owner, outside dispatch. The DICR provider must
 * outlive installation. This does not raise DMA IRQs or install a slot. */
int musashi_psycross_irq_runtime_bind_dma(MusashiPsyCrossIrqRuntime *,
    const MusashiDmaCallbackRegistrationDevice *);

/* Bind once on the runtime owner, outside dispatch. Nested source execution
 * of IRQ slot targets must outlive installation. This does not raise IRQs. */
int musashi_psycross_irq_runtime_bind_source_execute(MusashiPsyCrossIrqRuntime *,
    int (*execute)(void *, MusashiBootMemory *, uint32_t), void *userdata);

/* B0:19 / HookEntryInt adapter. It records the supplied guest context without
 * interpreting it as a host frame, then installs the single PsyCross VBlank
 * callback. The scheduler owns this object for its installed lifetime. */
int musashi_psycross_irq_scheduler_hook_entry_int(void *userdata,
                                                  uint32_t context_address);

/* Owner-thread guest interrupt eligibility, independent of the source mutex.
 * Fresh CPU construction starts disabled. Hook installation preserves SR.
 * Only 0/1 are accepted; this does not pump or perform a SYS. */
int musashi_psycross_irq_scheduler_set_enabled(MusashiPsyCrossIrqScheduler *, int);

/* Atomic source-only collection. Accepted empty batches still publish a cut
 * and frequency. Output is untouched on refusal. Consumes queued metadata and
 * increments source_vblanks; it does not raise IRQs or execute device/game code.
 * The owner may collect during its active callback, but cannot collect again
 * recursively. Uninstalled/foreign ownership refuses. Capacity, time, sequence
 * or frequency failures latch the scheduler fault and retain the queued data.
 * A caller that collects directly owns applying every returned event; pump
 * will not subsequently apply that consumed batch. */
int musashi_psycross_irq_scheduler_collect_source(MusashiPsyCrossIrqScheduler *,
                                                  MusashiIrqSourceBatch *);

/* Dispatch already-latched hardware only. Does not collect source metadata or
 * raise VBlank. Keeps BIOS priority/resampling and B0:17 continuation semantics.
 * Calling from inside any active dispatch/pump is refused. */
int musashi_psycross_irq_scheduler_dispatch_pending(MusashiPsyCrossIrqScheduler *,
                                                    const void *continuation);

/* Synchronous owner-thread checkpoint. continuation identifies the caller's
 * live suspended execution (never a guest address or captured RAM record).
 * Returns 1 for accepted pending/masked work or a completed delegated return;
 * 0 rejects misuse or latches a dispatch/source failure. Queued source edges
 * coalesce into hardware status; pending hardware is sampled on every pump.
 * No guest RAM/controller work runs on the source thread. No cycle parity is
 * claimed. The caller must not hold the source mutex across guest execution. */
int musashi_psycross_irq_scheduler_pump(MusashiPsyCrossIrqScheduler *,
                                       const void *continuation);

/* Remove only a callback installed by this scheduler. No prior callback is
 * restored because installation refuses to replace one. */
int musashi_psycross_irq_scheduler_uninstall(MusashiPsyCrossIrqScheduler *scheduler);

#endif
