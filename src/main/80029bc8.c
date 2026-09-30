/* Main-exec range [80029BC8,80029C44) from the SLUS executable.
 * SHA256(span)=3bd4a245c60803fea80d1d21de59d91df3e80bd9712bc960f175f91ba23f4c2d.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_80029CD4(s16);                             /* static */
s32 func_80029E30();                                /* static */

s32 func_80029BC8(s16 arg0, s16 arg1) {
    s32 temp_s2;

    temp_s2 = func_80029E30();
    return (temp_s2 * (arg0 + func_80029CD4(arg1))) / 100;
}
