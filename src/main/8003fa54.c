/* Main-exec range [8003FA54,8003FA64) from the SLUS executable.
 * SHA256(span)=a045b6b114208032fa1b9f3cdc5958b1694e0895262d210dcc7a0618412086b5.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s16 D_800A5F60;

void func_8003FA54(void) {
    D_800A5F60 = 0;
}
