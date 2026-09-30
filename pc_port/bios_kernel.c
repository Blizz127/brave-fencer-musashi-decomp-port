#include "musashi_bios_kernel.h"
#include <string.h>

static const uint32_t cd_specs[5] = {0x10u, 0x20u, 0x40u, 0x80u, 0x8000u};

void musashi_bios_kernel_init(MusashiBiosKernel *kernel) {
    if (kernel) memset(kernel, 0, sizeof(*kernel));
}

int musashi_bios_kernel_install_exception_binding(MusashiBiosKernel *kernel,
    MusashiBiosExceptionBinding *binding,
    int (*enter)(void *, const struct MusashiCpuExceptionToken *), void *userdata) {
    if (!kernel || !binding || !enter || kernel->dispatching ||
        kernel->exception_binding || binding->owner || binding->enter || binding->userdata)
        return 0;
    binding->owner = kernel;
    binding->enter = enter;
    binding->userdata = userdata;
    kernel->exception_binding = binding;
    kernel->exception_required = 1;
    return 1;
}

int musashi_bios_kernel_remove_exception_binding(MusashiBiosKernel *kernel,
    MusashiBiosExceptionBinding *binding) {
    if (!kernel || !binding || kernel->dispatching ||
        kernel->exception_binding != binding || binding->owner != kernel)
        return 0;
    kernel->exception_binding = NULL;
    binding->owner = NULL;
    binding->enter = NULL;
    binding->userdata = NULL;
    return 1;
}

static int run_exception_entry(MusashiBiosKernel *kernel,
    const struct MusashiCpuExceptionToken *token) {
    MusashiBiosExceptionBinding *binding = kernel->exception_binding;
    if (!binding) return !kernel->exception_required && !token;
    return token && binding->owner == kernel && binding->enter &&
        binding->enter(binding->userdata, token);
}

int musashi_bios_kernel_can_install_early_hook(MusashiBiosKernel *kernel,
                                             MusashiBiosEarlyHook *hook) {
    return kernel && hook && !kernel->dispatching &&
        ((!kernel->early_hook && !hook->owner) ||
         (kernel->early_hook == hook && hook->owner == kernel));
}

int musashi_bios_kernel_install_early_hook(MusashiBiosKernel *kernel,
    MusashiBiosEarlyHook *hook, int (*execute)(void *), void *userdata) {
    if (!execute || !musashi_bios_kernel_can_install_early_hook(kernel, hook)) return 0;
    hook->owner = kernel;
    hook->execute = execute;
    hook->userdata = userdata;
    kernel->early_hook = hook;
    return 1;
}

int musashi_bios_kernel_remove_early_hook(MusashiBiosKernel *kernel,
                                        MusashiBiosEarlyHook *hook) {
    if (!kernel || !hook || kernel->dispatching || kernel->early_hook != hook ||
        hook->owner != kernel) return 0;
    kernel->early_hook = NULL;
    hook->owner = NULL;
    hook->execute = NULL;
    hook->userdata = NULL;
    return 1;
}

static int run_early_hook(MusashiBiosKernel *kernel) {
    MusashiBiosEarlyHook *hook = kernel->early_hook;
    return !hook || (hook->owner == kernel && hook->execute && hook->execute(hook->userdata));
}

int musashi_bios_kernel_before_exception_with_context(MusashiBiosKernel *kernel,
    const struct MusashiCpuExceptionToken *token) {
    int accepted;
    if (!kernel || kernel->dispatching) return 0;
    kernel->dispatching = 1;
    accepted = run_exception_entry(kernel, token) && run_early_hook(kernel);
    kernel->dispatching = 0;
    return accepted;
}

int musashi_bios_kernel_before_exception(MusashiBiosKernel *kernel) {
    return musashi_bios_kernel_before_exception_with_context(kernel, NULL);
}

static int enqueue_node(MusashiBiosKernel *kernel,
    MusashiBiosIrqNode *node, unsigned priority, uint16_t mask,
    MusashiBiosIrqHandler execute, void *userdata) {
    if (!kernel || !node || node->owner || priority >= 4 || !mask ||
        (mask & ~0x7ffu) || !execute)
        return 0;
    node->owner = kernel;
    node->priority = priority;
    node->mask = mask;
    node->execute = execute;
    node->userdata = userdata;
    node->next = kernel->heads[priority];
    kernel->heads[priority] = node;
    return 1;
}

int musashi_bios_kernel_enqueue(MusashiBiosKernel *kernel,
    MusashiBiosIrqNode *node, unsigned priority, uint16_t mask,
    MusashiBiosIrqHandler execute, void *userdata) {
    if (!kernel || kernel->dispatching) return 0;
    return enqueue_node(kernel, node, priority, mask, execute, userdata);
}

static MusashiBiosIrqNode **find_node(MusashiBiosKernel *kernel,
                                    MusashiBiosIrqNode *node) {
    MusashiBiosIrqNode **link;
    if (node->owner != kernel || node->priority >= 4) return NULL;
    for (link = &kernel->heads[node->priority]; *link; link = &(*link)->next)
        if (*link == node) return link;
    return NULL;
}

static int dequeue_node(MusashiBiosKernel *kernel,
    MusashiBiosIrqNode *node) {
    MusashiBiosIrqNode **link;
    if (!kernel || !node) return 0;
    link = find_node(kernel, node);
    if (!link) return 0;
    *link = node->next;
    node->next = NULL;
    node->owner = NULL;
    return 1;
}

int musashi_bios_kernel_dequeue(MusashiBiosKernel *kernel,
                                MusashiBiosIrqNode *node) {
    if (!kernel || kernel->dispatching) return 0;
    return dequeue_node(kernel, node);
}

static int executing_owner(MusashiBiosKernel *kernel, MusashiBiosIrqNode *caller,
                            unsigned target_priority) {
    return kernel && caller && kernel->dispatching &&
        kernel->executing_node == caller && caller->owner == kernel &&
        caller->priority == kernel->executing_priority &&
        target_priority < kernel->executing_priority;
}

int musashi_bios_kernel_enqueue_from_handler(MusashiBiosKernel *kernel,
    MusashiBiosIrqNode *caller, MusashiBiosIrqNode *node, unsigned priority,
    uint16_t mask, MusashiBiosIrqHandler execute, void *userdata) {
    if (!executing_owner(kernel, caller, priority) || userdata != caller->userdata)
        return 0;
    return enqueue_node(kernel, node, priority, mask, execute, userdata);
}

int musashi_bios_kernel_dequeue_from_handler(MusashiBiosKernel *kernel,
    MusashiBiosIrqNode *caller, MusashiBiosIrqNode *node) {
    if (!node || !executing_owner(kernel, caller, node->priority) ||
        node->userdata != caller->userdata)
        return 0;
    return dequeue_node(kernel, node);
}

int musashi_bios_kernel_dispatch_with_context(MusashiBiosKernel *kernel,
    uint16_t pending, const struct MusashiCpuExceptionToken *token) {
    unsigned priority;
    if (!kernel || kernel->dispatching || (pending & ~0x7ffu)) return 0;
    kernel->dispatching = 1;
    if (!run_exception_entry(kernel, token) || !run_early_hook(kernel)) {
        kernel->dispatching = 0;
        return 0;
    }
    for (priority = 0; priority < 4; priority++) {
        MusashiBiosIrqNode *node;
        for (node = kernel->heads[priority]; node; node = node->next) {
            uint16_t selected = pending & node->mask;
            if (selected) {
                int accepted;
                kernel->executing_node = node;
                kernel->executing_priority = priority;
                accepted = node->execute(node->userdata, selected);
                kernel->executing_node = NULL;
                kernel->executing_priority = 0;
                if (!accepted) {
                    kernel->dispatching = 0;
                    return 0;
                }
            }
        }
    }
    kernel->dispatching = 0;
    return 1;
}

int musashi_bios_kernel_dispatch(MusashiBiosKernel *kernel, uint16_t pending) {
    return musashi_bios_kernel_dispatch_with_context(kernel, pending, NULL);
}

int musashi_bios_kernel_start_cd(MusashiBiosKernel *kernel,
    MusashiBiosIrqHandler execute, void *userdata) {
    unsigned i, free_slots = 0;
    int32_t result;
    if (!kernel || !execute || kernel->dispatching || kernel->cd_installed ||
        kernel->cd_nodes[0].owner || kernel->cd_nodes[1].owner)
        return 0;
    for (i = 0; i < MUSASHI_BIOS_EVENTS_CAPACITY && free_slots < 5; i++) {
        if (!kernel->events.records[i].status) {
            if (kernel->events.allocation_serial[i] == UINT64_MAX) return 0;
            free_slots++;
        }
    }
    if (free_slots < 5) return 0;
    /* C0:02 inserts at the head: IO then DMA produces DMA -> IO. */
    if (!musashi_bios_kernel_enqueue(kernel, &kernel->cd_nodes[0], 0, 4,
                                     execute, userdata) ||
        !musashi_bios_kernel_enqueue(kernel, &kernel->cd_nodes[1], 0, 8,
                                     execute, userdata))
        return 0;
    for (i = 0; i < 5; i++) {
        if (!musashi_bios_events_open(&kernel->events, 0xf0000003u, cd_specs[i],
                                     0x2000u, 0, &kernel->cd_events[i]))
            return 0;
        kernel->cd_serial[i] = kernel->events.allocation_serial[
            (uint32_t)kernel->cd_events[i] & 0xffffu];
    }
    for (i = 0; i < 5; i++)
        if (!musashi_bios_events_enable(&kernel->events, kernel->cd_events[i], &result))
            return 0;
    kernel->cd_installed = 1;
    return 1;
}

int musashi_bios_kernel_remove_cd(MusashiBiosKernel *kernel) {
    unsigned i;
    int32_t result;
    if (!kernel || kernel->dispatching || !kernel->cd_installed ||
        !find_node(kernel, &kernel->cd_nodes[0]) ||
        !find_node(kernel, &kernel->cd_nodes[1]))
        return 0;
    for (i = 0; i < 5; i++) {
        uint32_t index = (uint32_t)kernel->cd_events[i] & 0xffffu;
        const MusashiBiosEventRecord *record;
        if (index >= MUSASHI_BIOS_EVENTS_CAPACITY ||
            kernel->cd_serial[i] != kernel->events.allocation_serial[index]) return 0;
        record = &kernel->events.records[index];
        if (!record->status || record->class_word != 0xf0000003u ||
            record->spec != cd_specs[i] || record->mode != 0x2000u || record->callback)
            return 0;
    }
    for (i = 0; i < 5; i++)
        if (!musashi_bios_events_close(&kernel->events, kernel->cd_events[i], &result))
            return 0;
    if (!musashi_bios_kernel_dequeue(kernel, &kernel->cd_nodes[0]) ||
        !musashi_bios_kernel_dequeue(kernel, &kernel->cd_nodes[1]))
        return 0;
    kernel->cd_installed = 0;
    return 1;
}
