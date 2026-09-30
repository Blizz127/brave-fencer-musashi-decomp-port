/* Exact retail word export [80018714,80018730); verified against pinned EXE and asm. */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s32 D_800747AC;
extern s32 D_800C7D24;

void func_80018714(s32 arg0) {
    D_800C7D24 = 1;
    D_800747AC = arg0;
}

