#ifndef BFM_PLAT_DISC_CHECK_H
#define BFM_PLAT_DISC_CHECK_H

/* First-run disc check: the single implementation the platform and the
 * native lane both use.
 *
 * The port runs from the user's own US disc (SLUS-00726). Given the image
 * path from [disc] path / --disc, this opens it through the "image" storage
 * backend (.cue with its .bin files, raw .bin, .iso/.img), finds the boot
 * executable named by SYSTEM.CNF, and checks its SHA-256. On success the
 * disc stays open as the active disc; on failure nothing is left open.
 * Every failure has a user-facing message (bfm_plat_disc_status_message).
 *
 * CHD images are recognised but not supported yet (a BSD-3 reader, libchdr,
 * could sit behind the storage interface later); the message says how to
 * convert them. */

#include "bfm_plat_types.h"

#define BFM_RETAIL_BOOT_NAME "SLUS_007.26"
#define BFM_RETAIL_BOOT_SHA256 \
    "66371c3a7517e9eabd7cb6cf0c5abffe7296bd4bac29c85b8bf7bb9db349714a"
#define BFM_DISC_CHECK_MAX_FILES 8

typedef enum BfmPlatDiscStatus {
    BFM_DISC_OK = 0,
    BFM_DISC_NO_PATH,            /* nothing configured */
    BFM_DISC_UNREADABLE,         /* can't open / wrong size / bad sectors */
    BFM_DISC_MISSING_TRACKS,     /* a .cue names a file that isn't there */
    BFM_DISC_UNSUPPORTED_FORMAT, /* .chd, or an unknown extension */
    BFM_DISC_NOT_PS1,            /* no ISO9660 volume or no SYSTEM.CNF */
    BFM_DISC_WRONG_REGION,       /* Brave Fencer Musashi, not the US release */
    BFM_DISC_WRONG_DISC,         /* another PlayStation game */
    BFM_DISC_BAD_DUMP,           /* SLUS_007.26 present but its hash differs */
    BFM_DISC_STATUS_COUNT
} BfmPlatDiscStatus;

typedef struct BfmPlatDiscCheck {
    BfmPlatDiscStatus status;
    char boot_name[32];          /* from SYSTEM.CNF, e.g. "SLUS_007.26" */
    char boot_sha256[65];
    uint32_t boot_size;
    uint32_t track_count;
    uint32_t sector_size;
    char data_file[512];         /* resolved data track file */
    char detail[256];            /* the specific file/field that failed */
} BfmPlatDiscCheck;

/* Validates against the retail US boot executable. */
int bfm_plat_disc_validate(const char *path, BfmPlatDiscCheck *out);
/* Same, with an explicit expected boot name and SHA-256 (tests, and any
 * future supported release). Returns BFM_PLAT_OK only for BFM_DISC_OK. */
int bfm_plat_disc_validate_ex(const char *path, const char *boot_name,
                              const char *boot_sha256, BfmPlatDiscCheck *out);

const char *bfm_plat_disc_status_name(BfmPlatDiscStatus s);
/* One or two sentences telling the user what is wrong and what to do. */
const char *bfm_plat_disc_status_message(BfmPlatDiscStatus s);
/* status message + detail, into out. */
void bfm_plat_disc_check_describe(const BfmPlatDiscCheck *c, char *out,
                                  size_t size);

/* First run: looks for a single .cue/.iso/.bin/.chd in each ';'-separated
 * directory (in order; a .cue wins over a bare .bin in the same place).
 * Returns BFM_PLAT_OK with the path, or BFM_PLAT_NOT_FOUND. */
int bfm_plat_disc_autodetect(const char *dirs, char *out, size_t size);

#endif
