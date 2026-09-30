#ifndef MUSASHI_BIOS_KERNEL_H
#define MUSASHI_BIOS_KERNEL_H

#include "musashi_bios_events.h"

typedef struct MusashiBiosKernel MusashiBiosKernel;
struct MusashiCpuExceptionToken;
typedef int (*MusashiBiosIrqHandler)(void *userdata, uint16_t pending);

/* Separate from the card-owned CF0 hook. Once installed, this kernel cannot
 * silently return to the legacy image-less path after the lease is removed. */
typedef struct MusashiBiosExceptionBinding {
    MusashiBiosKernel *owner;
    int (*enter)(void *userdata, const struct MusashiCpuExceptionToken *token);
    void *userdata;
} MusashiBiosExceptionBinding;

/* The selected BIOS has one early exception patch site. Native code publishes
 * a stable callable owner instead of copying instructions into guest low RAM.
 * This hook precedes priority IRQ dispatch and also runs for SYS1/SYS2. */
typedef struct MusashiBiosEarlyHook {
    MusashiBiosKernel *owner;
    int (*execute)(void *userdata);
    void *userdata;
} MusashiBiosEarlyHook;

/* Native kernel registrations, never guest BIOS addresses. Nodes and their
 * userdata must outlive registration. The owning thread serializes all calls;
 * handlers may use event services but may not mutate this registry. */
typedef struct MusashiBiosIrqNode {
    struct MusashiBiosIrqNode *next;
    MusashiBiosKernel *owner;
    MusashiBiosIrqHandler execute;
    void *userdata;
    uint16_t mask;
    unsigned priority;
} MusashiBiosIrqNode;

struct MusashiBiosKernel {
    MusashiBiosEvents events;
    MusashiBiosIrqNode *heads[4];
    MusashiBiosIrqNode cd_nodes[2];
    int32_t cd_events[5];
    uint64_t cd_serial[5];
    int cd_installed;
    int dispatching;
    MusashiBiosEarlyHook *early_hook;
    MusashiBiosIrqNode *executing_node;
    unsigned executing_priority;
    MusashiBiosExceptionBinding *exception_binding;
    int exception_required;
};

int musashi_bios_kernel_install_exception_binding(MusashiBiosKernel *,
    MusashiBiosExceptionBinding *,
    int (*enter)(void *, const struct MusashiCpuExceptionToken *), void *);
int musashi_bios_kernel_remove_exception_binding(MusashiBiosKernel *,
    MusashiBiosExceptionBinding *);
int musashi_bios_kernel_before_exception_with_context(MusashiBiosKernel *,
    const struct MusashiCpuExceptionToken *);
int musashi_bios_kernel_dispatch_with_context(MusashiBiosKernel *, uint16_t,
    const struct MusashiCpuExceptionToken *);

/* Initialize fresh, unregistered storage only. This does not bootstrap a
 * PS1 BIOS or import low RAM. Game RAM is not part of this platform owner. */
void musashi_bios_kernel_init(MusashiBiosKernel *kernel);
int musashi_bios_kernel_enqueue(MusashiBiosKernel *kernel,
    MusashiBiosIrqNode *node, unsigned priority, uint16_t mask,
    MusashiBiosIrqHandler execute, void *userdata);
int musashi_bios_kernel_dequeue(MusashiBiosKernel *kernel,
    MusashiBiosIrqNode *node);
int musashi_bios_kernel_dispatch(MusashiBiosKernel *kernel, uint16_t pending);
/* The live priority handler may mutate its own strictly earlier-priority
 * resources. The current/later iteration lists, other userdata owners, early
 * hooks and callers outside dispatch have no such capability. */
int musashi_bios_kernel_enqueue_from_handler(MusashiBiosKernel *,
    MusashiBiosIrqNode *caller, MusashiBiosIrqNode *node, unsigned priority,
    uint16_t mask, MusashiBiosIrqHandler execute, void *userdata);
int musashi_bios_kernel_dequeue_from_handler(MusashiBiosKernel *,
    MusashiBiosIrqNode *caller, MusashiBiosIrqNode *node);
int musashi_bios_kernel_can_install_early_hook(MusashiBiosKernel *, MusashiBiosEarlyHook *);
int musashi_bios_kernel_install_early_hook(MusashiBiosKernel *, MusashiBiosEarlyHook *,
    int (*execute)(void *), void *userdata);
int musashi_bios_kernel_remove_early_hook(MusashiBiosKernel *, MusashiBiosEarlyHook *);
/* SYS callers invoke this before their exception service. IRQ dispatch does
 * so internally. Refusal retains hook effects; no recursion or mutation of
 * registered kernel owners is permitted during a hook. */
int musashi_bios_kernel_before_exception(MusashiBiosKernel *);

/* Create the native BIOS CD session through real registry/event operations:
 * IO then DMA registration; five class F0000003 polling events and enables.
 * A handler is mandatory; unavailable CD execution must refuse when reached.
 * Insufficient capacity refuses before modifying ownership. This is a native
 * service bootstrap contract, not the BIOS's disc boot transaction. */
int musashi_bios_kernel_start_cd(MusashiBiosKernel *kernel,
    MusashiBiosIrqHandler execute, void *userdata);

/* A0:72 lifecycle: close owned events, unlink IO then DMA, preserve all other
 * resources. Internal allocation serials guard the native CD leases without
 * changing retail event handles or B0 slot aliasing. Missing/inconsistent
 * ownership refuses before any cleanup. Guest
 * interrupt eligibility is independently controlled by the runtime. */
int musashi_bios_kernel_remove_cd(MusashiBiosKernel *kernel);

#endif
