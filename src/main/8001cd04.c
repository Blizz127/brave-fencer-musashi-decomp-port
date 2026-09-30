/* Exact retail [8001CD04,8001CD50).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001CF48();
typedef struct {
    s16 unk0, unk2;
    u8 pad4[0xA]; s16 unkE;
    u8 pad10[0xE]; s16 unk1E;
    s32 unk20;
    u8 pad24[8]; s16 unk2C;
} T;
void func_8001CD04(T *p, void *a1) {
    func_8001CF48(p);
    p->unk2 = 4;
    p->unk20 = (s32)a1;
    p->unkE = 0;
    p->unk1E = 0;
    p->unk2C = 0;
}
