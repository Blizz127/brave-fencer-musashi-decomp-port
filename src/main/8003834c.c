/* Exact retail word export for [8003834C,8003836C); EXE and assembly verified. */
#include "psx_types.h"

/* Decompiled by hand from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s32 D_800B9ECA;

void func_8003834C(s32 arg0, s32 arg1) {
    *(u16 *)((u8 *)&D_800B9ECA + ((arg0 * 127) * 4)) = (u16)arg1;
}
