#ifndef MUSASHI_CALLBACK_REGISTRATION_H
#define MUSASHI_CALLBACK_REGISTRATION_H

#include "musashi_boot_memory.h"

/* Checked device/BIOS operations used by retail 800429DC. Returning zero
 * refuses an operation. The provider owns hardware and BIOS semantics. */
typedef struct MusashiCallbackRegistrationDevice {
    void *userdata;
    int (*read16)(void *, uint32_t address, uint16_t *value);
    int (*write16)(void *, uint32_t address, uint16_t value);
    int (*b0_5b)(void *, int32_t disabled);
    int (*c0_0a)(void *, int32_t channel, int32_t disabled);
} MusashiCallbackRegistrationDevice;

/* Execute the decomp-owned registration body against shared guest RAM.
 * Returns one and publishes the previous guest callback word only on return.
 * Returns zero on invalid memory/device access or an unavailable/refused BIOS
 * operation; preceding guest/device effects survive and must not be retried
 * as though registration completed. Same-value and uninitialized-guard paths
 * require no device. Index address/shift arithmetic follows MIPS word rules;
 * a computed slot outside supported cached RAM is refused.
 * previous must not overlap memory or device. The device table must remain
 * valid and unchanged during the call; the owner serializes access to RAM.
 * This registers guest words; it does not deliver interrupts or run callbacks. */
int musashi_boot_call_800429dc(MusashiBootMemory *memory,
    const MusashiCallbackRegistrationDevice *device, int32_t index,
    uint32_t callback, uint32_t *previous);

/* Shared 800425B0 wrapper: read the live guest table pointer and its +08
 * target, then invoke the recovered registration body. Unknown targets are
 * refused without writes; there is no assumed dispatch-table location. */
int musashi_boot_call_800425b0(MusashiBootMemory *memory,
    const MusashiCallbackRegistrationDevice *device, int32_t index,
    uint32_t callback, uint32_t *previous);

/* Checked DMA control-word operations for retail 80042F8C. The provider owns
 * register semantics; the RAM pointer cell selects the device address. */
typedef struct MusashiDmaCallbackRegistrationDevice {
    void *userdata;
    int (*read32)(void *, uint32_t address, uint32_t *value);
    int (*write32)(void *, uint32_t address, uint32_t value);
} MusashiDmaCallbackRegistrationDevice;

/* Same completion/output/ownership contract as 800429DC above. This routine
 * snapshots the control pointer before publishing the callback and uses that
 * address for both device operations. Failed device access therefore leaves
 * the published callback visible. Unchanged callbacks need no device.
 * Registering a guest word does not execute a callback or transfer DMA data. */
int musashi_boot_call_80042f8c(MusashiBootMemory *memory,
    const MusashiDmaCallbackRegistrationDevice *device, int32_t index,
    uint32_t callback, uint32_t *previous);

/* Shared 800425E0 wrapper: same dispatch contract, using table slot +04. */
int musashi_boot_call_800425e0(MusashiBootMemory *memory,
    const MusashiDmaCallbackRegistrationDevice *device, int32_t index,
    uint32_t callback, uint32_t *previous);

/* Shared 80042E08 DMA IRQ handler. The RAM pointer cell selects DICR; the
 * provider owns write-one-to-clear acknowledgement. Each flagged channel is
 * acknowledged before its 8006CBC4 slot is loaded. Empty slots need no
 * executor. A refused callback keeps the acknowledge. The bus-error print
 * path is not a successful drain and returns zero. This does not raise DMA
 * IRQs or complete a transfer. */
int musashi_boot_call_80042e08(MusashiBootMemory *memory,
    const MusashiDmaCallbackRegistrationDevice *device,
    MusashiGuestCallbackExecutor execute, void *userdata);

#endif
