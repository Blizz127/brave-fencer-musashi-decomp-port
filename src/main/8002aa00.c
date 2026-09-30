/* Main-exec range [8002AA00,8002AA3C) from the SLUS executable.
 * SHA256(span)=ba2068e331ad9072bad7f4b4d35559dde16d0f936f757b4b8c21e8d900bceeb5.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
s32 func_8002AA3C();
s32 func_8002AA00(s32 a0) {
    s32 v = func_8002AA3C(a0);
    if (v) {
        return (a0 < v) ^ 1;
    }
    return -1;
}
