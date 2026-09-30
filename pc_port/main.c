#include <stdio.h>

#include "musashi_port.h"

#if MUSASHI_WITH_PSYCROSS
#include "musashi_psyq.h"
#endif

extern int func_80012AB0(int a, int b);
extern short func_80012E6C(int arg0, int arg1, short arg2, short arg3, short *arg4);
extern short func_8001311C(short arg0, short arg1, short arg2);

int main(void) {
    static const mus_u8 probe[4] = {0x12, 0x34, 0x56, 0x78};
    MusashiRuntimeState state;
    mus_u32 decoded = 0;

    musashi_port_init(&state);
    if (state.frame_counter != 0 || state.pad_state != 0) {
        fputs("musashi_pc_smoke: runtime reset failed\n", stderr);
        return 1;
    }
    if (!musashi_port_decode_probe(probe, &decoded) || decoded != 0x78563412u) {
        fputs("musashi_pc_smoke: little-endian probe failed\n", stderr);
        return 1;
    }
    if (func_80012AB0(7, 3) != 4) {
        fputs("musashi_pc_smoke: matched decomp function failed\n", stderr);
        return 1;
    }
    {
        short interpolation = 1;
        if (func_80012E6C(0x0FF0, 0x0010, 0, 0, &interpolation) != 32) {
            fputs("musashi_pc_smoke: matched angle function failed\n", stderr);
            return 1;
        }
    }
    if (func_8001311C(10, 20, 3) != 3 || func_8001311C(20, 10, 3) != -3 ||
        func_8001311C(10, 12, 5) != 2) {
        fputs("musashi_pc_smoke: matched step function failed\n", stderr);
        return 1;
    }

#if MUSASHI_WITH_PSYCROSS
    {
        MusashiPsyqProbe psyq;
        if (!musashi_psyq_probe(&psyq)) {
            fputs("musashi_pc_smoke: Psy-Q fixed-point probe failed\n", stderr);
            return 1;
        }
        printf("musashi_pc_smoke: psyq rcos(0)=%d rsin(0)=%d rcos(1/4)=%d rsin(1/4)=%d\n",
               psyq.cos_zero, psyq.sin_zero, psyq.cos_quarter, psyq.sin_quarter);
    }
#endif

    printf("musashi_pc_smoke: platform=%s frame=%u pad=%u le32=0x%08x\n",
           musashi_port_platform_name(),
           (unsigned)state.frame_counter,
           (unsigned)state.pad_state,
           (unsigned)decoded);
    return 0;
}
