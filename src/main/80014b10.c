/* Exact retail word export [80014B10,80014BFC); verified against pinned EXE and asm. */
#include "psx_types.h"
/* Byte-exact controller direction recovery, GCC 2.7.2 / ASPSX 2.56.
 * IDs 0x53 and 0x73 admit axis conversion around midpoint 0x80. */
extern u8 D_80078DCA[];
extern u8 func_80014C28(s32);
extern u8 func_80014CF8(s32,s32);
u16 func_80014B10(s32 port) {
    u16 result, vertical, horizontal;
    s32 masked, kind;
    result = *(u16 *)(D_80078DCA + (port & 0xff) * 0x4c);
    if (result != 0) return result;
    kind = func_80014C28(port & 0xff);
    if (kind != 0x53) {
        if (kind < 0x54) return 0;
        if (kind != 0x73) return 0;
    }
    /* Empty constraint keeps the unmasked port live across the ID call.
     * It emits no instruction; all controller logic is C. */
    __asm__("" : "=r"(port) : "0"(port));
    masked = port & 0xff;
    vertical = func_80014CF8(masked,5);
    horizontal = func_80014CF8(masked,4);
    result = 0;
    if (vertical != 0x80) {
        result = 0x1000;
        if (vertical >= 0x80U) result = 0x4000;
    }
    if (horizontal != 0x80) {
        if (horizontal < 0x80U) result |= 0x8000;
        else result |= 0x2000;
    }
    return result;

}

