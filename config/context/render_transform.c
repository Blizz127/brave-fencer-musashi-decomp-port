/* Recovery-only storage views, not original declarations or matching code.
 * 80020DA4 writes 18 bytes; 80020248 reads its columns as halfwords.
 * Separate transform arguments have a 0x20-byte stride, rotation control
 * words at 0..0x10, and translation words at 0x14, 0x18, 0x1C.
 * Preprocess with the repo include path before passing to m2c. */
#include "psx_types.h"

typedef struct MusashiRotationView {
    s16 m[3][3];
} MusashiRotationView;

typedef struct MusashiTransformView {
    MusashiRotationView rotation;
    /* Included in the fifth word read; value/initialization unresolved.
     * Never silently zero or read this field from an 18-byte rotation. */
    u16 unknown12;
    s32 translation[3];
} MusashiTransformView;
