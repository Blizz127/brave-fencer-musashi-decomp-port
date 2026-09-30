/* Exact retail [8001CA88,8001CB00).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001C9D0();
void func_80052D90();
void func_80054514();
typedef struct {
    s16 unk0, unk2;
    u8 pad4[10];
    s16 unkE;
    u8 pad10[14];
    s16 unk1E;
    s32 unk20;
    u8 pad24[0x50];
    s32 unk74;
} T;
void func_8001CA88(T *p, s32 a1) {
    s32 tmp[8];
    func_8001C9D0(p);
    p->unk0 = 1;
    p->unk2 = 4;
    func_80052D90(0, (u8 *)p + 0x30);
    func_80054514((u8 *)p + 0x30, tmp);
    p->unk20 = a1;
    p->unk74 = 0;
    p->unkE = 0;
    p->unk1E = 0;
}
