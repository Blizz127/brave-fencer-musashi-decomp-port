/* Main-exec range [80029E30,80029E94) from the SLUS executable.
 * SHA256(span)=18aae42939a5ddfc7c7bf080004bd72baef895b395294a4f16c0c9709dbda70c.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_8002A2B0();                                /* static */
s32 func_8002A76C();                                /* static */

s32 func_80029E30(void) {
    s32 temp_s0;

    temp_s0 = func_8002A76C();
    return ((temp_s0 + func_8002A2B0()) * 0xAF) / 100;
}
