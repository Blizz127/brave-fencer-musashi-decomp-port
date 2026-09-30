/* libetc InterruptCallback registration (guest 800429DC), port-owned HLE.
 *
 * Behaviour (PS1 interrupt controller and BIOS, psx-spx):
 * the guest keeps one callback word per IRQ line at 8006BB00, a halfword
 * "service installed" guard at 8006BAFC, the enabled-line halfword at
 * 8006BB2C, and a pointer to I_MASK at 8006CB8C. Registering a callback that
 * differs from the installed one, once the service is installed:
 *   1. samples I_MASK and writes 0 (no IRQ while the tables change);
 *   2. publishes the callback and sets (or, for 0, clears) the line's bit in
 *      the sampled mask and in the enabled-line halfword;
 *   3. hands the BIOS its automatic acknowledge for lines the game now
 *      services itself: VBlank (line 0) -> ChangeClearPad (B0:5B) and
 *      ChangeClearRCnt(3) (C0:0A); root counters 0-2 (lines 4-6) ->
 *      ChangeClearRCnt(0-2); the flag is "callback removed";
 *   4. restores I_MASK with the updated mask, re-reading its pointer.
 * The previous callback is returned. An unchanged callback, or a service not
 * yet installed, returns the previous word with no side effect.
 *
 * Failure contract (musashi_callback_registration.h): any refused access
 * returns 0 with the preceding effects kept; the result is written only on
 * success. */
#include "musashi_callback_registration.h"

enum {
    IRQ_CALLBACKS = 0x8006bb00u,
    IRQ_SERVICE_GUARD = 0x8006bafcu,
    IRQ_ENABLED_LINES = 0x8006bb2cu,
    I_MASK_POINTER = 0x8006cb8cu,
};
enum { LINE_VBLANK = 0, LINE_TIMER0 = 4, LINE_TIMER2 = 6 };

static int enabled_lines(MusashiBootMemory *memory, uint16_t set, uint16_t keep) {
    uint16_t lines;
    return musashi_boot_read16(memory, IRQ_ENABLED_LINES, &lines) &&
        musashi_boot_write16(memory, IRQ_ENABLED_LINES, (uint16_t)((lines | set) & keep));
}

int musashi_boot_call_800429dc(MusashiBootMemory *memory,
    const MusashiCallbackRegistrationDevice *device, int32_t index,
    uint32_t callback, uint32_t *result) {
    uint32_t slot, previous, mask_address, line_bit;
    uint16_t guard, sampled;
    uint32_t mask;
    int removed = callback == 0;
    if (!memory || !result) return 0;
    slot = (uint32_t)index * 4u + IRQ_CALLBACKS;
    if (!musashi_boot_read32(memory, slot, &previous)) return 0;
    if (callback == previous ||
        (musashi_boot_read16(memory, IRQ_SERVICE_GUARD, &guard) && guard == 0)) {
        *result = previous;
        return 1;
    }
    if (!device || !device->read16 || !device->write16 ||
        !musashi_boot_read32(memory, I_MASK_POINTER, &mask_address) || (mask_address & 1u) ||
        !device->read16(device->userdata, mask_address, &sampled) ||
        !device->write16(device->userdata, mask_address, 0))
        return 0;
    line_bit = 1u << ((uint32_t)index & 31u);
    mask = sampled;
    if (!musashi_boot_write32(memory, slot, callback)) return 0;
    if (!removed) {
        mask |= line_bit;
        if (!enabled_lines(memory, (uint16_t)line_bit, 0xffffu)) return 0;
    } else {
        mask &= ~line_bit;
        if (!enabled_lines(memory, 0, (uint16_t)~line_bit)) return 0;
    }
    if (index == LINE_VBLANK) {
        if (!device->b0_5b || !device->b0_5b(device->userdata, removed) ||
            !device->c0_0a || !device->c0_0a(device->userdata, 3, removed))
            return 0;
    } else if (index >= LINE_TIMER0 && index <= LINE_TIMER2) {
        if (!device->c0_0a || !device->c0_0a(device->userdata, index - LINE_TIMER0, removed))
            return 0;
    }
    if (!musashi_boot_read32(memory, I_MASK_POINTER, &mask_address) || (mask_address & 1u) ||
        !device->write16(device->userdata, mask_address, (uint16_t)mask))
        return 0;
    *result = previous;
    return 1;
}
