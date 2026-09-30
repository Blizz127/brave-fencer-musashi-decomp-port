/* Exact retail [8001CBDC,8001CC3C).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001CF48();
typedef struct {
    s16 unk0, unk2;
    u8 pad4[0x1C]; s32 unk20;
    u8 pad24[4]; s16 unk28, unk2A, unk2C;
} T;
void func_8001CBDC(T *p, void *a1, s16 a2, s16 a3) {
    func_8001CF48(p);
    p->unk2 = 0;
    p->unk28 = a2;
    p->unk2A = a3;
    p->unk20 = (s32)a1;
    p->unk2C = 0;
}
