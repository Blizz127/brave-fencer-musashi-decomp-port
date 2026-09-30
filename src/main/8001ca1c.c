/* Exact retail [8001CA1C,8001CA88).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001C9D0();
void func_80052D90();
void func_80054514();
typedef struct {
    s16 unk0, unk2;
    u8 pad4[0x1C];
    s32 unk20;
} T;
void func_8001CA1C(T *p, s32 a1) {
    s32 tmp[8];
    func_8001C9D0(p);
    p->unk0 = 1;
    p->unk2 = 3;
    func_80052D90(0, (u8 *)p + 0x30);
    func_80054514((u8 *)p + 0x30, tmp);
    p->unk20 = a1;
}
