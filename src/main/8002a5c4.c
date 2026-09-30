/* Main-exec range [8002A5C4,8002A670) from the SLUS executable.
 * SHA256(span)=2a0dcb3a0aca9d95e0d224859961d9a565212d4587ef328778a800cce5e15fe0.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

s32 func_8002A6BC();                                /* static */
void func_8002A6F0();                                  /* static */
s32 func_8002A728();                                /* static */
s32 func_8002A748();                                /* static */
s32 func_8002AA00(s32);                             /* static */
extern s32 D_80078F00;

s32 func_8002A5C4(s32 arg0) {
    s32 temp_s0;

    temp_s0 = func_8002AA00(func_8002A728() + 1);
    if (func_8002A748() != 0) {
        if (temp_s0 >= 0) {
            D_80078F00 += arg0;
            if (func_8002A6BC() != 0) {
                if (temp_s0 != 0) {
                    D_80078F00 = func_8002A748();
                    return 0;
                }
                func_8002A6F0();
                return 1;
            }
            goto block_6;
        }
        /* Duplicate return node #7. Try simplifying control flow for better match */
        return 0;
    }
block_6:
    return 0;
}
