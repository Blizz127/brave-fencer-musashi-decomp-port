/* Main-exec range [800291C8,800291DC) from the SLUS executable.
 * SHA256(span)=aabe98358cd762b01b3e930d1d3de5b00b6370609f93c49eb0673e4f3197695a.
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
extern s16 D_800BA1B8[];
void func_800291C8(s32 i, s16 v) { *(s16 *)((u8 *)D_800BA1B8 + i) = v; }
