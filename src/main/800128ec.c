/* PSY-Q library routine [800128EC,800129CC) from the SLUS executable.
 * SHA256(span)=2f4c628518ed5b2a2141af810c448ca0c7745699ea15a5bed6e9ea2074c27159.
 * Word export only (no registry entry): the native seam needs the retail
 * instruction stream so the guest can execute the library routine it calls.
 * No C match claim yet.
 */
#include "psx_types.h"

typedef struct {
    s32 x;
    s32 y;
    s32 z;
    s32 pad;
} Vec32;

void func_80047E58(void *, s32);
s32 func_80049440(s32);

void func_800128EC(Vec32 *arg0, s32 arg1) {
    Vec32 buf;
    s32 min_clz;
    s32 clz;

    buf = *arg0;

    min_clz = func_80049440(buf.x);
    clz = func_80049440(buf.y);
    if (clz < min_clz) {
        min_clz = clz;
    }
    clz = func_80049440(buf.z);
    if (clz < min_clz) {
        min_clz = clz;
    }
    if (min_clz < 18) {
        min_clz = 18 - min_clz;
        buf.x >>= min_clz;
        buf.y >>= min_clz;
        buf.z >>= min_clz;
    }
    func_80047E58(&buf, arg1);
}
