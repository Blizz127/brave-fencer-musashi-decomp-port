/* Main-exec range [8001C4A4,8001C558) from the SLUS executable.
 * SHA256(span)=cfff8a2c6df93db9b111a30e8bdf9875b106d78b6a37b126f472176610309fd2.
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
void func_80053290();
extern s32 D_80063548;
typedef struct {
    s16 unk0, unk2;
    u8 pad4[0x1C];
    s32 unk20;
    s32 unk24;
} T;
void func_8001C4A4(T *p, s32 *list) {
    s32 tmp[8];
    func_8001C9D0(p);
    p->unk0 = 1;
    p->unk2 = 7;
    func_80052D90(0, (u8 *)p + 0x30);
    func_80054514((u8 *)p + 0x30, tmp);
    p->unk24 = (s32)list;
    if (list) {
        p->unk20 = (s32)&D_80063548;
        while (*list) {
            func_80053290((void *)((*list & 0x80FFFFFF) + 4));
            list++;
        }
    }
}
