/* Exact retail [8001CB00,8001CB6C).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001CF48();
typedef struct {
    s16 unk0, unk2; s32 unk4;
    u8 pad8[0x18]; s32 unk20;
    u8 pad24[4]; s16 unk28, unk2A, unk2C;
} T;
void func_8001CB00(T *p, void *a1, s16 a2, s16 a3) {
    func_8001CF48(p);
    p->unk2 = 1;
    p->unk28 = a2;
    p->unk2A = a3;
    p->unk20 = (s32)a1;
    p->unk4 = 0x8000000;
    p->unk2C = 0;
}
