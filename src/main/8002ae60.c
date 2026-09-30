/* Main-exec range [8002AE60,8002AED0) from the SLUS executable.
 * SHA256(span)=14d232792d553e22d93c22319b0627015778c0a3473222bb7126a75a30f6cbb7.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
s32 func_800291B4(s32);
s32 func_8002AE60(void) {
    s16 i = 0;
    s16 n = 0;
    do {
        if (func_800291B4(i + 0x63) & 0x40)
            n++;
        i++;
    } while (i < 0x2B);
    return n > 0x2A;
}
