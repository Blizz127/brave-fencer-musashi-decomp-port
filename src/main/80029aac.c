/* Main-exec range [80029AAC,80029AF4) from the SLUS executable.
 * SHA256(span)=dadd0e5ca9751af67326eabb7b725917612751c2f0005e449f43a5752cc51418.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_8002A2B0();                                /* static */

s32 func_80029AAC(void) {
    return (func_8002A2B0() * 0x4B) / 100;
}
