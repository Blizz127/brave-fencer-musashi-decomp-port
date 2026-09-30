/* libetc VSync callback service (guest 80042CE8), port-owned HLE.
 * On each VBlank the guest counts a tick at 8006CBB8 and runs the eight
 * VSync callback words at 8006CB98 in order, skipping empty ones. A refused
 * access or callback stops the service and returns 0. */
#include "musashi_boot_memory.h"

enum { VSYNC_CALLBACKS = 0x8006cb98u, VSYNC_TICK = 0x8006cbb8u, VSYNC_SLOTS = 8 };

int musashi_boot_call_80042ce8(MusashiBootMemory *memory,
    MusashiGuestCallbackExecutor execute, void *userdata) {
    uint32_t tick, callback;
    unsigned i;
    if (!musashi_boot_read32(memory, VSYNC_TICK, &tick) ||
        !musashi_boot_write32(memory, VSYNC_TICK, tick + 1u))
        return 0;
    for (i = 0; i < VSYNC_SLOTS; ++i) {
        if (!musashi_boot_read32(memory, VSYNC_CALLBACKS + 4u * i, &callback)) return 0;
        if (callback && (!execute || !execute(userdata, memory, callback))) return 0;
    }
    return 1;
}
