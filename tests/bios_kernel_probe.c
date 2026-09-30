#include "musashi_bios_kernel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Log { unsigned calls; uint16_t pending; int accepted; } Log;
static int handler(void *userdata, uint16_t pending) {
    Log *log = userdata;
    log->calls++;
    log->pending |= pending;
    return log->accepted;
}

typedef struct EarlyLog {
    MusashiBiosKernel *kernel;
    MusashiBiosEarlyHook *hook;
    unsigned calls;
    int accepted;
} EarlyLog;

static int early(void *userdata) {
    EarlyLog *log = userdata;
    log->calls++;
    assert(!musashi_bios_kernel_before_exception(log->kernel));
    assert(!musashi_bios_kernel_dispatch(log->kernel, 1));
    assert(!musashi_bios_kernel_remove_early_hook(log->kernel, log->hook));
    assert(!musashi_bios_kernel_install_early_hook(log->kernel, log->hook, early, log));
    return log->accepted;
}

static void test_early_hooks(void) {
    MusashiBiosKernel kernel, foreign;
    MusashiBiosEarlyHook hook = {0}, competing = {0};
    MusashiBiosIrqNode node = {0};
    EarlyLog log = {&kernel, &hook, 0, 1};
    Log irq = {0, 0, 1};
    musashi_bios_kernel_init(&kernel);
    musashi_bios_kernel_init(&foreign);
    assert(musashi_bios_kernel_before_exception(&kernel));
    assert(musashi_bios_kernel_can_install_early_hook(&kernel, &hook));
    assert(musashi_bios_kernel_install_early_hook(&kernel, &hook, early, &log));
    assert(!musashi_bios_kernel_install_early_hook(&foreign, &hook, early, &log));
    assert(!musashi_bios_kernel_install_early_hook(&kernel, &competing, early, &log));
    assert(musashi_bios_kernel_install_early_hook(&kernel, &hook, early, &log));
    assert(musashi_bios_kernel_before_exception(&kernel) && log.calls == 1);
    assert(musashi_bios_kernel_enqueue(&kernel, &node, 0, 1, handler, &irq));
    log.accepted = 0;
    assert(!musashi_bios_kernel_dispatch(&kernel, 1));
    assert(log.calls == 2 && irq.calls == 0 && !kernel.dispatching);
    log.accepted = 1;
    assert(musashi_bios_kernel_dispatch(&kernel, 1));
    assert(log.calls == 3 && irq.calls == 1);
    assert(!musashi_bios_kernel_remove_early_hook(&foreign, &hook));
    assert(!musashi_bios_kernel_remove_early_hook(&kernel, &competing));
    assert(musashi_bios_kernel_remove_early_hook(&kernel, &hook));
    assert(!hook.owner && !kernel.early_hook);
    assert(musashi_bios_kernel_dispatch(&kernel, 1));
    assert(log.calls == 3 && irq.calls == 2);
    assert(musashi_bios_kernel_dequeue(&kernel, &node));
}

static void compare_retail_records(const MusashiBiosKernel *kernel, const char *path) {
    MusashiBiosEventRecord records[MUSASHI_BIOS_EVENTS_CAPACITY];
    unsigned i;
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0xe028, SEEK_SET) == 0);
    assert(fread(records, 1, sizeof(records), file) == sizeof(records));
    assert(fclose(file) == 0);
    /* The native bootstrap reproduces the five CD records and every slot's
     * allocation state. Retail unused records retain earlier BIOS payloads
     * (and stale stack data) that native boot never produced. Require that
     * negative control rather than disguising it as a full BIOS RAM match.
     * test_bios_events.py separately compares all 616 bytes for cleanup from
     * identical captured input, including preservation of those dead bytes. */
    assert(memcmp(records, kernel->events.records, 5 * sizeof(records[0])) == 0);
    for (i = 0; i < MUSASHI_BIOS_EVENTS_CAPACITY; i++)
        assert(records[i].status == kernel->events.records[i].status);
    assert(memcmp(records, kernel->events.records, sizeof(records)) != 0);
}

int main(int argc, char **argv) {
    MusashiBiosKernel kernel;
    MusashiBiosIrqNode unrelated = {0};
    MusashiBiosIrqNode later = {0};
    Log cd = {0, 0, 0}, other = {0, 0, 1};
    int32_t handle, result;
    unsigned i;
    test_early_hooks();
    musashi_bios_kernel_init(&kernel);
    assert(musashi_bios_kernel_enqueue(&kernel, &unrelated, 0, 1, handler, &other));
    assert(!musashi_bios_kernel_enqueue(&kernel, &unrelated, 1, 1, handler, &other));
    assert(musashi_bios_kernel_start_cd(&kernel, handler, &cd));
    assert(!musashi_bios_kernel_start_cd(&kernel, handler, &cd));
    assert(kernel.heads[0] == &kernel.cd_nodes[1]);
    assert(kernel.cd_nodes[1].next == &kernel.cd_nodes[0]);
    assert(kernel.cd_nodes[0].next == &unrelated);
    for (i = 0; i < 5; i++) {
        assert((uint32_t)kernel.cd_events[i] == 0xf1000000u + i);
        assert(kernel.events.records[i].status == 0x2000u);
    }
    /* These are real interrupt owners: unsupported BIOS execution refuses.
     * An unrelated VBlank is routed only to its matching registration. */
    assert(!musashi_bios_kernel_dispatch(&kernel, 4));
    assert(cd.calls == 1 && cd.pending == 4 && other.calls == 0);
    assert(musashi_bios_kernel_dispatch(&kernel, 1));
    assert(other.calls == 1 && other.pending == 1);
    assert(musashi_bios_events_open(&kernel.events, 0x1234, 8, 0x2000, 0, &handle));
    assert((uint32_t)handle == 0xf1000005u);
    assert(musashi_bios_events_enable(&kernel.events, handle, &result));
    assert(musashi_bios_kernel_enqueue(&kernel, &later, 0, 2, handler, &other));
    assert(musashi_bios_kernel_remove_cd(&kernel));
    assert(!kernel.cd_installed && kernel.heads[0] == &later);
    assert(later.next == &unrelated && unrelated.next == NULL);
    for (i = 0; i < 5; i++) {
        assert(kernel.events.records[i].status == 0);
        assert(kernel.events.records[i].class_word == 0xf0000003u);
    }
    assert(kernel.events.records[5].status == 0x2000u);
    assert(musashi_bios_kernel_dispatch(&kernel, 12));
    assert(cd.calls == 1); /* Removed handlers cannot execute. */
    assert(!musashi_bios_kernel_remove_cd(&kernel));
    assert(musashi_bios_events_open(&kernel.events, 99, 1, 0x1000, 4, &handle));
    assert((uint32_t)handle == 0xf1000000u);
    assert(musashi_bios_kernel_dequeue(&kernel, &later));
    assert(musashi_bios_kernel_dequeue(&kernel, &unrelated));
    assert(!musashi_bios_kernel_dequeue(&kernel, &unrelated));

    musashi_bios_kernel_init(&kernel);
    assert(musashi_bios_kernel_start_cd(&kernel, handler, &cd));
    assert(musashi_bios_kernel_dequeue(&kernel, &kernel.cd_nodes[0]));
    {
        MusashiBiosKernel before = kernel;
        assert(!musashi_bios_kernel_remove_cd(&kernel));
        assert(memcmp(&kernel, &before, sizeof(kernel)) == 0);
    }
    musashi_bios_kernel_init(&kernel);
    for (i = 0; i < 18; i++)
        assert(musashi_bios_events_open(&kernel.events, i, 1, 0x2000, 0, &handle));
    {
        MusashiBiosKernel before = kernel;
        assert(!musashi_bios_kernel_start_cd(&kernel, handler, &cd));
        assert(memcmp(&kernel, &before, sizeof(kernel)) == 0);
    }
    /* A reused guest handle still aliases its slot, but native CD ownership
     * cannot authorize cleanup of a separately allocated replacement. */
    musashi_bios_kernel_init(&kernel);
    assert(musashi_bios_kernel_start_cd(&kernel, handler, &cd));
    assert(musashi_bios_events_close(&kernel.events, kernel.cd_events[0], &result));
    assert(musashi_bios_events_open(&kernel.events, 0xf0000003u, 0x10,
                                    0x2000, 0, &handle));
    assert(handle == kernel.cd_events[0]);
    assert(musashi_bios_events_enable(&kernel.events, handle, &result));
    {
        MusashiBiosKernel before = kernel;
        assert(!musashi_bios_kernel_remove_cd(&kernel));
        assert(memcmp(&kernel, &before, sizeof(kernel)) == 0);
    }
    if (argc == 3) {
        /* Execute native resource creation and cleanup, then compare owned
         * records and all allocation states. No snapshot initializes it. */
        musashi_bios_kernel_init(&kernel);
        assert(musashi_bios_kernel_start_cd(&kernel, handler, &cd));
        compare_retail_records(&kernel, argv[1]);
        assert(musashi_bios_kernel_remove_cd(&kernel));
        compare_retail_records(&kernel, argv[2]);
        puts("NATIVE BIOS EVENT LIFECYCLE: 140 owned bytes and 22 slot states match; unused payload differs");
    }
    return 0;
}
