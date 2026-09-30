/* Exact retail word export [8001C2C4,8001C320); verified against pinned EXE. */
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
void func_8001C2C4(T *p) {
    s32 tmp[8];
    func_8001C9D0(p);
    p->unk0 = 1;
    p->unk2 = 0;
    func_80052D90(0, (u8 *)p + 0x30);
    func_80054514((u8 *)p + 0x30, tmp);
    p->unk24 = 0;
}
