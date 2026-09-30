/* Main-exec range [80029B4C,80029BC8) from the SLUS executable.
 * SHA256(span)=e4299a6ff414b73240dad0c4db3629c26de099e0b475ed283a8d111a7c34d584.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_80029CD4(s16);                             /* static */
s32 func_80029DB4();                                /* static */

s32 func_80029B4C(s16 arg0, s16 arg1) {
    s32 temp_s2;

    temp_s2 = func_80029DB4();
    return (temp_s2 * (arg0 + func_80029CD4(arg1))) / 100;
}
