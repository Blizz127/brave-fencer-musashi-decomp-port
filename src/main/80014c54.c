/* Main-exec range [80014C54,80014CAC) from the SLUS executable.
 * SHA256(span)=c6b74f128b6465ebbb78decc1c017bd38f8d095b358db3b2cf9799f7b40b10f3.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
extern u8 D_80078D98[];
s32 func_80014C54(u32 a0, u32 a1, u32 a2) {
    u8 *p = D_80078D98 + (a0 & 0xFF) * 0x4C;
    u16 v;
    if ((a1 & 0xFF) == 1) {
        v = *(u16 *)(p + 0x32);
    } else {
        v = *(u16 *)(p + 0x3A);
    }
    return (v & a2) == a2;
}
