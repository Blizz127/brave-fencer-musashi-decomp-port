/* libetc DMACallback registration (guest 80042F8C), port-owned HLE.
 *
 * Behaviour, from the PS1 DMA interrupt register (DICR, psx-spx):
 * bits 16-22 enable the per-channel completion IRQ, bit 23 is the master
 * enable, and bits 24-30 are write-one-to-clear flags, so they are always
 * written as zero here to avoid acknowledging a pending completion.
 *
 * The guest keeps one callback word per channel at 8006CBC4 and a pointer to
 * DICR at 8006CBC0. Registering a new non-zero callback publishes it, then
 * enables the channel and the master bit; registering zero publishes zero,
 * keeps the master bit and disables the channel. An unchanged callback
 * touches nothing. The previous word is returned.
 *
 * Contract (musashi_callback_registration.h): the DICR pointer is read
 * before the callback is published and that address is used for both device
 * accesses, so a failed device access leaves the published callback visible;
 * the result is written only on success. */
#include "musashi_callback_registration.h"

enum {
    CALLBACK_TABLE = 0x8006cbc4u,
    DICR_POINTER = 0x8006cbc0u,
    DICR_KEEP = 0x00ffffffu,          /* never write the flag bits back */
    DICR_MASTER_ENABLE = 0x00800000u,
};

int musashi_boot_call_80042f8c(MusashiBootMemory *memory,
    const MusashiDmaCallbackRegistrationDevice *device, int32_t index,
    uint32_t callback, uint32_t *result) {
    uint32_t slot, previous, dicr_address, dicr, channel_bit;
    if (!memory || !result) return 0;
    slot = (uint32_t)index * 4u + CALLBACK_TABLE;
    if (!musashi_boot_read32(memory, slot, &previous)) return 0;
    if (callback != previous) {
        channel_bit = 1u << (((uint32_t)index + 16u) & 31u);
        if (!musashi_boot_read32(memory, DICR_POINTER, &dicr_address) ||
            !musashi_boot_write32(memory, slot, callback) ||
            !device || !device->read32 || (dicr_address & 3u) ||
            !device->read32(device->userdata, dicr_address, &dicr))
            return 0;
        dicr = (dicr & DICR_KEEP) | DICR_MASTER_ENABLE;
        dicr = callback ? dicr | channel_bit : dicr & ~channel_bit;
        if (!device->write32 || !device->write32(device->userdata, dicr_address, dicr))
            return 0;
    }
    *result = previous;
    return 1;
}
