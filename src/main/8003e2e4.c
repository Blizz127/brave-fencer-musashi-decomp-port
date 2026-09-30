/* Main-exec range [8003E2E4,8003E310) from the SLUS executable.
 * SHA256(span)=7fe1dab97d224ba0aae5650848d9f48d3432d250beb4b0baf46d1507bdf5fb4a.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

void func_8003E310(s16, s16);                          /* static */

void func_8003E2E4(s16 arg0, s16 arg1) {
    func_8003E310(arg0, arg1);
}
