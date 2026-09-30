/* Main-exec range [80013028,800130D0) from the SLUS executable.
 * SHA256(span)=9af503680e158f1e94465c07e72dbaa739d1fa89d3ec347f0c6b3b5fd9c14c34.
 * Word export for the native seam; the body below is an
 * UNVERIFIED draft, not an oracle match claim. */
#include "psx_types.h"

s32 func_80013028(s32 arg0, s32 arg1, s16 arg2, s16 arg3, s16 *arg4) {
    s16 diff;
    s32 ret;

    diff = arg1 - arg0;
    if (*arg4 == 0 || arg2 == 0) {
        return diff;
    }
    ret = (s16)((diff * arg3) / arg2);
    if (ret == 0) {
        ret = -1;
        if (diff > 0) {
            ret = 1;
        }
    }
    return ret;
}
