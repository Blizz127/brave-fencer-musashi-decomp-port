/* Exact retail word export for [80038668,80038698); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s32 D_800B9E92[];

/* Same explicit sequencing as func_80038638: index, base, offset. */
void func_80038668(s32 arg0, s32 arg1) {
    s32 i = arg0 * 127;
    u8 *p = (u8 *)&D_800B9E92[i];

    p[arg1] &= ~1;
}
