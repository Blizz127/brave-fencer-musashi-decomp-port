/* Main-exec range [8001C558,8001C5B8) from the SLUS executable.
 * SHA256(span)=2eac06896c83c48400c9902d0f29ee71e126ae479cf56371ddaa3d49ac7a3b97.
 * Word export for the native seam; the body below is kept
 * byte-identical (wrap only, no rewrite). */
/* Decompiled by m2c from main.s, then verified byte-exact
 * against retail by tools/match_function.py. Types and signatures are
 * whatever reproduces the bytes; they are not evidence of the
 * original declaration. */

#include "psx_types.h"
void func_8001C9D0();
void func_80052D90();
void func_80054514();
typedef struct {
    s16 unk0, unk2;
    u8 pad4[0x20];
    s32 unk24;
} T;
void func_8001C558(T *p) {
    s32 tmp[8];
    func_8001C9D0(p);
    p->unk0 = 1;
    p->unk2 = 7;
    func_80052D90(0, (u8 *)p + 0x30);
    func_80054514((u8 *)p + 0x30, tmp);
    p->unk24 = 0;
}
