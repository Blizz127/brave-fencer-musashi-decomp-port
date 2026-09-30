#include "psx_types.h"


/* Partial view of the same 76-byte records used by startup. Volatile byte
 * fields reproduce retail stores; this does not establish original types. */
typedef struct {
    u8 opaque00[0x4A];
    volatile u8 flag4A, flag4B;
} RecordResetView;
extern RecordResetView D_80078D98[2];
extern u8 D_800747B9, D_800747B8, D_80062BBC, D_80062BBD;

void func_80018FC8(void) {
    s32 i = 0;
    RecordResetView *record = D_80078D98;
    D_800747B9 = 0;
    D_800747B8 = 0;
    D_80062BBC = 0x40;
    D_80062BBD = 0;
    for (; i < 2; i++) {
        record->flag4B = 0;
        record->flag4A = 0;
        record++;
    }
}
