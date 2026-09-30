/* Exact retail word export for [800167B8,800167F0); EXE and assembly verified. */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

void func_80016224(s32, s32);                            /* static */
extern s16 D_800AF7CE;

void func_800167B8(s32 arg0) {
    D_800AF7CE = 0;
    func_80016224(-((u32) (arg0 & 0xFFFF) < 4U) & 0xFF, 0);
}
