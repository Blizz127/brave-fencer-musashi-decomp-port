#include "musashi_bios_kernel.h"

#include <assert.h>
#include <string.h>

typedef struct HandlerContext {
    MusashiBiosKernel *kernel;
    MusashiBiosIrqNode *current;
    MusashiBiosIrqNode *add_node;
    MusashiBiosIrqNode *remove_node;
    MusashiBiosIrqNode *target_node;
    MusashiBiosIrqNode *foreign_node;
    MusashiBiosIrqNode fake_caller;
    MusashiBiosIrqNode later_node;
    unsigned add_calls;
    unsigned remove_calls;
    unsigned target_calls;
    unsigned ordinary_refusals;
    unsigned privileged_refusals;
} HandlerContext;

static int target_handler(void *userdata, uint16_t pending) {
    HandlerContext *context = userdata;
    assert(pending == 1);
    assert(context->kernel->executing_node == context->target_node);
    assert(context->kernel->executing_priority == 1);
    context->target_calls++;
    return 1;
}

static int remove_handler(void *userdata, uint16_t pending) {
    HandlerContext *context = userdata;
    assert(pending == 1);
    context->current = context->remove_node;
    context->remove_calls++;
    if (context->remove_calls == 2) {
        assert(musashi_bios_kernel_dequeue_from_handler(
            context->kernel, context->current, context->target_node));
    }
    return 1;
}

static int add_handler(void *userdata, uint16_t pending) {
    HandlerContext *context = userdata;
    MusashiBiosIrqNode ordinary = {0};
    MusashiBiosIrqNode foreign_userdata = {0};
    assert(pending == 1);
    context->current = context->add_node;
    context->add_calls++;

    /* Ordinary mutation remains forbidden from inside a handler. */
    assert(!musashi_bios_kernel_enqueue(context->kernel, &ordinary, 1, 1,
                                        target_handler, context));
    assert(!musashi_bios_kernel_dequeue(context->kernel, context->remove_node));
    context->ordinary_refusals += 2;

    /* Authority is tied to the executing node, its priority, and userdata. */
    assert(!musashi_bios_kernel_enqueue_from_handler(
        context->kernel, &context->fake_caller, &ordinary, 1, 1,
        target_handler, context));
    assert(!musashi_bios_kernel_enqueue_from_handler(
        context->kernel, context->current, &ordinary, 2, 1,
        target_handler, context));
    assert(!musashi_bios_kernel_enqueue_from_handler(
        context->kernel, context->current, &context->later_node, 3, 1,
        target_handler, context));
    assert(!musashi_bios_kernel_enqueue_from_handler(
        context->kernel, context->current, &foreign_userdata, 1, 1,
        target_handler, &foreign_userdata));
    assert(!musashi_bios_kernel_dequeue_from_handler(
        context->kernel, context->current, context->remove_node));
    assert(!musashi_bios_kernel_dequeue_from_handler(
        context->kernel, context->current, context->foreign_node));
    context->privileged_refusals += 6;

    /* The one permitted mutation is to the strictly earlier priority list. */
    if (context->add_calls == 1) {
        assert(musashi_bios_kernel_enqueue_from_handler(
            context->kernel, context->current, context->target_node, 1, 1,
            target_handler, context));
        assert(context->kernel->heads[2] == context->add_node);
        assert(context->kernel->heads[2]->next == context->remove_node);
    }
    return 1;
}

static int early_hook(void *userdata) {
    HandlerContext *context = userdata;
    MusashiBiosIrqNode node = {0};
    assert(!musashi_bios_kernel_enqueue_from_handler(
        context->kernel, context->add_node, &node, 1, 1,
        target_handler, context));
    return 1;
}

static int refusing_handler(void *userdata, uint16_t pending) {
    HandlerContext *context = userdata;
    assert(pending == 1);
    assert(!musashi_bios_kernel_enqueue_from_handler(
        context->kernel, context->add_node, &context->later_node, 3, 1,
        target_handler, context));
    return 0;
}

int main(void) {
    MusashiBiosKernel kernel;
    MusashiBiosKernel foreign_kernel;
    MusashiBiosKernel refusal_kernel;
    HandlerContext context = {0};
    MusashiBiosIrqNode add_node = {0};
    MusashiBiosIrqNode remove_node = {0};
    MusashiBiosIrqNode target_node = {0};
    MusashiBiosIrqNode foreign_node = {0};
    MusashiBiosIrqNode refusal_node = {0};
    MusashiBiosEarlyHook hook = {0};

    musashi_bios_kernel_init(&kernel);
    musashi_bios_kernel_init(&foreign_kernel);
    context.kernel = &kernel;
    context.add_node = &add_node;
    context.remove_node = &remove_node;
    context.target_node = &target_node;
    context.foreign_node = &foreign_node;
    context.later_node.priority = 3;
    context.fake_caller.priority = 2;
    assert(musashi_bios_kernel_enqueue(&foreign_kernel, &foreign_node, 1, 1,
                                       target_handler, &context));
    assert(musashi_bios_kernel_enqueue(&kernel, &remove_node, 2, 1,
                                       remove_handler, &context));
    assert(musashi_bios_kernel_enqueue(&kernel, &add_node, 2, 1,
                                       add_handler, &context));
    assert(musashi_bios_kernel_enqueue_from_handler(
        &kernel, &add_node, &target_node, 1, 1, target_handler, &context) == 0);
    assert(musashi_bios_kernel_dequeue_from_handler(
        &kernel, &add_node, &target_node) == 0);
    assert(musashi_bios_kernel_install_early_hook(
        &kernel, &hook, early_hook, &context));
    assert(musashi_bios_kernel_before_exception(&kernel));
    assert(musashi_bios_kernel_remove_early_hook(&kernel, &hook));

    /* First dispatch adds the earlier-priority node, but it cannot run in
     * the already-passed priority-1 list. */
    assert(musashi_bios_kernel_dispatch(&kernel, 1));
    assert(context.add_calls == 1 && context.remove_calls == 1);
    assert(context.target_calls == 0);
    assert(kernel.executing_node == NULL && kernel.executing_priority == 0);
    assert(kernel.heads[1] == &target_node);

    /* On the next dispatch the target runs before priority-2 removes it. */
    assert(musashi_bios_kernel_dispatch(&kernel, 1));
    assert(context.target_calls == 1 && context.remove_calls == 2);
    assert(kernel.heads[1] == NULL);
    assert(kernel.executing_node == NULL && kernel.executing_priority == 0);
    assert(context.ordinary_refusals == 4 && context.privileged_refusals == 12);
    assert(foreign_node.owner == &foreign_kernel);

    /* Privileged APIs also refuse when called outside a live handler. */
    assert(!musashi_bios_kernel_enqueue_from_handler(
        &kernel, &add_node, &target_node, 1, 1, target_handler, &context));
    assert(!musashi_bios_kernel_dequeue_from_handler(
        &kernel, &add_node, &target_node));

    /* A handler refusal still clears the execution authority fields. */
    musashi_bios_kernel_init(&refusal_kernel);
    context.kernel = &refusal_kernel;
    context.add_node = &refusal_node;
    context.later_node = (MusashiBiosIrqNode){0};
    assert(musashi_bios_kernel_enqueue(&refusal_kernel, &refusal_node, 2, 1,
                                       refusing_handler, &context));
    assert(!musashi_bios_kernel_dispatch(&refusal_kernel, 1));
    assert(refusal_kernel.executing_node == NULL &&
           refusal_kernel.executing_priority == 0);
    return 0;
}
