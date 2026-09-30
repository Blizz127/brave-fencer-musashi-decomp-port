/* Exact retail [8001D0F4,8001D130).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"
void func_8001D0F4(s32 *p) {
    while (*p) {
        *p = *p & 0x80FFFFFF;
        p++;
    }
}
