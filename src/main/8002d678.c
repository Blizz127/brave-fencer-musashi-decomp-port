/* Main-exec range [8002D678,8002D6D8) from the SLUS executable.
 * SHA256(span)=aab91bd03fe0194dbfb6a170052a0496e3c1d5796ae81257e7513f1492951df2.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

s32 func_8002DC68(u32, s32);                        /* static */
extern s8 D_800A4F17;

s32 func_8002D678(s32 arg0, s32 arg1) {
    s32 var_v1;
    u32 temp_s0;

    temp_s0 = arg0 & 0xFFFF;
    D_800A4F17 = 1;
    if (temp_s0 >= 0x400U) {
        var_v1 = func_8002DC68(temp_s0, arg1 & 0xFFFF);
        if (var_v1 != 0) {
            var_v1 |= temp_s0 << 0x10;
        }
        return var_v1;
    }
    return 0;
}
