/* Main-exec range [800130D0,8001311C) from the SLUS executable.
 * SHA256(span)=fa9c5ce6a7e22581a31b3dcd1ef6b081040f8426ed3c4f90ae7c3dc837eb873f.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_8001311C(s16, s16, s16);                   /* static */

s16 func_800130D0(s16 arg0, s16 arg1, s16 arg2) {
    return (s16) (arg0 + func_8001311C(arg0, arg1, arg2));
}
