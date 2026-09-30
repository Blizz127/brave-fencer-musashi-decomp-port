/* Main-exec range [8002A92C,8002A960) from the SLUS executable.
 * SHA256(span)=9008f17ce1c1117b3b68d252badeccbec3e9dbef88a49037c7204facd98b6454.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_8002A9A8();                                /* static */
s32 func_8002A9B8();                                /* static */

s32 func_8002A92C(void) {
    s32 temp_s0;

    temp_s0 = func_8002A9B8();
    return func_8002A9A8() >= temp_s0;
}
