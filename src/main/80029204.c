/* Main-exec range [80029204,80029218) from the SLUS executable.
 * SHA256(span)=268576082f66ca0a3e9ffb5ca1330d276c9b7d1041e1133e2a995534d0f7054b.
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
s32 func_80029204(s32 i) { return *(s32 *)((u8 *)D_800BA1B8 + i); }
