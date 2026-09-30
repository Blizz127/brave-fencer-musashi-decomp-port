/* Exact retail [8001CDE8,8001CE28).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001CF48();
typedef struct {
    s16 unk0, unk2;
    u8 pad4[0x1C]; s32 unk20;
} T;
void func_8001CDE8(T *p, void *a1) {
    func_8001CF48(p);
    p->unk2 = 7;
    p->unk20 = (s32)a1;
}
