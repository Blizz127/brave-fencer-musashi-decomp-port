/* Main-exec range [8002A354,8002A400) from the SLUS executable.
 * SHA256(span)=d100b07557717015d19581bf5d09c3e9a033acc2416d5ca47e5a92175ee0d64d.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

s32 func_8002A44C();                                /* static */
void func_8002A480();                                  /* static */
s32 func_8002A4B8();                                /* static */
s32 func_8002A4D8();                                /* static */
s32 func_8002AA00(s32);                             /* static */
extern s32 D_80078EF0;

s32 func_8002A354(s32 arg0) {
    s32 temp_s0;

    temp_s0 = func_8002AA00(func_8002A4B8() + 1);
    if (func_8002A4D8() != 0) {
        if (temp_s0 >= 0) {
            D_80078EF0 += arg0;
            if (func_8002A44C() != 0) {
                if (temp_s0 != 0) {
                    D_80078EF0 = func_8002A4D8();
                    return 0;
                }
                func_8002A480();
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
