#ifndef MUSASHI_IRQ_DISPATCH_H
#define MUSASHI_IRQ_DISPATCH_H
#include "musashi_boot_memory.h"
/* An explicit stop before 8005CED8 (BIOS ReturnFromException), NOT completion
 * of interrupt return or restored CPU state. Zero refuses an operation;
 * preceding guest/device effects survive and the call is not resumable. */
#define MUSASHI_IRQ_CONTEXT_RETURN_PENDING 2
#define MUSASHI_IRQ_CONTEXT_RETURN_DELEGATED 3
typedef struct MusashiIrqDispatchDevice {
    void *userdata;
    int (*read16)(void *, uint32_t, uint16_t *);
    int (*write16)(void *, uint32_t, uint16_t);
    MusashiGuestCallbackExecutor execute;
    /* Must execute bound 8005C604 with these guest words. No default success.
     * count is one or two; format is a guest address, not a host string. */
    int (*diagnostic)(void *, MusashiBootMemory *, uint32_t format,
        uint32_t first, uint32_t second, uint32_t count);
    /* Implements B0:17 against the platform's live exception frame. The
     * saved 48-byte game record is not an exception frame and must not be
     * substituted here. Return nonzero only after scheduling or performing
     * the real platform return; this API's delegated result is not a game
     * startup or menu-completion result. */
    int (*return_from_exception)(void *);
} MusashiIrqDispatchDevice;
/* Device and RAM access must be serialized. Device owns IRQ semantics and
 * may refuse reads to bound execution; an uncleared IRQ keeps the retail
 * rescan loop active, never turns into an assumed successful return. */
int musashi_boot_call_800427f4(MusashiBootMemory *, const MusashiIrqDispatchDevice *);
#endif
