/* Exact retail [8001C924,8001C97C).
 * Words from pinned SLUS_007.26 (live-guest identity).
 */
#include "psx_types.h"
void func_80053290();
typedef struct { u8 pad[0x24]; s32 *u24; } T;
void func_8001C924(T *p, s32 *list) {
    p->u24 = list;
    while (*list) {
        func_80053290(*list + 4);
        list++;
    }
}
