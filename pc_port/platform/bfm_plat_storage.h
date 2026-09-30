#ifndef BFM_PLAT_STORAGE_H
#define BFM_PLAT_STORAGE_H

/* Disc and save storage.
 *
 * The game is read from the user's own disc image at runtime; nothing from
 * the disc is committed or shipped. Retail code that called CdRead/CdSearchFile
 * reaches bfm_plat_disc_read / bfm_plat_disc_find_file instead.
 *
 * LBA convention: LBA 0 is MSF 00:02:00 (the first sector of track 1 in the
 * image file), as libcd uses. */

#include "bfm_plat_types.h"

#define BFM_DISC_RAW_SIZE 2352u
#define BFM_DISC_DATA_SIZE 2048u
#define BFM_MEMCARD_SIZE (128u * 1024u)
#define BFM_MEMCARD_SLOTS 2

typedef enum BfmPlatDiscReadMode {
    BFM_DISC_READ_DATA = 0,    /* 2048 bytes, Mode 2 Form 1 user data */
    BFM_DISC_READ_FORM2 = 1,   /* 2324 bytes, Mode 2 Form 2 (XA/STR) */
    BFM_DISC_READ_NO_SYNC = 2, /* 2340 bytes: header+subheader+data+EDC */
    BFM_DISC_READ_RAW = 3      /* 2352 bytes */
} BfmPlatDiscReadMode;

typedef struct BfmPlatDiscInfo {
    uint32_t sector_count;
    uint32_t sector_size;      /* 2352 raw image, 2048 cooked .iso */
    uint32_t track_count;
} BfmPlatDiscInfo;

typedef struct BfmPlatStorageBackend {
    const char *name;
    int (*open)(void *self, const char *path);
    void (*close)(void *self);
    int (*info)(void *self, BfmPlatDiscInfo *out);
    /* One sector into out (size bytes, size from the read mode). */
    int (*read_sector)(void *self, uint32_t lba, BfmPlatDiscReadMode mode,
                       void *out, size_t size);
    void *self;
} BfmPlatStorageBackend;

int bfm_plat_storage_register(const BfmPlatStorageBackend *backend);
/* Opens the disc with the named backend. No fallback: a missing disc is a
 * user-visible error. */
int bfm_plat_disc_open(const char *backend, const char *path);
void bfm_plat_disc_close(void);
const char *bfm_plat_disc_backend(void);
int bfm_plat_disc_info(BfmPlatDiscInfo *out);

size_t bfm_plat_disc_mode_size(BfmPlatDiscReadMode mode);
int bfm_plat_disc_read(uint32_t lba, uint32_t count, BfmPlatDiscReadMode mode,
                       void *out, size_t out_size);

uint32_t bfm_plat_disc_msf_to_lba(uint8_t m, uint8_t s, uint8_t f);
void bfm_plat_disc_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f);

/* ISO9660 lookup ("\\DATA\\FILE.BIN;1" or "DATA/FILE.BIN"; case-insensitive,
 * version suffix optional). */
int bfm_plat_disc_find_file(const char *path, uint32_t *lba, uint32_t *size);

/* Reads a whole file into out; goes through the mods "files" replacement
 * hook keyed by the hash of the original data. *size gets the byte count
 * (replacement size when replaced; BFM_PLAT_NO_SPACE if it doesn't fit). */
int bfm_plat_disc_read_file(const char *path, void *out, size_t cap,
                            size_t *size);

/* Memory cards: raw 128 KiB images at <save_dir>/bfm_card<slot>.mcd
 * (the common emulator .mcd layout). Saves are written to a temp file and
 * renamed. A missing card loads as BFM_PLAT_NOT_FOUND. */
int bfm_plat_memcard_set_dir(const char *dir);
int bfm_plat_memcard_load(unsigned slot, uint8_t *card, size_t size);
int bfm_plat_memcard_save(unsigned slot, const uint8_t *card, size_t size);

void bfm_plat_storage_reset(void);

#endif
