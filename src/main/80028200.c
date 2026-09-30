/* Main-exec range [80028200,80028234) from the SLUS executable.
 * SHA256(span)=d0b5ad71e47ddd6c41549c2517bc4c89e864ea7d143c9f7c92effc911b6b3fe8.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

void func_80028200(s32 arg0, s32 arg1, s32 arg2, s32 *arg3, s32 arg4) {
    s32 *temp_a0;

    temp_a0 = arg0 + ((arg1 >> arg2) * 4);
    *arg3 = (*temp_a0 & 0xFFFFFF) | arg4;
    *temp_a0 = (s32) arg3 & 0xFFFFFF;
}
