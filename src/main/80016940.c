/* Main-exec range [80016940,80016978) from the SLUS executable.
 * SHA256(span)=d462aef0a340782469551ddfc7525f400b0aaf077cc68c1937dd3ff728b4210e.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"

void func_80016450(s32, s32);                            /* static */
extern s16 D_800AF7CE;

void func_80016940(s32 arg0) {
    D_800AF7CE = 0;
    func_80016450(-((u32) (arg0 & 0xFFFF) < 4U) & 0xFF, 0);
}
