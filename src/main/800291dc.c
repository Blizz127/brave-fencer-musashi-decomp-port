/* Main-exec range [800291DC,800291F0) from the SLUS executable.
 * SHA256(span)=0237b72e11354e3942a428ecb423458f3d580d4cd1714084e5df8c4112e2a6e0.
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
s16 func_800291DC(s32 i) { return *(s16 *)((u8 *)D_800BA1B8 + i); }
