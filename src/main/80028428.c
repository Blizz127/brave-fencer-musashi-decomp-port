/* Main-exec range [80028428,80028488) from the SLUS executable.
 * SHA256(span)=cb9e13de84bbdf0b6141da1aa4fee853eef289383e6c3f3933958aab007b75f0.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_80028620();
typedef struct { u8 pad[0xC]; u8 a, b, c; u8 padF; } S;
extern S D_800A5E88[];
void func_80028428(u16 idx, u8 a1, u8 a2, u8 a3) {
    if (idx < 3) {
        D_800A5E88[idx].a = a1;
        D_800A5E88[idx].b = a2;
        D_800A5E88[idx].c = a3;
        func_80028620(idx, &D_800A5E88[idx]);
    }
}
