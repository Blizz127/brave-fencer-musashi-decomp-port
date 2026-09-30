/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_8002FA3C();
extern s16 D_800760EC;
extern void (*D_800A4F24)();
extern s16 D_800760E8;
s32 func_8002D8A8(void) {
    D_800760EC = 6;
    D_800A4F24 = func_8002FA3C;
    D_800760E8 = 0;
    return 0x10;
}
