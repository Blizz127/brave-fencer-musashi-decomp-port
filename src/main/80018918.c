#include "psx_types.h"

/* Exact startup-record setup [80018918,800189A8). */

/* Observed record layout only. D_80078DA0 aliases offset eight in the
 * first record; native bindings must preserve that shared storage. */
typedef struct {
    u32 opaque00;
    u32 state04;
    u8 opaque08[0x44];
} StartupRecord;
extern StartupRecord D_80078D98[2];
extern u8 D_80078DA0[];
extern s32 D_800AE610;
extern void func_80016714(void *, s32);
extern void func_80018FC8(void);
extern s32 func_80042580(void);
extern void func_8005F0C8(void *, void *);
extern void func_8005D0F8(void);

void func_80018918(void) {
    u32 i = 0;
    u32 initial_state = 8;
    StartupRecord *record;
    D_800AE610 = 0;
    record = D_80078D98;
    do {
        func_80016714(record, 0x4C);
        record->state04 = initial_state;
        i++;
        record++;
    } while (i < 2);
    func_80018FC8();
    func_80042580();
    func_8005F0C8(D_80078DA0, D_80078DA0 + 0x4C);
    func_8005D0F8();
}
