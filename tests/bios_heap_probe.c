#include "musashi_bios_heap.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ThreadContext {
    uint64_t token;
    MusashiBiosHeap *heap;
    MusashiBootMemory *memory;
    int reenter;
    int nested_refused;
    int constructor_reenter;
    int constructor_nested_refused;
} ThreadContext;

static uint64_t current_thread(void *userdata) {
    ThreadContext *context = (ThreadContext *)userdata;
    if (context->reenter && context->heap) {
        context->reenter = 0;
        context->nested_refused =
            !musashi_bios_heap_init_heap(context->heap, 0x80001000u, 4u);
    }
    if (context->constructor_reenter && context->heap) {
        context->constructor_reenter = 0;
        context->constructor_nested_refused = !musashi_bios_heap_init_heap(
            context->heap, 0x80001000u, 4u);
    }
    return context->token;
}

static unsigned changed_bytes(const MusashiBootMemory *before,
                              const MusashiBootMemory *after) {
    unsigned changed = 0;
    unsigned i;
    for (i = 0; i < MUSASHI_RAM_SIZE; ++i)
        if (before->bytes[i] != after->bytes[i]) ++changed;
    return changed;
}

static void make_owner(MusashiBiosHeap *heap, MusashiBootMemory *memory,
                       ThreadContext *context, uint8_t fill) {
    memset(memory, fill, sizeof(*memory));
    memset(heap, 0, sizeof(*heap));
    memset(context, 0, sizeof(*context));
    context->token = 7;
    assert(musashi_bios_heap_init(heap, memory, context, current_thread));
    context->heap = heap;
    context->memory = memory;
}

static void test_selected_effects(void) {
    MusashiBiosHeap heap;
    MusashiBootMemory memory, before;
    ThreadContext context;
    MusashiBiosHeapSnapshot snapshot;

    make_owner(&heap, &memory, &context, 0xa5);
    before = memory;
    assert(musashi_bios_heap_snapshot(&heap, &snapshot));
    assert(snapshot.cursor == 0 && snapshot.preferred_size == 0 &&
           snapshot.end == 0 && snapshot.scan_origin == 0 &&
           snapshot.current_header == 0 && snapshot.owner_thread == 7 &&
           snapshot.lazy == 0 && snapshot.initialized == 1 &&
           snapshot.faulted == 0);

    heap.scan_origin = 0x80123450u;
    heap.current_header = 0x80123454u;
    heap.lazy = 1;
    assert(musashi_bios_heap_init_heap(&heap, 0x801ff800u, 0x800u));
    assert(changed_bytes(&before, &memory) == 4);
    assert(!memcmp(memory.bytes + 0x1ff800u, "\0\0\0\0", 4));
    assert(musashi_bios_heap_snapshot(&heap, &snapshot));
    assert(snapshot.cursor == 0x801ff800u &&
           snapshot.preferred_size == 0x800u &&
           snapshot.end == 0x80200004u && snapshot.lazy == 0 &&
           snapshot.scan_origin == 0x80123450u &&
           snapshot.current_header == 0x80123454u);

    before = memory;
    assert(musashi_bios_heap_init_heap(&heap, 0x80001000u, 7u));
    assert(changed_bytes(&before, &memory) == 4);
    assert(musashi_bios_heap_snapshot(&heap, &snapshot));
    assert(snapshot.cursor == 0x80001000u &&
           snapshot.preferred_size == 7u && snapshot.end == 0x80001008u &&
           snapshot.lazy == 0 && snapshot.scan_origin == 0x80123450u &&
           snapshot.current_header == 0x80123454u);

    assert(musashi_bios_heap_init_heap(&heap, 0x80100000u, UINT32_MAX));
    assert(musashi_bios_heap_snapshot(&heap, &snapshot));
    assert(snapshot.preferred_size == UINT32_MAX &&
           snapshot.end == 0x80100000u);
    assert(musashi_bios_heap_destroy(&heap));
    assert(!memcmp(memory.bytes + 0x100000u, "\0\0\0\0", 4));
}

static void test_invalid_and_ownership_guards(void) {
    static const uint32_t bad_bases[] = {
        0x80000001u, 0x00100000u, 0xa0100000u, 0x80200000u
    };
    MusashiBiosHeap heap;
    MusashiBootMemory memory, before;
    ThreadContext context;
    MusashiBiosHeapSnapshot snapshot = {0xa5a5a5a5u, 0, 0, 0, 0, 0, 1, 1, 0};
    unsigned i;

    for (i = 0; i < sizeof(bad_bases) / sizeof(bad_bases[0]); ++i) {
        make_owner(&heap, &memory, &context, 0x5a);
        before = memory;
        assert(!musashi_bios_heap_init_heap(&heap, bad_bases[i], 0x800u));
        assert(!memcmp(&memory, &before, sizeof(memory)));
        assert(heap.faulted == 1);
        assert(!musashi_bios_heap_snapshot(&heap, &snapshot));
        assert(snapshot.cursor == 0xa5a5a5a5u);
        context.token = 7;
        assert(musashi_bios_heap_destroy(&heap));
        assert(!memcmp(&memory, &before, sizeof(memory)));
    }

    make_owner(&heap, &memory, &context, 0x66);
    before = memory;
    context.token = 8;
    assert(!musashi_bios_heap_init_heap(&heap, 0x80001000u, 4u));
    assert(!memcmp(&memory, &before, sizeof(memory)));
    assert(heap.faulted == 1);
    context.token = 7;
    assert(musashi_bios_heap_destroy(&heap));
    assert(!heap.initialized && !memcmp(&memory, &before, sizeof(memory)));

    make_owner(&heap, &memory, &context, 0x77);
    before = memory;
    context.reenter = 1;
    assert(!musashi_bios_heap_init_heap(&heap, 0x80001000u, 4u));
    assert(context.nested_refused == 1 && heap.faulted == 1);
    assert(!memcmp(&memory, &before, sizeof(memory)));
    context.reenter = 0;
    assert(musashi_bios_heap_destroy(&heap));

    make_owner(&heap, &memory, &context, 0x79);
    before = memory;
    context.reenter = 1;
    assert(!musashi_bios_heap_destroy(&heap));
    assert(heap.initialized == 1 && heap.faulted == 1);
    assert(!memcmp(&memory, &before, sizeof(memory)));
    context.reenter = 0;
    assert(musashi_bios_heap_destroy(&heap));

    make_owner(&heap, &memory, &context, 0x88);
    before = memory;
    context.token = 9;
    assert(!musashi_bios_heap_destroy(&heap));
    assert(!memcmp(&memory, &before, sizeof(memory)));
    assert(heap.initialized == 1 && heap.faulted == 1);
}

static void test_constructor_and_destroy_refusal(void) {
    MusashiBiosHeap heap;
    MusashiBootMemory memory;
    ThreadContext context;
    unsigned char before[sizeof(heap)];

    memset(&heap, 0, sizeof(heap));
    memset(&memory, 0x99, sizeof(memory));
    memset(&context, 0, sizeof(context));
    context.token = 0;
    memcpy(before, &heap, sizeof(heap));
    assert(!musashi_bios_heap_init(&heap, &memory, &context, current_thread));
    assert(!memcmp(&heap, before, sizeof(heap)));

    memset(&heap, 0, sizeof(heap));
    context.token = 7;
    context.heap = &heap;
    context.memory = &memory;
    context.constructor_reenter = 1;
    assert(musashi_bios_heap_init(&heap, &memory, &context, current_thread));
    assert(context.constructor_nested_refused == 1);
    assert(heap.initialized == 1 && heap.faulted == 0);
    assert(!memcmp(memory.bytes, "\x99\x99\x99\x99", 4));
    assert(musashi_bios_heap_destroy(&heap));

    memset(&heap, 0, sizeof(heap));
    memset(&context, 0, sizeof(context));
    context.token = 7;
    assert(musashi_bios_heap_init(&heap, &memory, &context, current_thread));
    assert(!musashi_bios_heap_init(&heap, &memory, &context, current_thread));
    assert(musashi_bios_heap_destroy(&heap));
    assert(!musashi_bios_heap_destroy(&heap));
    assert(!memcmp(memory.bytes, "\x99\x99\x99\x99", 4));
}

static int parse_word(const char *text, uint32_t *value) {
    char *end;
    unsigned long parsed;

    if (!text || !value) return 0;
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int run_case(const char *base_text, const char *size_text) {
    static MusashiBootMemory memory;
    MusashiBiosHeap heap;
    ThreadContext context;
    MusashiBiosHeapSnapshot snapshot;
    uint32_t base;
    uint32_t size;
    uint32_t header;

    if (!parse_word(base_text, &base) || !parse_word(size_text, &size))
        return 2;
    memset(&memory, 0xa5, sizeof(memory));
    memset(&heap, 0, sizeof(heap));
    memset(&context, 0, sizeof(context));
    context.token = 7;
    if (!musashi_bios_heap_init(&heap, &memory, &context, current_thread) ||
        !musashi_bios_heap_snapshot(&heap, &snapshot))
        return 3;
    heap.scan_origin = 0x80123450u;
    heap.current_header = 0x80123454u;
    if (!musashi_bios_heap_init_heap(&heap, base, size) ||
        !musashi_bios_heap_snapshot(&heap, &snapshot) ||
        !musashi_boot_read32(&memory, base, &header))
        return 4;
    printf("cursor=%08x preferred=%08x end=%08x lazy=%d header=%08x "
           "scan=%08x current=%08x\n",
           snapshot.cursor, snapshot.preferred_size, snapshot.end,
           snapshot.lazy, header, snapshot.scan_origin,
           snapshot.current_header);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3) return run_case(argv[1], argv[2]);
    if (argc != 1) return 2;
    test_selected_effects();
    test_invalid_and_ownership_guards();
    test_constructor_and_destroy_refusal();
    puts("BIOS_HEAP_PASS");
    return 0;
}
