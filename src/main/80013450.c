/* Main-exec range [80013450,80013478) from the SLUS executable.
 * SHA256(span)=5164de0984ffa12d5033ebb448a89c7a1a1d384a8508d209a038f0b6c27a3f5d.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_80013478();                                /* static */
void func_80047D3C(s32);                               /* static */

void func_80013450(void) {
    func_80047D3C(func_80013478());
}
