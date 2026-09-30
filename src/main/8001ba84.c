/* Main-exec range [8001BA84,8001BADC) from the SLUS executable.
 * SHA256(span)=0c682049980248e06eba0f5a380aada6c43ab058e5d4a7d951d553669d9e5c5d.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_8001BC6C();
extern s32 D_800630E8[];
void func_8001BA84(void) {
    func_8001BC6C(D_800630E8, 0x80);
    func_8001BC6C(D_800630E8 + 2, 0x80);
    func_8001BC6C(D_800630E8 + 4, 0x80);
    func_8001BC6C(D_800630E8 + 6, 0x80);
}
