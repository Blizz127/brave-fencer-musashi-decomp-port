/* "pinned" storage backend: bfm_plat storage -> pc_port/disc_media.c, which
 * opens only a cue/bin pair whose SHA-256 matches the verified retail
 * release (the user's own dump; nothing from it is in the repository).
 * path = "<cue>|<bin>". Needs OpenSSL (libcrypto), like disc_media.c. */
#include "../bfm_plat.h"
#include "musashi_disc_media.h"

#include <stdio.h>
#include <string.h>

typedef struct PinnedDisc {
    MusashiDiscMedia *media;
    MusashiDiscMediaInfo info;
} PinnedDisc;

static PinnedDisc state;

static int pinned_open(void *self, const char *path) {
    PinnedDisc *d = (PinnedDisc *)self;
    char cue[1024], bin[1024];
    const char *bar;
    memset(d, 0, sizeof *d);
    if (!path || !(bar = strchr(path, '|')) || (size_t)(bar - path) >= sizeof cue ||
        strlen(bar + 1) >= sizeof bin)
        return BFM_PLAT_INVALID;
    memcpy(cue, path, (size_t)(bar - path));
    cue[bar - path] = '\0';
    strcpy(bin, bar + 1);
    d->media = musashi_disc_media_open_pinned(cue, bin);
    if (!d->media) return BFM_PLAT_NOT_FOUND;
    if (!musashi_disc_media_get_info(d->media, &d->info)) {
        musashi_disc_media_close(d->media);
        d->media = NULL;
        return BFM_PLAT_ERROR;
    }
    return BFM_PLAT_OK;
}

static void pinned_close(void *self) {
    PinnedDisc *d = (PinnedDisc *)self;
    musashi_disc_media_close(d->media);
    d->media = NULL;
}

static int pinned_info(void *self, BfmPlatDiscInfo *out) {
    PinnedDisc *d = (PinnedDisc *)self;
    out->sector_count = d->info.total_frames;
    out->sector_size = d->info.sector_size;
    out->track_count = d->info.track_count;
    return BFM_PLAT_OK;
}

static int pinned_read(void *self, uint32_t lba, BfmPlatDiscReadMode mode,
                       void *out, size_t size) {
    PinnedDisc *d = (PinnedDisc *)self;
    uint8_t raw[MUSASHI_DISC_RAW_SECTOR_SIZE];
    size_t off = mode == BFM_DISC_READ_RAW ? 0 : mode == BFM_DISC_READ_NO_SYNC ? 12 : 24;
    if (!d->media ||
        !musashi_disc_media_read_sector(d->media, lba, raw, sizeof raw))
        return BFM_PLAT_INVALID;
    memcpy(out, raw + off, size);
    return BFM_PLAT_OK;
}

static const BfmPlatStorageBackend pinned_backend = {
    "pinned", pinned_open, pinned_close, pinned_info, pinned_read, &state
};

int bfm_plat_backend_pinned_disc_register(void);
int bfm_plat_backend_pinned_disc_register(void) {
    return bfm_plat_storage_register(&pinned_backend);
}
