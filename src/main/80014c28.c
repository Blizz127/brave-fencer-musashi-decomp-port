/* Exact retail word export for [80014C28,80014C54); EXE and assembly verified. */
/* Byte-exact C recovery, GCC 2.7.2 / ASPSX 2.56, -O2 -G0.
 * The 0x4c-byte record stride and byte offsets are observed accesses,
 * not a claim about the original record declaration. */
#include "psx_types.h"
extern u8 D_80078D98[];
u8 func_80014C28(s32 port) {
    return D_80078D98[(port & 0xff) * 0x4c];
}
