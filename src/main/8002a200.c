/* Main-exec range [8002A200,8002A234) from the SLUS executable.
 * SHA256(span)=61936742d22b5b8d6340806314d5d14c30743057de63ed94c16497b18ced35d1.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_8002A27C();                                /* static */
s32 func_8002A28C();                                /* static */

s32 func_8002A200(void) {
    s32 temp_s0;

    temp_s0 = func_8002A28C();
    return func_8002A27C() >= temp_s0;
}
