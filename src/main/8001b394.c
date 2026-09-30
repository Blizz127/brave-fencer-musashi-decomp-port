/* Exact retail word export for [8001B394,8001B3C4); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

void func_8001B3C4();                                  /* static */
extern s32 D_800C6D10;
extern s32 D_800C6D34;

s32 func_8001B394(s32 arg0) {
    D_800C6D34 = arg0;
    func_8001B3C4();
    return D_800C6D10;
}
