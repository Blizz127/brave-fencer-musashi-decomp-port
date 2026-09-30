/* Main-exec range [800285CC,80028620) from the SLUS executable.
 * SHA256(span)=f5a780d6552614791e7db1d7155dc62169288216ca40c150046ddd6d870f682e.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_80053978();
void func_800538EC();
typedef struct { s16 a; u8 pad2[2]; s32 b; u8 c; u8 pad9; u8 d; } S;
extern S D_800C6D18;
void func_800285CC(void) {
    D_800C6D18.a = -0x106;
    D_800C6D18.b = 0x1F40000;
    D_800C6D18.c = 0;
    D_800C6D18.d = 0;
    func_80053978(&D_800C6D18);
    func_800538EC(1);
}
