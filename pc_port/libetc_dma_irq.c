/* libetc DMA interrupt service (guest 80042E08), port-owned HLE.
 *
 * DICR (psx-spx): bits 24-30 are the per-channel completion flags, cleared
 * by writing one; bits 0-23 are control; bit 31 is the IRQ master flag and
 * bit 15 forces an IRQ (bus error). The guest keeps a pointer to DICR at
 * 8006CBC0 and one callback word per channel at 8006CBC4.
 *
 * While any completion flag is set, each flagged channel, lowest first, is
 * acknowledged alone (control bits kept, only its flag written as one) and
 * then its callback, if any, runs. A final DICR that shows a bus error
 * (master flag with no channel flag, or bit 15) is the retail diagnostic
 * path, not a successful drain, and returns 0. */
#include "musashi_callback_registration.h"

enum { DICR_POINTER = 0x8006cbc0u, DMA_CALLBACKS = 0x8006cbc4u, DMA_CHANNELS = 7 };

static uint32_t completion_flags(uint32_t dicr) { return (dicr >> 24) & 0x7fu; }

int musashi_boot_call_80042e08(MusashiBootMemory *memory,
    const MusashiDmaCallbackRegistrationDevice *device,
    MusashiGuestCallbackExecutor execute, void *userdata) {
    uint32_t control, dicr, pending, callback;
    unsigned channel;
    if (!memory || !device || !device->read32 || !device->write32) return 0;
    if (!musashi_boot_read32(memory, DICR_POINTER, &control) || (control & 3u)) return 0;
    if (!device->read32(device->userdata, control, &dicr)) return 0;
    for (pending = completion_flags(dicr); pending; pending = completion_flags(dicr)) {
        for (channel = 0; pending && channel < DMA_CHANNELS; ++channel, pending >>= 1) {
            if (!(pending & 1u)) continue;
            if (!device->read32(device->userdata, control, &dicr) ||
                !device->write32(device->userdata, control,
                                 dicr & ((1u << (channel + 24u)) | 0x00ffffffu)) ||
                !musashi_boot_read32(memory, DMA_CALLBACKS + 4u * channel, &callback))
                return 0;
            if (callback && (!execute || !execute(userdata, memory, callback))) return 0;
        }
        if (!device->read32(device->userdata, control, &dicr)) return 0;
    }
    if (!device->read32(device->userdata, control, &dicr)) return 0;
    return !((dicr & 0xff000000u) == 0x80000000u || (dicr & 0x8000u));
}
