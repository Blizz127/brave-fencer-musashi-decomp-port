/* Exact retail word export for [80014D30,80014D68); EXE and assembly verified. */
/* Byte-exact C recovery, GCC 2.7.2 / ASPSX 2.56, -O2 -G0.
 * The 0x4c-byte record stride and byte offsets are observed accesses,
 * not a claim about the original record declaration. */
#include "psx_types.h"
extern u8 D_80078D98[];
u8 func_80014D30(s32 port, s32 axis) {
    u8 *record = D_80078D98 + (port & 0xff) * 0x4c;
    /* Empty register constraint preserves the retail address evaluation order;
     * it emits no instructions. The load and indexing are recovered C. */
    __asm__("" : "=r"(record), "=r"(axis) : "0"(record), "1"(axis));
    return (record + (axis & 0xff))[0x42];
}
