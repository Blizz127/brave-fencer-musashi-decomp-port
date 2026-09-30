/* Main-exec range [800361CC,8003621C) from the SLUS executable.
 * SHA256(span)=ba615a730ef303ae7bb4b638db4d9c46d7dc3224337e49296f19f380b24541b3.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

extern s32 D_8006AEF0;
s32 func_800435B4();                                /* static */
extern u8 D_80076110;

void func_800361CC(void) {
    if (D_80076110 == 0) {
        D_8006AEF0 = func_800435B4();
    } else {
        func_800435B4();
    }
    D_80076110 = 1;
}
