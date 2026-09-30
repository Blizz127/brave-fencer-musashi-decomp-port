/* Main-exec range [800291F0,80029204) from the SLUS executable.
 * SHA256(span)=0374b03b6a10e52ecebe2fd14e02b41b0e7ef20c99c07f47b7e56baa4e9c5fc4.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
extern s32 D_800BA1B8[];
void func_800291F0(s32 i, s32 v) { *(s32 *)((u8 *)D_800BA1B8 + i) = v; }
