/* Main-exec range [80014E80,80014ED4) from the SLUS executable.
 * SHA256(span)=b2bb4f672d46435a95e9eb00b46cd5d3e397ca9049479b4fef59b44f26bba06a.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_80014ED4();                               /* static */
s32 func_80015018();                               /* static */

s32 func_80014E80(s32 arg0, s32 arg1) {
    s32 var_v0;

    if ((arg0 & 0xFF) == 1) {
        var_v0 = func_80015018(0) & 0xFFFF;
    } else {
        var_v0 = func_80014ED4(0) & 0xFFFF;
    }
    return (var_v0 & arg1) != 0;
}
