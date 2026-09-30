#ifndef MUSASHI_VBLANK_H
#define MUSASHI_VBLANK_H
#include "musashi_boot_memory.h"

/* Checked execution of the one- and two-word guest call sites in 8006291C
 * and 800616D0. Targets and arguments are guest words, never host pointers.
 * Return one only after real bound code returns; call_one must then publish
 * its guest return word. Unknown/refused/incomplete targets return zero.
 * Callbacks share RAM with their callers and may change the live queue state.
 * The owner serializes execution and keeps this descriptor stable. */
typedef struct MusashiVblankExecutor {
    void *userdata;
    int (*call_one)(void *, MusashiBootMemory *, uint32_t target,
                    uint32_t argument, uint32_t *result);
    int (*call_two)(void *, MusashiBootMemory *, uint32_t target,
                    uint32_t first, uint32_t second);
} MusashiVblankExecutor;

/* One means the shared body returned. Zero preserves prior effects and does
 * not provide a resumable frame; do not retry it as a fresh call. Query output
 * must not alias RAM and is only published on success. Negative queue indices
 * require no external executor. Addresses and arithmetic retain guest width. */
int musashi_boot_call_80062988(MusashiBootMemory *memory, uint32_t *result);
int musashi_boot_call_8006291c(MusashiBootMemory *memory,
    const MusashiVblankExecutor *executor);
int musashi_boot_call_800616d0(MusashiBootMemory *memory,
    const MusashiVblankExecutor *executor);

/* MusashiGuestCallbackExecutor router for the retail-observed slot-seven
 * target only. userdata is a MusashiVblankExecutor, possibly NULL on idle
 * paths. Unknown outer targets refuse before invoking any game body. */
int musashi_boot_execute_vblank_callback(void *userdata,
    MusashiBootMemory *memory, uint32_t target);
#endif
