#ifndef MUSASHI_BIOS_EVENTS_H
#define MUSASHI_BIOS_EVENTS_H

#include <stdint.h>

#define MUSASHI_BIOS_EVENTS_CAPACITY 22u

/* The retail event table is seven guest words per slot. Reserved words remain
 * visible because OpenEvent and CloseEvent do not clear them. */
typedef struct MusashiBiosEventRecord {
    uint32_t class_word;
    uint32_t status;
    uint32_t spec;
    uint32_t mode;
    uint32_t callback;
    uint32_t reserved0;
    uint32_t reserved1;
} MusashiBiosEventRecord;

typedef MusashiBiosEventRecord MusashiBiosEvent;

typedef struct MusashiBiosEventTable {
    MusashiBiosEventRecord records[MUSASHI_BIOS_EVENTS_CAPACITY];
    /* Native lease metadata; this is outside the guest-visible record span. */
    uint64_t allocation_serial[MUSASHI_BIOS_EVENTS_CAPACITY];
} MusashiBiosEventTable;

typedef MusashiBiosEventTable MusashiBiosEvents;

/* guest_target is a guest callback word, never a host function pointer. */
typedef int (*MusashiBiosEventExecutor)(void *userdata, uint32_t guest_target);

void musashi_bios_events_init(MusashiBiosEvents *events);

int musashi_bios_events_open(MusashiBiosEvents *events, uint32_t class_word,
                             uint32_t spec, uint32_t mode, uint32_t callback,
                             int32_t *handle);
int musashi_bios_events_close(MusashiBiosEvents *events, int32_t handle,
                              int32_t *result);
int musashi_bios_events_enable(MusashiBiosEvents *events, int32_t handle,
                               int32_t *result);
int musashi_bios_events_disable(MusashiBiosEvents *events, int32_t handle,
                                int32_t *result);
int musashi_bios_events_test(MusashiBiosEvents *events, int32_t handle,
                             int32_t *result);
/* HwSPU poll analogue of DMA IRQ 8003AA18. Manual 8003A84C is synchronous, so
 * TestEvent(F0000009, spec 0x20, poll) observes completion without a ch4 IRQ.
 * Other handles are ordinary TestEvent. */
int musashi_bios_events_test_sync_spu(MusashiBiosEvents *events, int32_t handle,
                                     int32_t *result);

/* Scan live records in index order. A callback executor is required only for
 * a matching enabled callback-mode record with a nonzero callback word. */
int musashi_bios_events_deliver(MusashiBiosEvents *events, uint32_t class_word,
                                uint32_t spec,
                                MusashiBiosEventExecutor execute,
                                void *userdata);
int musashi_bios_events_undeliver(MusashiBiosEvents *events,
                                  uint32_t class_word, uint32_t spec);

#endif
