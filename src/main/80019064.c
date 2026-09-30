/* Main-exec range [80019064,800190AC) from the SLUS executable.
 * SHA256(span)=75b4dc5b2164896cbac2bfa1b299e9f431796e4fec570557615e25e9e1055622.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
extern s32 D_800747C8;
extern u8 D_800B9A64;
extern s32 D_800747C0;
extern u8 D_800747C4;
extern u8 D_800747C5;
void func_80019064(s32 a0) {
    if (D_800747C8 == 0 && D_800B9A64 == 0) {
        D_800747C0 = a0;
        D_800747C4 = 0xFF;
        D_800747C5 = 0;
    }
}
