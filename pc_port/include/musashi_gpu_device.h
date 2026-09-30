#ifndef MUSASHI_GPU_DEVICE_H
#define MUSASHI_GPU_DEVICE_H

#include <stdint.h>

typedef uint32_t (*MusashiGpuRead32)(void *userdata, uint32_t address);
typedef void (*MusashiGpuWrite32)(void *userdata, uint32_t address,
                                  uint32_t value);
typedef uint16_t (*MusashiGpuRead16)(void *userdata, uint32_t address);
typedef void (*MusashiGpuWrite16)(void *userdata, uint32_t address,
                                  uint16_t value);

/* Explicit platform seam for the two GPU words used by retail 8005C1C0.
 * Callbacks own device semantics; these addresses must never be backed by the
 * cached-RAM array. */
typedef struct MusashiGpuDevice {
    MusashiGpuRead32 read32;
    MusashiGpuWrite32 write32;
    void *userdata;
    MusashiGpuRead16 read16;
    MusashiGpuWrite16 write16;
} MusashiGpuDevice;

/* Callback initialization uses interrupt/DMA registers, not GPU state or
 * cached RAM. This deliberately separate interface is the required platform
 * contract before 80042718 can advance past its first hardware writes and
 * 80042C90's observed 1F801114 control write. */
typedef uint16_t (*MusashiCallbackRead16)(void *userdata, uint32_t address);
typedef void (*MusashiCallbackWrite16)(void *userdata, uint32_t address,
                                       uint16_t value);
typedef void (*MusashiCallbackWrite32)(void *userdata, uint32_t address,
                                       uint32_t value);
/* B0:19 / HookEntryInt. The platform must register the supplied guest context
 * with its interrupt mechanism; returning zero refuses the operation. */
typedef int (*MusashiCallbackHookEntryInt)(void *userdata,
                                           uint32_t context_address);
struct MusashiCallbackRegistrationDevice;
/* A0:72 / _96_remove receives the observed guest callback-table address.
 * The provider owns BIOS-version-specific behavior and may refuse it. */
typedef int (*MusashiCallbackA0_72)(void *userdata, uint32_t table_address);
/* SYS(2) / ExitCriticalSection. The provider owns synchronization policy. */
typedef int (*MusashiCallbackExitCriticalSection)(void *userdata);
/* A0:49 / GPU_cw. The provider owns GPU synchronization, GP0 submission, and
 * the returned guest word; returning zero refuses the operation. */
typedef int (*MusashiCallbackGpuCw)(void *userdata, uint32_t command,
                                    int32_t *result);
typedef struct MusashiCallbackDevice {
    MusashiCallbackRead16 read16;
    MusashiCallbackWrite16 write16;
    MusashiCallbackWrite32 write32;
    MusashiCallbackHookEntryInt hook_entry_int;
    /* B0:19 can install a platform-owned scheduler while the checked MMIO,
     * callback-registration, and BIOS services remain owned by userdata. A
     * null value deliberately preserves the legacy userdata route. */
    void *hook_entry_int_userdata;
    /* Registration is game-owned code. Supply only its checked IRQ and BIOS
     * operations; callers cannot substitute a success-returning game handler. */
    const struct MusashiCallbackRegistrationDevice *registration_device;
    MusashiCallbackA0_72 a0_72;
    MusashiCallbackExitCriticalSection exit_critical_section;
    MusashiCallbackGpuCw gpu_cw;
    /* ResetGraph's 8005BD7C uses this separate, checked GPU/MMIO contract. */
    const MusashiGpuDevice *reset_device;
    void *userdata;
    /* Optional synchronous owner-thread IRQ checkpoint. The token identifies
     * the live local CPU continuation, not guest RAM or a register interface.
     * The runner retains pc/npc (including a pending branch delay transfer)
     * unchanged while this call executes; refusal stops the current run. */
    int (*checkpoint)(void *userdata, const void *continuation);
} MusashiCallbackDevice;

#endif
