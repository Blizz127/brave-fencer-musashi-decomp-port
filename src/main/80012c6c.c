/* Complete retail export [80012C6C,80012CB8). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_80012CB8(s16, s16, s16);                   /* static */

s16 func_80012C6C(s16 arg0, s16 arg1, s16 arg2) {
    return (s16) (arg0 + func_80012CB8(arg0, arg1, arg2));
}
