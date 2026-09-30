#ifndef MUSASHI_BIOS_HEAP_H
#define MUSASHI_BIOS_HEAP_H

#include <stdint.h>

#include "musashi_boot_memory.h"

/* Native identity provider. A zero token is unavailable and cannot establish
 * an owner lease. Calls are serialized by the installing owner. */
typedef uint64_t (*MusashiBiosHeapCurrentThread)(void *userdata);

/* Diagnostic state only; this does not mirror the BIOS globals into guest RAM.
 * The scan pointers are retained for the later malloc/free owner. */
typedef struct MusashiBiosHeapSnapshot {
    uint32_t cursor;
    uint32_t preferred_size;
    uint32_t end;
    uint32_t scan_origin;
    uint32_t current_header;
    uint64_t owner_thread;
    int lazy;
    int initialized;
    int faulted;
} MusashiBiosHeapSnapshot;

/* Stable native owner for the selected retail A0:39 InitHeap leaf. The
 * memory pointer is borrowed and must outlive this owner. No host allocator
 * or guest result register is involved. Fresh zeroed storage is required. */
typedef struct MusashiBiosHeap {
    MusashiBootMemory *memory;
    void *thread_userdata;
    MusashiBiosHeapCurrentThread current_thread;
    uint64_t owner_thread;
    uint32_t cursor;
    uint32_t preferred_size;
    uint32_t end;
    uint32_t scan_origin;
    uint32_t current_header;
    int lazy;
    int initialized;
    int faulted;
    int busy;
    int callback_depth;
} MusashiBiosHeap;

/* Fresh constructor. Reinitializing a live or nonzero owner is refused. */
int musashi_bios_heap_init(MusashiBiosHeap *heap, MusashiBootMemory *memory,
                           void *thread_userdata,
                           MusashiBiosHeapCurrentThread current_thread);

/* Selected ROM A0:39 semantics: cursor=base, preferred=size,
 * end=base+(size&~3)+4 modulo 2^32, lazy=0, and one zero guest header word.
 * Existing scan pointers are deliberately retained across repeated calls. */
int musashi_bios_heap_init_heap(MusashiBiosHeap *heap, uint32_t base,
                                uint32_t size);

/* Owner-thread diagnostic copy. Refusal leaves the destination unchanged. */
int musashi_bios_heap_snapshot(const MusashiBiosHeap *heap,
                               MusashiBiosHeapSnapshot *snapshot);

/* Releases only native ownership. Guest RAM and prior headers are untouched. */
int musashi_bios_heap_destroy(MusashiBiosHeap *heap);

#endif
