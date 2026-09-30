/* Main-exec range [8001F97C,8001F9F8) from the SLUS executable.
 * SHA256(span)=8bfd23aa731ffa0502c08c5970e558b25967e879e217f9eda11b797f7ba925e6.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_80020DA4();
void func_80020F34();
typedef struct {
    u8 pad[8]; s16 x, y, z;
    u8 padE[0x1E]; u16 flags;
    u8 pad2E[0x1A]; s32 u48, u4C, u50;
} T;
void func_8001F97C(T *p) {
    func_80020DA4((u8 *)p + 0x10, (u8 *)p + 0x34);
    if (p->flags & 0x10) {
        func_80020F34((u8 *)p + 0x34, (u8 *)p + 0x18);
    }
    p->u48 = p->x;
    p->u4C = p->y;
    p->u50 = p->z;
    p->flags |= 1;
}
