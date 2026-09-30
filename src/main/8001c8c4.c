/* Exact retail [8001C8C4,8001C924).
 * Words from pinned SLUS_007.26 (live-guest identity).
 */
#include "psx_types.h"
void func_8001C9D0();
void func_80052D90();
void func_80054514();
typedef struct {
    s16 unk0, unk2;
    u8 pad4[0x20];
    s32 unk24;
} T;
void func_8001C8C4(T *p) {
    s32 tmp[8];
    func_8001C9D0(p);
    p->unk0 = 1;
    p->unk2 = 5;
    func_80052D90(0, (u8 *)p + 0x30);
    func_80054514((u8 *)p + 0x30, tmp);
    p->unk24 = 0;
}
