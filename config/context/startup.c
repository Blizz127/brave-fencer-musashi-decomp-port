/* Partial type context for m2c startup recovery; not compiled game code.
 * Preprocess with cc -E -P -I include before passing to m2c --context.
 *
 * Only widths and offsets observed in asm/main.s are represented. Names
 * retain offsets because gameplay meanings and the full object size remain
 * unresolved. Padding is unknown storage, not evidence of unused bytes.
 * Addresses inside the unknown storage must not become host-sized pointers.
 */
#include "psx_types.h"

typedef struct MusashiStartupDescriptor {
    u32 field00; /* sw at 80010390: 0x640 */
    u32 field04; /* sw at 800103B8: zero */
    u32 address08; /* sw at 80010364: PS1 address, not a host pointer */
} MusashiStartupDescriptor;

typedef struct MusashiStartupStateView {
    /* Index fieldA3D2 is assigned the boolean toggle at 800102A8..800102C4;
     * descriptor addressing multiplies that index by 12 at 80010320..328. */
    MusashiStartupDescriptor descriptors[2];
    u8 unknown0018[0x38 - 0x18];
    /* Opaque blocks: only bases, strides, and two-valued index use are
     * established at 800107A4..800107FC. Their internal layout is unknown. */
    u8 blocks0038[2][0x5C];
    u8 unknown00F0[0x14C - 0xF0];
    u8 blocks014C[2][0x14];
    u8 unknown0174[0x188 - 0x174];
    u16 field0188; /* lhu/sh at 8001076C..80010774 */
    u16 field018A; /* lhu/sh at 80010778..800107A0 */
    u8 unknown018C[0xA3A8 - 0x18C];
    u16 fieldA3A8; /* lhu/sh at 800103EC..80010404 */
    u8 unknownA3AA[2];
    u16 fieldA3AC; /* lhu/sh at 80010410..80010430 */
    u8 unknownA3AE[0xA3D2 - 0xA3AE];
    u16 fieldA3D2; /* sh/lhu at 800102C4..800102D0 */
    u16 fieldA3D4; /* lhu at 8001023C */
    u8 unknownA3D6[0xA3E3 - 0xA3D6];
    u8 fieldA3E3; /* lbu at 800103D4 */
    u8 unknownA3E4[4];
    u32 fieldA3E8; /* lw at 80010760; signedness unresolved */
} MusashiStartupStateView;

/* The retail prologue loads this object's address into $s1 at 800101A0. */
extern MusashiStartupStateView D_800AF630;

/* sw at 800101BC and lw at 8001085C; no function type is implied. */
extern s32 D_80074778;

/* 800189A8..80018A1C initializes its own loop state; incoming a0/a1 are
 * unused. Its calls assign a0 from s0/s1 in their delay slots. */
extern void func_800189A8(void);

/* Startup accesses these words at index * 20. These are overlapping views
 * of retail storage, not declarations of independently allocated objects:
 * A6528 is A651C+12, and AE7C8 is AE7BC+12. */
typedef struct MusashiStartupAddressStride20 {
    u32 address00;
    u8 unknown04[16];
} MusashiStartupAddressStride20;

extern MusashiStartupAddressStride20 D_800A651C[2];
extern MusashiStartupAddressStride20 D_800A6528[2];
extern MusashiStartupAddressStride20 D_800AE7BC[2];
extern MusashiStartupAddressStride20 D_800AE7C8[2];

/* Address arithmetic uses byte offsets; extents and ownership are unknown.
 * AA60C = A6610+0x3FFC, BA0E4 = BA0D8+12: preserve these aliases when
 * constructing runtime storage. See asm/main.s at 80010434..800105E0. */
extern u8 D_800A6610[];
extern u8 D_800AA60C[];
extern u8 D_800BA0D8[];
extern u8 D_800BA0E4[];
extern u8 D_8007BA70[];
extern u8 D_800A4F48[];
