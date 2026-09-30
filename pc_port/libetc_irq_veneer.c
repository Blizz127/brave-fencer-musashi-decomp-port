/* libetc InterruptCallback veneer (guest 800425B0), port-owned HLE: the guest keeps a
 * pointer at 8006CB84 to its interrupt-service table; slot +08 registers an interrupt callback.
 * Dispatch only to the registration service the port implements and
 * refuse any other installed target without writes. */
#include "musashi_callback_registration.h"

int musashi_boot_call_800425b0(MusashiBootMemory *memory,
    const MusashiCallbackRegistrationDevice *device, int32_t index,
    uint32_t callback, uint32_t *previous) {
    uint32_t table, target;
    if (!musashi_boot_read32(memory, 0x8006cb84u, &table) ||
        !musashi_boot_read32(memory, table + 8u, &target))
        return 0;
    return target == 0x800429dcu &&
        musashi_boot_call_800429dc(memory, device, index, callback, previous);
}
