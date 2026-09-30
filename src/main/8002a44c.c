/* Main-exec range [8002A44C,8002A480) from the SLUS executable.
 * SHA256(span)=a16c6c3df85473d0c99a6473d0e5ddfffd0f1a90e6502e373a7dd4620eb379e6.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_8002A4C8();                                /* static */
s32 func_8002A4D8();                                /* static */

s32 func_8002A44C(void) {
    s32 temp_s0;

    temp_s0 = func_8002A4D8();
    return func_8002A4C8() >= temp_s0;
}
