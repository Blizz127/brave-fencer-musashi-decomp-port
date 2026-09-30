/* Main-exec range [8002D80C,8002D834) from the SLUS executable.
 * SHA256(span)=51b3e7f01f639a4f62661efeb788e54b82ec07007e7c7577ce2acf008a03b306.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

extern u16 D_8006A990;
extern u16 D_800A4E8E;

u16 func_8002D80C(void) {
    if (D_800A4E8E & 1) {
        return D_8006A990;
    }
    return 0;
}
