/* Exact retail [8001CF30,8001CF48).
 * Words from pinned SLUS_007.26.
 */
#include "psx_types.h"

/* HAND MODEL from m2c draft of func_8001CF30 (main.s): NOT
 * verified against retail. C89-gated only; promotion requires an
 * oracle MATCH. Word RMW clearing bit 6, setting bit 12 (lw/sw). Body is m2c's verbatim with the
 * member typed. */

struct data_8001CF30 {
    u8 pad[0x4];
    s32 unk4; };

void func_8001CF30(struct data_8001CF30 *arg0) {
    arg0->unk4 = (s32) ((arg0->unk4 & ~0x40) | 0x1000);
}
