#ifndef BFM_PLAT_TYPES_H
#define BFM_PLAT_TYPES_H

/* Shared vocabulary for every bfm_plat_* interface.
 *
 * Game C calls bfm_plat_* functions only. It never includes a PsyCross
 * header, a host/OS header or a Sony Psy-Q header. Backends sit behind each
 * interface and are picked by name from the user config at startup.
 *
 * Return convention: BFM_PLAT_OK (0) on success, a negative BfmPlatResult on
 * refusal. (The older musashi_* device owners return 1 for success; the
 * backends that wrap them translate.) */

#include <stddef.h>
#include <stdint.h>

typedef enum BfmPlatResult {
    BFM_PLAT_PAUSED = 1,        /* bfm_plat_frame_begin: don't advance the game */
    BFM_PLAT_OK = 0,
    BFM_PLAT_ERROR = -1,
    BFM_PLAT_UNSUPPORTED = -2,
    BFM_PLAT_INVALID = -3,
    BFM_PLAT_NOT_FOUND = -4,
    BFM_PLAT_NO_SPACE = -5,
    BFM_PLAT_NOT_READY = -6
} BfmPlatResult;

/* A VRAM rectangle in PS1 VRAM pixel units (1024x512, 16-bit). */
typedef struct BfmPlatRect {
    int16_t x, y, w, h;
} BfmPlatRect;

const char *bfm_plat_result_name(int result);

#endif
