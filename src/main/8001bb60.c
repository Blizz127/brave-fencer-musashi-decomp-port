/* Main-exec range [8001BB60,8001BBBC) from the SLUS executable.
 * SHA256(span)=4bc3765bd7c5f39f3cbe3ceb3a260b7d3e39bcc84607d11b0f898aa8f2b7f1ee.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

void func_8001BBBC(s32, s32, s32);                         /* static */
extern s32 D_80074808;
extern s32 D_8007480C;
extern s32 D_800A5E60;

void func_8001BB60(void) {
    s32 var_s0;

    var_s0 = 0;
    D_80074808 = D_800A5E60;
    do {
        func_8001BBBC(0, var_s0 * 8, 0);
        var_s0 += 1;
    } while (var_s0 < 0x1E);
    D_8007480C = 0;
}
