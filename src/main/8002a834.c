/* Main-exec range [8002A834,8002A8E0) from the SLUS executable.
 * SHA256(span)=b40f304969844aa05b8689139f614ca5c3a66da52def0d44a452d0be303e6b67.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

s32 func_8002A92C();                                /* static */
void func_8002A960();                                  /* static */
s32 func_8002A998();                                /* static */
s32 func_8002A9B8();                                /* static */
s32 func_8002AA00(s32);                             /* static */
extern s32 D_80078EF8;

s32 func_8002A834(s32 arg0) {
    s32 temp_s0;

    temp_s0 = func_8002AA00(func_8002A998() + 1);
    if (func_8002A9B8() != 0) {
        if (temp_s0 >= 0) {
            D_80078EF8 += arg0;
            if (func_8002A92C() != 0) {
                if (temp_s0 != 0) {
                    D_80078EF8 = func_8002A9B8();
                    return 0;
                }
                func_8002A960();
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
