/* Main-exec range [80015AD0,80015B6C) from the SLUS executable.
 * SHA256(span)=cafc3894a4e08fead1abb9076dcf6254c8f37f277517d47942ae1f58bfe2c773.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

s32 func_80047BD8(s32, s32);                        /* static */

s32 func_80015AD0(s16 arg0, s16 arg1) {
    s16 var_s0;
    s32 temp_a1;
    s32 var_v0;

    var_s0 = arg1;
    temp_a1 = arg1 << 0x10;
    if (temp_a1 == 0) {
        var_s0 = 1;
    }
    var_v0 = func_80047BD8((s32) (arg0 << 0xC) / var_s0, temp_a1);
    if (var_s0 < 0) {
        if (arg0 >= 0) {
            return var_v0 + 0x800;
        }
        var_v0 -= 0x800;
        /* Duplicate return node #6. Try simplifying control flow for better match */
        return var_v0;
    }
    return var_v0;
}
