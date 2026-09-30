/* Main-exec range [800243EC,80024448) from the SLUS executable.
 * SHA256(span)=87c50f80a168332faefbaed9864ffe6a46781277e675322f82aa669a8ad7ab85.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
#include "psx_types.h"

/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

void func_80024448();                                  /* static */
void func_80026D64();                                  /* static */
extern s32 D_80078D88;
extern s32 D_800A2B78;

void func_800243EC(s32 *arg0) {
    s32 temp_v1;

    temp_v1 = *arg0;
    D_80078D88 = temp_v1;
    D_800A2B78 = (temp_v1 >> 0x1C) & 7;
    if (temp_v1 & 0x40) {
        func_80026D64();
        return;
    }
    func_80024448();
}
