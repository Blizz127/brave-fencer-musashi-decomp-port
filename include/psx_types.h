#ifndef PSX_TYPES_H
#define PSX_TYPES_H

/* Freestanding integer types for decompiled sources.
 *
 * src/shared/types.h cannot be used here: it includes <stdint.h>, and the
 * Psy-Q chain runs cpp with -nostdinc so no hosted headers are reachable.
 * These widths are fixed by the PS1's 32-bit MIPS ABI.
 */

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;

#endif
