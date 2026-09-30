/* Exact retail [8001C97C,8001C9D0).
 * Words from pinned SLUS_007.26 (live-guest identity).
 */
#include "psx_types.h"
void func_80053290();
void func_8001C97C(s32 *p) {
    while (*p) {
        func_80053290(*p + 4);
        p++;
    }
}
