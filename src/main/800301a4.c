/* Exact retail word export for [800301A4,800301C8); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern u8 D_8006AEF4;
extern s16 D_800A46CC;

s32 func_800301A4(void) {
    D_800A46CC = 0;
    D_8006AEF4 &= 0xFD;
    return 1;
}
