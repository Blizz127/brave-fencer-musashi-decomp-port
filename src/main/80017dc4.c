/* Main-exec range [80017DC4,80017DF8) from the SLUS executable.
 * SHA256(span)=e7378412d5d808bb29a1a54e0826765125e50baed639a95612cd888d9e3af5d5.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_80017DC4(s16 *src, s16 *dst) {
    dst[0] = src[0];
    dst[1] = 0;
    dst[2] = 0;
    dst[3] = 0;
    dst[4] = src[1];
    dst[5] = 0;
    dst[6] = 0;
    dst[7] = 0;
    dst[8] = src[2];
}
