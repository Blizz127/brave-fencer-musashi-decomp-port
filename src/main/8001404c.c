/* Main-exec range [8001404C,80014070) from the SLUS executable.
 * SHA256(span)=5fd6ddf191da4ab2db2d5836e3f002b24c6dc173d7a27c905cc02841a5d137d3.
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
void func_8001404C(s16 *dst, s16 *src) {
    dst[2] = src[0];
    dst[5] = src[1];
    dst[8] = src[2];
}
