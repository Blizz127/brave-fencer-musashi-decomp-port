/* Exact retail word export [8001BFB0,8001BFD0); verified against pinned EXE and asm. */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s32 D_80063074;

void func_8001BFB0(void) {
    D_80063074 = (s32) (D_80063074 + 1);
}

