/* Main-exec range [80014004,80014028) from the SLUS executable.
 * SHA256(span)=fb828220417a659f2cd631628a214b6068dc1fbb601b4a47c42d84b9d0feec95.
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
void func_80014004(s16 *dst, s16 *src) {
    dst[0] = src[0];
    dst[3] = src[1];
    dst[6] = src[2];
}
