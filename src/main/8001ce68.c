/* Exact retail [8001CE68,8001CEC0).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001CF48();
typedef struct {
    s16 unk0, unk2; s32 unk4;
    u8 pad8[0x18]; s32 unk20;
    u8 pad24[3]; u8 unk27;
} T;
void func_8001CE68(T *p, void *a1) {
    func_8001CF48(p);
    p->unk2 = 8;
    p->unk27 = 0xBA;
    p->unk20 = (s32)a1;
    p->unk4 |= 0x400000;
}
