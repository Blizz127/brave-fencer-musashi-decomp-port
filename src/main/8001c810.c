/* Exact retail [8001C810,8001C8C4).
 * Words from pinned SLUS_007.26 (live-guest identity).
 */
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
void func_8001C810(T *p, s32 *list) {
    s32 tmp[8];
    func_8001C9D0(p);
    p->unk0 = 1;
    p->unk2 = 5;
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
