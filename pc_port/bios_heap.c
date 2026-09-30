#include "musashi_bios_heap.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static int zero_storage(const MusashiBiosHeap *heap) {
    const unsigned char *bytes;
    size_t i;

    if (!heap) return 0;
    bytes = (const unsigned char *)heap;
    for (i = 0; i < sizeof(*heap); ++i)
        if (bytes[i] != 0) return 0;
    return 1;
}

static void fault(MusashiBiosHeap *heap) {
    if (heap) heap->faulted = 1;
}

/* Begin an owner operation. The callback is entered while busy is set so a
 * callback that calls back into this owner cannot create a second operation. */
static int begin(MusashiBiosHeap *heap) {
    uint64_t token;

    if (!heap || !heap->initialized || heap->faulted || heap->busy ||
        heap->callback_depth != 0 || !heap->current_thread ||
        heap->owner_thread == 0) {
        if (heap && heap->busy) {
            if (heap->initialized) fault(heap);
            if (heap->callback_depth < INT_MAX) heap->callback_depth++;
        }
        return 0;
    }
    heap->busy = 1;
    heap->callback_depth = 1;
    token = heap->current_thread(heap->thread_userdata);
    heap->callback_depth = 0;
    if (token == 0 || token != heap->owner_thread) {
        fault(heap);
        heap->busy = 0;
        return 0;
    }
    if (heap->faulted) {
        heap->busy = 0;
        return 0;
    }
    return 1;
}

static void finish(MusashiBiosHeap *heap) {
    heap->callback_depth = 0;
    heap->busy = 0;
}

int musashi_bios_heap_init(MusashiBiosHeap *heap, MusashiBootMemory *memory,
                           void *thread_userdata,
                           MusashiBiosHeapCurrentThread current_thread) {
    uint64_t token;

    if (!heap || !memory || !current_thread) return 0;
    if (!zero_storage(heap)) {
        /* A constructor callback must not be able to publish a nested lease. */
        if (heap->busy || heap->callback_depth) {
            heap->faulted = 1;
            if (heap->callback_depth < INT_MAX) heap->callback_depth++;
        }
        return 0;
    }
    /* The storage is still unpublished while the identity callback runs. */
    heap->busy = 1;
    heap->callback_depth = 1;
    token = current_thread(thread_userdata);
    heap->callback_depth = 0;
    heap->busy = 0;
    if (token == 0 || heap->faulted) {
        memset(heap, 0, sizeof(*heap));
        return 0;
    }
    heap->memory = memory;
    heap->thread_userdata = thread_userdata;
    heap->current_thread = current_thread;
    heap->owner_thread = token;
    heap->initialized = 1;
    return 1;
}

int musashi_bios_heap_init_heap(MusashiBiosHeap *heap, uint32_t base,
                                uint32_t size) {
    MusashiBootMemory *memory;
    uint8_t *header;
    uint32_t end;

    if (!begin(heap)) return 0;
    memory = heap->memory;
    header = (base & 3u) == 0 ? musashi_boot_ram_span(memory, base, 4) : NULL;
    if (heap->memory != memory || !header) {
        fault(heap);
        finish(heap);
        return 0;
    }
    end = base + (size & ~3u) + 4u;
    /* All fallible checks precede the ordered source effects. */
    heap->cursor = base;
    header[0] = 0;
    header[1] = 0;
    header[2] = 0;
    header[3] = 0;
    heap->end = end;
    heap->preferred_size = size;
    heap->lazy = 0;
    finish(heap);
    return 1;
}

int musashi_bios_heap_snapshot(const MusashiBiosHeap *heap,
                               MusashiBiosHeapSnapshot *snapshot) {
    MusashiBiosHeap *mutable_heap;
    uint64_t token;

    if (!heap || !snapshot || !heap->initialized || heap->faulted ||
        !heap->current_thread || heap->owner_thread == 0)
        return 0;
    mutable_heap = (MusashiBiosHeap *)heap;
    if (heap->busy || heap->callback_depth != 0) {
        fault(mutable_heap);
        if (heap->busy && heap->callback_depth < INT_MAX)
            mutable_heap->callback_depth++;
        return 0;
    }
    mutable_heap->busy = 1;
    mutable_heap->callback_depth = 1;
    token = heap->current_thread(heap->thread_userdata);
    mutable_heap->callback_depth = 0;
    if (token == 0 || token != heap->owner_thread || heap->faulted) {
        fault(mutable_heap);
        mutable_heap->busy = 0;
        return 0;
    }
    snapshot->cursor = heap->cursor;
    snapshot->preferred_size = heap->preferred_size;
    snapshot->end = heap->end;
    snapshot->scan_origin = heap->scan_origin;
    snapshot->current_header = heap->current_header;
    snapshot->owner_thread = heap->owner_thread;
    snapshot->lazy = heap->lazy;
    snapshot->initialized = heap->initialized;
    snapshot->faulted = heap->faulted;
    mutable_heap->busy = 0;
    return 1;
}

int musashi_bios_heap_destroy(MusashiBiosHeap *heap) {
    uint64_t token;

    if (!heap || !heap->initialized || heap->busy ||
        heap->callback_depth != 0 || !heap->current_thread ||
        heap->owner_thread == 0) {
        if (heap && heap->busy) {
            fault(heap);
            if (heap->callback_depth < INT_MAX) heap->callback_depth++;
        }
        return 0;
    }
    heap->busy = 1;
    heap->callback_depth = 1;
    token = heap->current_thread(heap->thread_userdata);
    if (token == 0 || token != heap->owner_thread ||
        heap->callback_depth > 1) {
        fault(heap);
        heap->callback_depth = 0;
        heap->busy = 0;
        return 0;
    }
    memset(heap, 0, sizeof(*heap));
    return 1;
}
