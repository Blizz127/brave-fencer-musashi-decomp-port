/* Exact retail [8001CF00,8001CF30).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001CF48();
typedef struct { s16 unk0, unk2; } T;
void func_8001CF00(T *p) {
    func_8001CF48(p);
    p->unk2 = 11;
}
