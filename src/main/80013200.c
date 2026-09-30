/* Main-exec range [80013200,80013228) from the SLUS executable.
 * SHA256(span)=d6c824f556dbccf390fbc360349e2a17484b45a4353ef7ee44e69389f92fa93f.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_80013228();                                /* static */
void func_80047D3C(s32);                               /* static */

void func_80013200(void) {
    func_80047D3C(func_80013228());
}
