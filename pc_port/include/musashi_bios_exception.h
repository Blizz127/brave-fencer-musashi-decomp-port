#ifndef MUSASHI_BIOS_EXCEPTION_H
#define MUSASHI_BIOS_EXCEPTION_H
#include <stddef.h>
#include "musashi_boot_memory.h"
#include "musashi_bios_kernel.h"
#include "musashi_cpu_status.h"

typedef enum MusashiBiosExceptionVariant {
    MUSASHI_BIOS_EXCEPTION_ORIGINAL = 1,
    MUSASHI_BIOS_EXCEPTION_GTE_PATCH = 2
} MusashiBiosExceptionVariant;
/* One canonical image in BootMemory. The published variant selects audited
 * native entry semantics on the same CPU token; no guest TCB is fabricated. */
typedef struct MusashiBiosExceptionImage {
    MusashiBootMemory *memory;
    MusashiBiosKernel *kernel;
    MusashiCpuStatus *cpu;
    MusashiBiosExceptionBinding binding;
    uint64_t generation, writes, entries, last_sequence;
    uint32_t written_mask;
    /* Handler/continuation seeded at init: the user's ROM words, or zero
     * stand-ins without a ROM (never executed; entry is serviced natively). */
    uint32_t seed_handler[14], seed_continuation[14];
    MusashiBiosExceptionVariant variant;
    int initialized, busy, dirty, faulted;
} MusashiBiosExceptionImage;

/* Fresh zero storage; immutable selected ROM must be exactly524288 bytes and
 * match the pinned full SHA256. Copies only674..690 andC80..CF0, never CF0. */
int musashi_bios_exception_init(MusashiBiosExceptionImage *, MusashiBootMemory *,
    MusashiBiosKernel *, MusashiCpuStatus *, const uint8_t *rom, size_t size);
/* No ROM: same table, zero stand-in handler/continuation (no BIOS code is
 * carried by the port). */
int musashi_bios_exception_init_selected(MusashiBiosExceptionImage *, MusashiBootMemory *,
    MusashiBiosKernel *, MusashiCpuStatus *);
/* Pure overlap query for routing BEFORE generic RAM at all access widths. */
int musashi_bios_exception_intersects(uint32_t address, size_t size);
int musashi_bios_exception_table(MusashiBiosExceptionImage *, uint32_t *);
int musashi_bios_exception_read32(MusashiBiosExceptionImage *, uint32_t, uint32_t *);
int musashi_bios_exception_write32(MusashiBiosExceptionImage *, uint32_t, uint32_t);
/* Bounded A0:44: requires all14 words actually written, complete recognized
 * variant, critical exclusion, immutable continuation/table. No guest v0. */
int musashi_bios_exception_publish(MusashiBiosExceptionImage *);
/* Actual required kernel consumer; never bypass the shared CPU token. */
int musashi_bios_exception_enter(void *, const MusashiCpuExceptionToken *);
/* After scheduler removal. A faulted CPU frame can be discarded at teardown;
 * healthy active entry refuses. Does not clear RAM or complete an exception. */
int musashi_bios_exception_destroy(MusashiBiosExceptionImage *);
#endif
