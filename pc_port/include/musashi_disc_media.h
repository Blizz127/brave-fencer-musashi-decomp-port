#ifndef MUSASHI_DISC_MEDIA_H
#define MUSASHI_DISC_MEDIA_H

#include <stddef.h>
#include <stdint.h>

#define MUSASHI_DISC_RAW_SECTOR_SIZE 2352u

typedef struct MusashiDiscMedia MusashiDiscMedia;

typedef enum MusashiDiscTrackMode {
    MUSASHI_DISC_TRACK_MODE2_2352 = 1,
    MUSASHI_DISC_TRACK_AUDIO = 2
} MusashiDiscTrackMode;

typedef struct MusashiDiscTrackInfo {
    uint32_t number;
    /* INDEX 00 is the pregap start; INDEX 01 is the program start. */
    uint32_t index00_frame;
    uint32_t index01_frame;
    /* Alias of INDEX 01. end_frame is exclusive at the next track's
     * INDEX 00, or total_frames for the final track. Pregap/audio-gap frames
     * remain addressable through the raw file-frame read API. */
    uint32_t first_frame;
    uint32_t end_frame;
    MusashiDiscTrackMode mode;
} MusashiDiscTrackInfo;

typedef struct MusashiDiscMediaInfo {
    uint32_t sector_size;
    uint32_t total_frames;
    uint32_t track_count;
    MusashiDiscTrackInfo tracks[4];
} MusashiDiscMediaInfo;

/* Opens only the repository's verified cue/bin pair. The returned owner has
 * a private immutable BIN snapshot; callers never borrow a file or pointer.
 * The caller controls the owner's lifetime and must not use its pointer after
 * musashi_disc_media_close. Concurrent readers are safe while the owner
 * remains alive; close must wait until those readers have finished. */
MusashiDiscMedia *musashi_disc_media_open_pinned(const char *cue_path,
                                                 const char *bin_path);

/* Opens the user's own image from one path: a .cue (any file names; its
 * FILE entries are joined in order, e.g. a one-bin or a per-track dump) or a
 * bare .bin. The joined bytes must still be the supported release (the same
 * size and SHA-256 as the pinned image). On refusal `why` gets a sentence a
 * player can act on. A 2048-byte .iso is refused: it has no XA subheaders. */
MusashiDiscMedia *musashi_disc_media_open_image(const char *path, char *why,
                                                size_t why_size);

/* Copies a file from the image's ISO 9660 root directory (e.g.
 * "SLUS_007.26"); *size receives its length. Returns zero if absent or
 * larger than max. */
int musashi_disc_media_read_root_file(const MusashiDiscMedia *media, const char *name,
                                      uint8_t *out, size_t max, size_t *size);

/* Copies immutable metadata into caller storage. Returns zero on refusal. */
int musashi_disc_media_get_info(const MusashiDiscMedia *media,
                                MusashiDiscMediaInfo *info);

/* Copies one complete raw 2352-byte file frame. Invalid frame or output size
 * leaves the caller's output bytes unchanged and returns zero. */
int musashi_disc_media_read_sector(const MusashiDiscMedia *media,
                                   uint32_t file_frame, void *output,
                                   size_t output_size);

/* Releases the private snapshot. NULL is accepted. */
void musashi_disc_media_close(MusashiDiscMedia *media);

#endif
