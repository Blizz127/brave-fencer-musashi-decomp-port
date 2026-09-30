/* Main-exec range [8002A6BC,8002A6F0) from the SLUS executable.
 * SHA256(span)=bf3b0ce1470722101381e074510e6df53dd6917ac791a177b78dd4be28206b2c.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_8002A738();                                /* static */
s32 func_8002A748();                                /* static */

s32 func_8002A6BC(void) {
    s32 temp_s0;

    temp_s0 = func_8002A748();
    return func_8002A738() >= temp_s0;
}
