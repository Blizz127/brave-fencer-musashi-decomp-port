#include "musashi_disc_media.h"

#include <openssl/evp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PINNED_CUE_SIZE = 250,
    PINNED_BIN_SIZE = 416021760,
    PINNED_TOTAL_FRAMES = 176880,
    PINNED_TRACK_COUNT = 4,
    READ_CHUNK = 1024 * 1024
};

struct MusashiDiscMedia {
    unsigned char *image;
    MusashiDiscMediaInfo info;
};

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int sha256_matches(const unsigned char *data, size_t size,
                          const char *expected_hex) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    size_t i;

    if (!data || !expected_hex || strlen(expected_hex) != 64u ||
        EVP_Digest(data, size, digest, &digest_size, EVP_sha256(), NULL) != 1 ||
        digest_size != 32u)
        return 0;
    for (i = 0; i < 32u; ++i) {
        int high = hex_value(expected_hex[i * 2u]);
        int low = hex_value(expected_hex[i * 2u + 1u]);
        if (high < 0 || low < 0 || digest[i] != (unsigned char)((high << 4) | low))
            return 0;
    }
    return 1;
}

static int read_file_bytes(const char *path, unsigned char *destination,
                           size_t expected_size) {
    FILE *file;
    long length;
    size_t offset = 0;
    int close_ok;

    if (!path || !destination) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        (unsigned long)length != (unsigned long)expected_size ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    while (offset < expected_size) {
        size_t request = expected_size - offset;
        size_t count = request > READ_CHUNK ? READ_CHUNK : request;
        size_t got = fread(destination + offset, 1, count, file);
        offset += got;
        if (got != count) break;
    }
    close_ok = fclose(file) == 0;
    return close_ok && offset == expected_size;
}

static int file_has_exact_size(const char *path, size_t expected_size) {
    FILE *file;
    long length;
    int close_ok;

    if (!path) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0)
        close_ok = 0;
    else
        close_ok = (unsigned long)length == (unsigned long)expected_size;
    if (fclose(file) != 0) close_ok = 0;
    return close_ok;
}

static int validate_cue(const char *cue_path) {
    unsigned char cue[PINNED_CUE_SIZE];
    return read_file_bytes(cue_path, cue, sizeof(cue)) &&
           sha256_matches(cue, sizeof(cue),
                          "85326d3f735d603a9d37f0e0754a37a52cc9d0bf6b8b4ce566c4fa54f6fdee5c");
}

static void fill_info(MusashiDiscMediaInfo *info) {
    memset(info, 0, sizeof(*info));
    info->sector_size = MUSASHI_DISC_RAW_SECTOR_SIZE;
    info->total_frames = PINNED_TOTAL_FRAMES;
    info->track_count = PINNED_TRACK_COUNT;

    info->tracks[0].number = 1;
    info->tracks[0].index00_frame = 0;
    info->tracks[0].index01_frame = 0;
    info->tracks[0].first_frame = 0;
    info->tracks[0].end_frame = 155122;
    info->tracks[0].mode = MUSASHI_DISC_TRACK_MODE2_2352;

    info->tracks[1].number = 2;
    info->tracks[1].index00_frame = 155122;
    info->tracks[1].index01_frame = 155272;
    info->tracks[1].first_frame = 155272;
    info->tracks[1].end_frame = 159662;
    info->tracks[1].mode = MUSASHI_DISC_TRACK_AUDIO;

    info->tracks[2].number = 3;
    info->tracks[2].index00_frame = 159662;
    info->tracks[2].index01_frame = 159812;
    info->tracks[2].first_frame = 159812;
    info->tracks[2].end_frame = 162778;
    info->tracks[2].mode = MUSASHI_DISC_TRACK_AUDIO;

    info->tracks[3].number = 4;
    info->tracks[3].index00_frame = 162778;
    info->tracks[3].index01_frame = 162928;
    info->tracks[3].first_frame = 162928;
    info->tracks[3].end_frame = PINNED_TOTAL_FRAMES;
    info->tracks[3].mode = MUSASHI_DISC_TRACK_AUDIO;
}

MusashiDiscMedia *musashi_disc_media_open_pinned(const char *cue_path,
                                                 const char *bin_path) {
    unsigned char *image;
    MusashiDiscMedia *media;

    if (!validate_cue(cue_path)) return NULL;
    if (!file_has_exact_size(bin_path, PINNED_BIN_SIZE)) return NULL;
    image = malloc(PINNED_BIN_SIZE);
    if (!image) return NULL;
    if (!read_file_bytes(bin_path, image, PINNED_BIN_SIZE) ||
        !sha256_matches(image, PINNED_BIN_SIZE,
                        "0a53702937d74e20d99fee9a29a80f7e91da762e66958819d531173939a50879")) {
        free(image);
        return NULL;
    }
    media = calloc(1, sizeof(*media));
    if (!media) {
        free(image);
        return NULL;
    }
    media->image = image;
    fill_info(&media->info);
    return media;
}

static int ends_with(const char *path, const char *ext) {
    size_t n = strlen(path), m = strlen(ext), i;
    if (n < m) return 0;
    for (i = 0; i < m; ++i) {
        char c = path[n - m + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != ext[i]) return 0;
    }
    return 1;
}

static long file_size(const char *path) {
    FILE *file = fopen(path, "rb");
    long length = -1;
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) == 0) length = ftell(file);
    fclose(file);
    return length;
}

/* Up to eight FILE entries of a cue sheet, resolved against its folder. */
static int cue_files(const char *cue_path, char files[][4096], int max) {
    FILE *cue = fopen(cue_path, "r");
    char line[4096], dir[4096];
    const char *slash = strrchr(cue_path, '/');
    int count = 0;
    if (!cue) return -1;
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - cue_path) : 0, cue_path);
    while (fgets(line, sizeof line, cue)) {
        char *p = line, *name, *end;
        while (*p == ' ' || *p == '\t') ++p;
        if (strncmp(p, "FILE", 4) != 0 || (p[4] != ' ' && p[4] != '\t')) continue;
        p += 5;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '"') {
            name = ++p;
            end = strchr(p, '"');
        } else {
            name = p;
            end = strpbrk(p, " \t\r\n");
        }
        if (!end || end == name || count >= max) { fclose(cue); return -1; }
        *end = '\0';
        if (name[0] == '/' || !slash) snprintf(files[count], 4096, "%s", name);
        else snprintf(files[count], 4096, "%s/%s", dir, name);
        ++count;
    }
    fclose(cue);
    return count;
}

MusashiDiscMedia *musashi_disc_media_open_image(const char *path, char *why,
                                                size_t why_size) {
    char files[8][4096];
    int count, i;
    size_t offset = 0;
    unsigned char *image;
    MusashiDiscMedia *media;
#define REFUSE(...) do { if (why && why_size) snprintf(why, why_size, __VA_ARGS__); return NULL; } while (0)
    if (!path || !path[0]) REFUSE("No disc image was given.");
    if (ends_with(path, ".iso"))
        REFUSE("%s is an .iso (2048-byte sectors). PlayStation games need the raw "
               "2352-byte dump: use the .cue/.bin of your disc.", path);
    if (ends_with(path, ".cue")) {
        count = cue_files(path, files, 8);
        if (count <= 0) REFUSE("%s could not be read as a cue sheet.", path);
    } else {
        snprintf(files[0], sizeof files[0], "%s", path);
        count = 1;
    }
    for (i = 0; i < count; ++i) {
        long length = file_size(files[i]);
        if (length < 0) REFUSE("The disc image file %s is missing.", files[i]);
        if ((unsigned long)length > PINNED_BIN_SIZE - offset) offset = PINNED_BIN_SIZE + 1u;
        else offset += (size_t)length;
    }
    if (offset != PINNED_BIN_SIZE)
        REFUSE("%s is not a complete image of Brave Fencer Musashi (USA, SLUS-00726): "
               "the size does not match.", path);
    image = malloc(PINNED_BIN_SIZE);
    if (!image) REFUSE("Not enough memory to load the disc image.");
    for (offset = 0, i = 0; i < count; ++i) {
        size_t length = (size_t)file_size(files[i]);
        if (!read_file_bytes(files[i], image + offset, length)) {
            free(image);
            REFUSE("The disc image file %s could not be read.", files[i]);
        }
        offset += length;
    }
    if (!sha256_matches(image, PINNED_BIN_SIZE,
                        "0a53702937d74e20d99fee9a29a80f7e91da762e66958819d531173939a50879")) {
        free(image);
        REFUSE("%s is not an unmodified image of Brave Fencer Musashi (USA, SLUS-00726).", path);
    }
    media = calloc(1, sizeof(*media));
    if (!media) {
        free(image);
        REFUSE("Not enough memory to load the disc image.");
    }
#undef REFUSE
    media->image = image;
    fill_info(&media->info);
    return media;
}

/* ISO 9660 in Mode 2 Form 1 sectors: user data starts 24 bytes in. */
static const unsigned char *iso_sector(const MusashiDiscMedia *media, uint32_t lba) {
    if (lba >= media->info.tracks[0].end_frame) return NULL;
    return media->image + (size_t)lba * MUSASHI_DISC_RAW_SECTOR_SIZE + 24u;
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int musashi_disc_media_read_root_file(const MusashiDiscMedia *media, const char *name,
                                      uint8_t *out, size_t max, size_t *size) {
    const unsigned char *pvd, *root;
    uint32_t dir_lba, dir_size, pos;
    size_t name_len;
    if (!media || !media->image || !name || !out || !size) return 0;
    name_len = strlen(name);
    pvd = iso_sector(media, 16);
    if (!pvd || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) return 0;
    root = pvd + 156;
    dir_lba = le32(root + 2);
    dir_size = le32(root + 10);
    for (pos = 0; pos < dir_size;) {
        const unsigned char *sector = iso_sector(media, dir_lba + pos / 2048u);
        const unsigned char *rec;
        if (!sector) return 0;
        rec = sector + pos % 2048u;
        if (rec[0] == 0) { pos = (pos / 2048u + 1u) * 2048u; continue; }
        if (rec[32] >= name_len && !memcmp(rec + 33, name, name_len) &&
            (rec[32] == name_len || rec[33 + name_len] == ';')) {
            uint32_t lba = le32(rec + 2), length = le32(rec + 10), done = 0;
            if (length > max) return 0;
            while (done < length) {
                const unsigned char *data = iso_sector(media, lba + done / 2048u);
                uint32_t chunk = length - done < 2048u ? length - done : 2048u;
                if (!data) return 0;
                memcpy(out + done, data, chunk);
                done += chunk;
            }
            *size = length;
            return 1;
        }
        pos += rec[0];
    }
    return 0;
}

int musashi_disc_media_get_info(const MusashiDiscMedia *media,
                                MusashiDiscMediaInfo *info) {
    if (!media || !media->image || !info) return 0;
    *info = media->info;
    return 1;
}

int musashi_disc_media_read_sector(const MusashiDiscMedia *media,
                                   uint32_t file_frame, void *output,
                                   size_t output_size) {
    size_t offset;

    if (!media || !media->image || !output ||
        output_size < MUSASHI_DISC_RAW_SECTOR_SIZE ||
        file_frame >= media->info.total_frames)
        return 0;
    offset = (size_t)file_frame * MUSASHI_DISC_RAW_SECTOR_SIZE;
    memcpy(output, media->image + offset, MUSASHI_DISC_RAW_SECTOR_SIZE);
    return 1;
}

void musashi_disc_media_close(MusashiDiscMedia *media) {
    if (!media) return;
    free(media->image);
    media->image = NULL;
    free(media);
}
