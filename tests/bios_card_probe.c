#include "musashi_bios_card.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static unsigned priority_calls;

static int priority_handler(void *userdata, uint16_t pending) {
    (void)userdata;
    assert(pending == 1);
    priority_calls++;
    return 1;
}

static int foreign_hook(void *userdata) {
    unsigned *calls = userdata;
    (*calls)++;
    return 1;
}

int main(void) {
    MusashiBiosKernel kernel;
    MusashiBiosKernel kernel_before;
    MusashiBiosCard card = {0};
    MusashiBiosCard card_before;
    MusashiBiosCard fresh = {0};
    MusashiBiosKernel fresh_kernel;
    MusashiBiosEarlyHook foreign = {0};
    MusashiBiosIrqNode unrelated = {0};
    int32_t old_initialized = 0x13579bdf;
    uint32_t untouched_event;
    unsigned foreign_calls = 0;

    musashi_bios_kernel_init(&kernel);
    kernel.events.records[3].class_word = 0xdeadbeefu;
    kernel.events.records[3].status = 0x1234u;
    untouched_event = kernel.events.records[3].class_word;
    assert(musashi_bios_kernel_enqueue(&kernel, &unrelated, 0, 1,
                                       priority_handler, NULL));
    assert(musashi_bios_card_init(&card, &kernel));

    /* A constructor only accepts zeroed fresh storage and cannot erase a
     * live or previously prepared lease. */
    card_before = card;
    kernel_before = kernel;
    assert(!musashi_bios_card_init(&card, &kernel));
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);

    assert(card.initialized_word == 0);
    assert(card.pad_started == 0);
    assert(card.fast_track_active == 0);
    assert(card.hook_calls == 0);
    assert(card.sio_descriptor.role == MUSASHI_BIOS_CARD_DESCRIPTOR_SIO);
    assert(card.card_descriptor.role == MUSASHI_BIOS_CARD_DESCRIPTOR_CARD);
    assert(card.sio_descriptor.next == NULL && card.card_descriptor.next == NULL);
    assert(card.sio_descriptor.handler == NULL && card.sio_descriptor.verifier == NULL);
    assert(card.card_descriptor.handler == NULL && card.card_descriptor.verifier == NULL);

    /* A constructed but unpublished card has no lease to remove. */
    musashi_bios_kernel_init(&fresh_kernel);
    assert(musashi_bios_card_init(&fresh, &fresh_kernel));
    assert(musashi_bios_card_shutdown(&fresh));
    assert(fresh.kernel == NULL && fresh_kernel.early_hook == NULL);

    /* A foreign hook blocks service preflight without changing either owner. */
    assert(musashi_bios_kernel_install_early_hook(
        &kernel, &foreign, foreign_hook, &foreign_calls));
    card_before = card;
    kernel_before = kernel;
    old_initialized = 0x13579bdf;
    assert(!musashi_bios_card_init_service(&card, INT32_MIN, &old_initialized));
    assert(old_initialized == 0x13579bdf);
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);
    assert(musashi_bios_kernel_remove_early_hook(&kernel, &foreign));

    assert(musashi_bios_card_init_service(&card, INT32_MIN, &old_initialized));
    assert(old_initialized == 0);
    assert(card.initialized_word == 1);
    assert(card.pad_started == INT32_MIN);
    assert(card.action_in_progress == 0 && card.port == 0);
    assert(card.flags[0] == 1 && card.flags[1] == 1);
    assert(card.fast_track_active == 0);
    assert(card.hook_calls == 0);
    assert(kernel.events.records[3].class_word == untouched_event);
    assert(kernel.events.records[3].status == 0x1234u);
    assert(kernel.heads[0] == &unrelated);
    assert(musashi_bios_kernel_before_exception(&kernel));
    assert(card.hook_calls == 1);
    assert(priority_calls == 0);

    /* Active fast-track state is an unsupported path and is not cleared by
     * InitCARD preflight. The installed hook also refuses before priority. */
    card.fast_track_active = 1;
    card_before = card;
    kernel_before = kernel;
    old_initialized = 0x2468ace0;
    assert(!musashi_bios_card_init_service(&card, 99, &old_initialized));
    assert(old_initialized == 0x2468ace0);
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);
    assert(!musashi_bios_kernel_before_exception(&kernel));
    assert(card.hook_calls == 2);
    assert(!musashi_bios_kernel_dispatch(&kernel, 1));
    assert(card.hook_calls == 3);
    assert(priority_calls == 0);
    card.fast_track_active = 0;

    /* Corrupting the owned callback lease or prepared descriptors refuses
     * instead of allowing a repeated service to erase the corruption. */
    card.early_hook.userdata = NULL;
    card_before = card;
    kernel_before = kernel;
    assert(!musashi_bios_card_init_service(&card, 99, &old_initialized));
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);
    card.early_hook.userdata = &card;

    card.sio_descriptor.next = &card.card_descriptor;
    card_before = card;
    kernel_before = kernel;
    assert(!musashi_bios_card_init_service(&card, 99, &old_initialized));
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);
    card.sio_descriptor.next = NULL;

    card.card_descriptor.handler = &card;
    card_before = card;
    kernel_before = kernel;
    assert(!musashi_bios_card_init_service(&card, 99, &old_initialized));
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);
    card.card_descriptor.handler = NULL;

    /* A kernel slot pointing elsewhere is also an incoherent topology. */
    kernel_before = kernel;
    card_before = card;
    kernel.early_hook = &foreign;
    assert(!musashi_bios_card_init_service(&card, 99, &old_initialized));
    kernel.early_hook = &card.early_hook;
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    kernel_before.early_hook = &card.early_hook;
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);

    /* Reinitialization preserves diagnostic calls and exchanges the old word. */
    card.sio_descriptor.reserved = 0xfeedfaceu;
    card.card_descriptor.reserved = 0x12345678u;
    card.action_in_progress = 0xff;
    card.port = 0x5a;
    card.flags[0] = 0xa5;
    card.flags[1] = 0x5a;
    assert(musashi_bios_card_init_service(&card, -1, &old_initialized));
    assert(old_initialized == 1);
    assert(card.pad_started == -1);
    assert(card.hook_calls == 3);
    assert(!card.sio_descriptor.reserved && !card.card_descriptor.reserved);
    assert(!card.action_in_progress && !card.port);
    assert(card.flags[0] == 1 && card.flags[1] == 1);
    assert(musashi_bios_kernel_before_exception(&kernel));
    assert(card.hook_calls == 4);
    assert(foreign_calls == 0);

    kernel.dispatching = 1;
    old_initialized = 0x2468ace0;
    card.pad_started = 77;
    card_before = card;
    kernel_before = kernel;
    assert(!musashi_bios_card_init_service(&card, 99, &old_initialized));
    assert(old_initialized == 0x2468ace0);
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);
    kernel.dispatching = 0;

    card.hook_calls = UINT64_MAX;
    card_before = card;
    kernel_before = kernel;
    assert(!musashi_bios_kernel_before_exception(&kernel));
    assert(memcmp(&card, &card_before, sizeof(card)) == 0);
    assert(memcmp(&kernel, &kernel_before, sizeof(kernel)) == 0);

    assert(musashi_bios_card_shutdown(&card));
    assert(card.kernel == NULL);
    assert(kernel.early_hook == NULL);
    assert(musashi_bios_kernel_before_exception(&kernel));
    return 0;
}
