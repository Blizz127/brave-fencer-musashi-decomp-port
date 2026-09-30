/* libmcrd memory-card VBlank service (guest 800616D0, 8006291C, 80062988),
 * port-owned HLE.
 *
 * The card library runs its requests from VBlank as a small stack: the top
 * index lives at 80072A34 (negative when idle), request records are 16 bytes
 * at 80078D38 and each has a handler word at 80078D78. One VBlank step
 * (8006291C) runs the top request's handler with its record and pops the
 * request when the handler reports it finished. The idle query (80062988)
 * is the index's sign bit.
 *
 * The VBlank entry (800616D0) steps only when a request is active; when that
 * step makes the stack idle it completes the operation: it sets the done flag
 * 80078C88, moves the pending result pair 80078C80/80078C84 to the published
 * pair 80078CC4/80078CC8, clears the pending pair, and calls the completion
 * callback at 80078CC0 (if any) with the published pair. Guest calls go
 * through the executor; a refused access or call returns 0. */
#include "musashi_vblank.h"

enum {
    REQUEST_INDEX = 0x80072a34u,
    REQUEST_RECORDS = 0x80078d38u,
    REQUEST_HANDLERS = 0x80078d78u,
    RESULT_PENDING_A = 0x80078c80u,
    RESULT_PENDING_B = 0x80078c84u,
    DONE_FLAG = 0x80078c88u,
    COMPLETION = 0x80078cc0u,
    RESULT_A = 0x80078cc4u,
    RESULT_B = 0x80078cc8u,
};

int musashi_boot_call_80062988(MusashiBootMemory *memory, uint32_t *result) {
    uint32_t index;
    if (!result || !musashi_boot_read32(memory, REQUEST_INDEX, &index)) return 0;
    *result = index >> 31;
    return 1;
}

int musashi_boot_call_8006291c(MusashiBootMemory *memory,
    const MusashiVblankExecutor *executor) {
    uint32_t index, handler, finished, current;
    if (!musashi_boot_read32(memory, REQUEST_INDEX, &index)) return 0;
    if (index & 0x80000000u) return 1;
    if (!musashi_boot_read32(memory, REQUEST_HANDLERS + index * 4u, &handler) ||
        !executor || !executor->call_one ||
        !executor->call_one(executor->userdata, memory, handler,
                            REQUEST_RECORDS + index * 16u, &finished))
        return 0;
    if (finished && (!musashi_boot_read32(memory, REQUEST_INDEX, &current) ||
                     !musashi_boot_write32(memory, REQUEST_INDEX, current - 1u)))
        return 0;
    return 1;
}

int musashi_boot_call_800616d0(MusashiBootMemory *memory,
    const MusashiVblankExecutor *executor) {
    uint32_t idle, a, b, callback;
    if (!musashi_boot_call_80062988(memory, &idle)) return 0;
    if (idle) return 1;
    if (!musashi_boot_call_8006291c(memory, executor) ||
        !musashi_boot_call_80062988(memory, &idle))
        return 0;
    if (!idle) return 1;
    if (!musashi_boot_write32(memory, DONE_FLAG, 1) ||
        !musashi_boot_read32(memory, RESULT_PENDING_A, &a) ||
        !musashi_boot_write32(memory, RESULT_A, a) ||
        !musashi_boot_read32(memory, RESULT_PENDING_B, &b) ||
        !musashi_boot_read32(memory, COMPLETION, &callback) ||
        !musashi_boot_write32(memory, RESULT_B, b) ||
        !musashi_boot_write32(memory, RESULT_PENDING_A, 0) ||
        !musashi_boot_write32(memory, RESULT_PENDING_B, 0))
        return 0;
    if (callback) {
        if (!musashi_boot_read32(memory, RESULT_A, &a) ||
            !musashi_boot_read32(memory, RESULT_B, &b) ||
            !executor || !executor->call_two ||
            !executor->call_two(executor->userdata, memory, callback, a, b))
            return 0;
    }
    return 1;
}
