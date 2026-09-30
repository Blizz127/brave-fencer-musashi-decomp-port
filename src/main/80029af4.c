/* Main-exec range [80029AF4,80029B4C) from the SLUS executable.
 * SHA256(span)=161e62f091de7bef562cd286ad5af0c69a2edc98b872383e7fa95d358e4aeeb1.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_80029CD4();                               /* static */
s32 func_80029E30();                                /* static */

s32 func_80029AF4(void) {
    s32 temp_s0;

    temp_s0 = func_80029E30();
    return (temp_s0 * (func_80029CD4(0x28) + 0xC8)) / 100;
}
