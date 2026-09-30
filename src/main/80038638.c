/* Exact retail word export for [80038638,80038668); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s32 D_800B9E92[];

/* The explicit index temp and address temp force evaluation order:
 * index, then base, then the arg1 offset. Folding them into one
 * expression lets the scheduler reorder the materialization. */
void func_80038638(s32 arg0, s32 arg1) {
    s32 i = arg0 * 127;
    u8 *p = (u8 *)&D_800B9E92[i];

    p[arg1] |= 1;
}
