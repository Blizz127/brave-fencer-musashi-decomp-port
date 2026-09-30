/* Exact retail word export for [8002850C,80028558); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

void func_80053AF8(s16, s16, s16);                     /* static */
extern s16 D_800AE7E0;
extern s16 D_800AE7E2;
extern s16 D_800AE7E4;

void func_8002850C(s16 arg0, s16 arg1, s16 arg2) {
    D_800AE7E0 = arg0;
    D_800AE7E2 = arg1;
    D_800AE7E4 = arg2;
    func_80053AF8(arg0, arg1, arg2);
}
