/* Main-exec range [80012F74,80012FC8) from the SLUS executable.
 * SHA256(span)=6e1ee92d2e1ef9ed7da834cfb5d5430cd207faa5747f5e2d6e501452b70d21a4.
 * Word export only: the native seam executes the retail instruction
 * stream itself; this is not a C match claim. */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_80012FC8(s16, s16, s16, s16);              /* static */

s16 func_80012F74(s16 arg0, s16 arg1, s16 arg2, s16 arg3) {
    return (s16) (arg0 + func_80012FC8(arg0, arg1, arg2, arg3));
}
