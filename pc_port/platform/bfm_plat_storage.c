#include "bfm_plat_storage.h"
#include "bfm_plat_mods.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BACKENDS 8

/* ---- "image" backend: user .cue/.bin (raw 2352) or .iso (2048) ---- */

typedef struct ImageDisc {
    FILE *file;
    uint32_t sector_size;
    uint32_t sector_count;
    uint32_t track_count;
} ImageDisc;

static ImageDisc image_state;

static int ends_with_ci(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix), i;
    if (m > n) return 0;
    for (i = 0; i < m; i++)
        if (tolower((unsigned char)s[n - m + i]) != suffix[i]) return 0;
    return 1;
}

/* Resolves the first FILE "..." BINARY entry of a cue sheet relative to the
 * cue's directory; counts TRACK lines. */
static int cue_data_file(const char *cue, char *out, size_t size,
                         uint32_t *tracks) {
    FILE *f = fopen(cue, "r");
    char line[512];
    int found = 0;
    *tracks = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (strncmp(p, "TRACK", 5) == 0) (*tracks)++;
        if (!found && strncmp(p, "FILE", 4) == 0) {
            char *q1 = strchr(p, '"');
            char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            const char *slash;
            size_t dir_len = 0, name_len;
            if (!q2) continue;
            name_len = (size_t)(q2 - q1 - 1);
            slash = strrchr(cue, '/');
#ifdef _WIN32
            if (!slash || strrchr(cue, '\\') > slash) slash = strrchr(cue, '\\');
#endif
            if (slash && q1[1] != '/') dir_len = (size_t)(slash - cue + 1);
            if (dir_len + name_len + 1 > size) continue;
            memcpy(out, cue, dir_len);
            memcpy(out + dir_len, q1 + 1, name_len);
            out[dir_len + name_len] = '\0';
            found = 1;
        }
    }
    fclose(f);
    return found;
}

static int image_open(void *self, const char *path) {
    ImageDisc *d = (ImageDisc *)self;
    char data_path[1024];
    long bytes;
    memset(d, 0, sizeof *d);
    if (!path || !*path) return BFM_PLAT_INVALID;
    if (ends_with_ci(path, ".cue")) {
        if (!cue_data_file(path, data_path, sizeof data_path, &d->track_count))
            return BFM_PLAT_INVALID;
        d->sector_size = BFM_DISC_RAW_SIZE;
    } else {
        if (strlen(path) >= sizeof data_path) return BFM_PLAT_INVALID;
        strcpy(data_path, path);
        d->track_count = 1;
        d->sector_size = ends_with_ci(path, ".iso") ? BFM_DISC_DATA_SIZE
                                                    : BFM_DISC_RAW_SIZE;
    }
    d->file = fopen(data_path, "rb");
    if (!d->file) return BFM_PLAT_NOT_FOUND;
    if (fseek(d->file, 0, SEEK_END) != 0 || (bytes = ftell(d->file)) <= 0 ||
        (unsigned long)bytes % d->sector_size != 0) {
        fclose(d->file);
        d->file = NULL;
        return BFM_PLAT_INVALID;
    }
    d->sector_count = (uint32_t)((unsigned long)bytes / d->sector_size);
    return BFM_PLAT_OK;
}

static void image_close(void *self) {
    ImageDisc *d = (ImageDisc *)self;
    if (d->file) fclose(d->file);
    memset(d, 0, sizeof *d);
}

static int image_info(void *self, BfmPlatDiscInfo *out) {
    ImageDisc *d = (ImageDisc *)self;
    out->sector_count = d->sector_count;
    out->sector_size = d->sector_size;
    out->track_count = d->track_count;
    return BFM_PLAT_OK;
}

static int image_read(void *self, uint32_t lba, BfmPlatDiscReadMode mode,
                      void *out, size_t size) {
    ImageDisc *d = (ImageDisc *)self;
    uint8_t raw[BFM_DISC_RAW_SIZE];
    size_t offset;
    if (!d->file || lba >= d->sector_count) return BFM_PLAT_INVALID;
    if (d->sector_size == BFM_DISC_DATA_SIZE) {
        if (mode != BFM_DISC_READ_DATA) return BFM_PLAT_UNSUPPORTED;
        if (fseek(d->file, (long)lba * (long)BFM_DISC_DATA_SIZE, SEEK_SET) ||
            fread(out, 1, size, d->file) != size)
            return BFM_PLAT_ERROR;
        return BFM_PLAT_OK;
    }
    if (fseek(d->file, (long)lba * (long)BFM_DISC_RAW_SIZE, SEEK_SET) ||
        fread(raw, 1, sizeof raw, d->file) != sizeof raw)
        return BFM_PLAT_ERROR;
    switch (mode) {
    case BFM_DISC_READ_DATA:
    case BFM_DISC_READ_FORM2: offset = 24; break;
    case BFM_DISC_READ_NO_SYNC: offset = 12; break;
    default: offset = 0; break;
    }
    memcpy(out, raw + offset, size);
    return BFM_PLAT_OK;
}

static const BfmPlatStorageBackend image_backend = {
    "image", image_open, image_close, image_info, image_read, &image_state
};

/* ---- dispatcher ---- */

static const BfmPlatStorageBackend *backends[MAX_BACKENDS] = {&image_backend};
static unsigned backend_count = 1;
static const BfmPlatStorageBackend *active;
static char card_dir[512] = "saves";

int bfm_plat_storage_register(const BfmPlatStorageBackend *b) {
    unsigned i;
    if (!b || !b->name || !*b->name || !b->open || !b->read_sector)
        return BFM_PLAT_INVALID;
    for (i = 0; i < backend_count; i++)
        if (strcmp(backends[i]->name, b->name) == 0) return BFM_PLAT_INVALID;
    if (backend_count >= MAX_BACKENDS) return BFM_PLAT_NO_SPACE;
    backends[backend_count++] = b;
    return BFM_PLAT_OK;
}

int bfm_plat_disc_open(const char *name, const char *path) {
    unsigned i;
    int r;
    bfm_plat_disc_close();
    for (i = 0; i < backend_count; i++) {
        if (name && strcmp(backends[i]->name, name) == 0) {
            r = backends[i]->open(backends[i]->self, path);
            if (r == BFM_PLAT_OK) active = backends[i];
            return r;
        }
    }
    return BFM_PLAT_NOT_FOUND;
}

void bfm_plat_disc_close(void) {
    if (active && active->close) active->close(active->self);
    active = NULL;
}

const char *bfm_plat_disc_backend(void) { return active ? active->name : NULL; }

int bfm_plat_disc_info(BfmPlatDiscInfo *out) {
    if (!active) return BFM_PLAT_NOT_READY;
    if (!out) return BFM_PLAT_INVALID;
    memset(out, 0, sizeof *out);
    return active->info ? active->info(active->self, out) : BFM_PLAT_UNSUPPORTED;
}

size_t bfm_plat_disc_mode_size(BfmPlatDiscReadMode mode) {
    switch (mode) {
    case BFM_DISC_READ_DATA: return 2048;
    case BFM_DISC_READ_FORM2: return 2324;
    case BFM_DISC_READ_NO_SYNC: return 2340;
    case BFM_DISC_READ_RAW: return 2352;
    }
    return 0;
}

int bfm_plat_disc_read(uint32_t lba, uint32_t count, BfmPlatDiscReadMode mode,
                       void *out, size_t out_size) {
    size_t each = bfm_plat_disc_mode_size(mode);
    uint32_t i;
    if (!active) return BFM_PLAT_NOT_READY;
    if (!each || !out || count == 0 || out_size / each < count)
        return BFM_PLAT_INVALID;
    for (i = 0; i < count; i++) {
        int r = active->read_sector(active->self, lba + i, mode,
                                    (uint8_t *)out + (size_t)i * each, each);
        if (r != BFM_PLAT_OK) return r;
    }
    return BFM_PLAT_OK;
}

uint32_t bfm_plat_disc_msf_to_lba(uint8_t m, uint8_t s, uint8_t f) {
    uint32_t abs = ((uint32_t)m * 60u + s) * 75u + f;
    return abs >= 150u ? abs - 150u : 0u;
}

void bfm_plat_disc_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s,
                              uint8_t *f) {
    uint32_t abs = lba + 150u;
    if (m) *m = (uint8_t)(abs / (60u * 75u));
    if (s) *s = (uint8_t)((abs / 75u) % 60u);
    if (f) *f = (uint8_t)(abs % 75u);
}

/* ---- ISO9660 ---- */

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* Component compare: case-insensitive, ignores a ";N" suffix on the disc
 * name and a trailing '.' (ISO names like "DIR." / "FILE.;1"). */
static int name_match(const uint8_t *disc, size_t dlen, const char *want,
                      size_t wlen) {
    size_t i;
    while (dlen && disc[dlen - 1] != ';' &&
           memchr(disc, ';', dlen) != NULL)
        dlen--;
    if (dlen && disc[dlen - 1] == ';') dlen--;
    if (dlen && disc[dlen - 1] == '.') dlen--;
    if (wlen >= 2 && want[wlen - 2] == ';') wlen -= 2;
    if (dlen != wlen) return 0;
    for (i = 0; i < dlen; i++)
        if (toupper(disc[i]) != toupper((unsigned char)want[i])) return 0;
    return 1;
}

int bfm_plat_disc_find_file(const char *path, uint32_t *out_lba,
                            uint32_t *out_size) {
    uint8_t sec[BFM_DISC_DATA_SIZE];
    uint32_t dir_lba, dir_size;
    const char *p = path;
    int r;
    if (!active) return BFM_PLAT_NOT_READY;
    if (!path) return BFM_PLAT_INVALID;
    r = bfm_plat_disc_read(16, 1, BFM_DISC_READ_DATA, sec, sizeof sec);
    if (r != BFM_PLAT_OK) return r;
    if (sec[0] != 1 || memcmp(sec + 1, "CD001", 5) != 0) return BFM_PLAT_INVALID;
    dir_lba = le32(sec + 156 + 2);
    dir_size = le32(sec + 156 + 10);
    for (;;) {
        const char *end;
        size_t len;
        uint32_t off, sector;
        int found = 0, last;
        while (*p == '/' || *p == '\\') p++;
        if (!*p) return BFM_PLAT_INVALID;
        end = p;
        while (*end && *end != '/' && *end != '\\') end++;
        len = (size_t)(end - p);
        last = (*end == '\0');
        for (sector = 0; !found && sector * BFM_DISC_DATA_SIZE < dir_size;
             sector++) {
            r = bfm_plat_disc_read(dir_lba + sector, 1, BFM_DISC_READ_DATA,
                                   sec, sizeof sec);
            if (r != BFM_PLAT_OK) return r;
            for (off = 0; off < BFM_DISC_DATA_SIZE;) {
                uint8_t rec = sec[off];
                uint8_t nlen;
                if (rec == 0) break;
                if (rec < 34 || off + rec > BFM_DISC_DATA_SIZE) return BFM_PLAT_INVALID;
                nlen = sec[off + 32];
                if (33u + nlen <= rec &&
                    name_match(sec + off + 33, nlen, p, len)) {
                    int is_dir = (sec[off + 25] & 2) != 0;
                    if (last != !is_dir) return BFM_PLAT_NOT_FOUND;
                    dir_lba = le32(sec + off + 2);
                    dir_size = le32(sec + off + 10);
                    found = 1;
                    break;
                }
                off += rec;
            }
        }
        if (!found) return BFM_PLAT_NOT_FOUND;
        if (last) {
            if (out_lba) *out_lba = dir_lba;
            if (out_size) *out_size = dir_size;
            return BFM_PLAT_OK;
        }
        p = end;
    }
}

int bfm_plat_disc_read_file(const char *path, void *out, size_t cap,
                            size_t *size) {
    uint32_t lba, bytes, sectors;
    uint8_t *buf;
    const uint8_t *rep = NULL;
    size_t rep_size = 0;
    int r;
    if (!size) return BFM_PLAT_INVALID;
    r = bfm_plat_disc_find_file(path, &lba, &bytes);
    if (r != BFM_PLAT_OK) return r;
    sectors = (bytes + BFM_DISC_DATA_SIZE - 1u) / BFM_DISC_DATA_SIZE;
    buf = (uint8_t *)malloc(sectors ? (size_t)sectors * BFM_DISC_DATA_SIZE : 1u);
    if (!buf) return BFM_PLAT_ERROR;
    if (sectors) {
        r = bfm_plat_disc_read(lba, sectors, BFM_DISC_READ_DATA, buf,
                               (size_t)sectors * BFM_DISC_DATA_SIZE);
        if (r != BFM_PLAT_OK) { free(buf); return r; }
    }
    if (bfm_plat_mods_replace_asset("files", buf, bytes, &rep, &rep_size) ==
        BFM_PLAT_OK) {
        *size = rep_size;
        if (rep_size > cap || (rep_size && !out)) { free(buf); return BFM_PLAT_NO_SPACE; }
        memcpy(out, rep, rep_size);
    } else {
        *size = bytes;
        if (bytes > cap || (bytes && !out)) { free(buf); return BFM_PLAT_NO_SPACE; }
        memcpy(out, buf, bytes);
    }
    free(buf);
    return BFM_PLAT_OK;
}

/* ---- memory cards ---- */

int bfm_plat_memcard_set_dir(const char *dir) {
    if (!dir || !*dir || strlen(dir) >= sizeof card_dir) return BFM_PLAT_INVALID;
    strcpy(card_dir, dir);
    return BFM_PLAT_OK;
}

static int card_path(unsigned slot, char *out, size_t size, const char *ext) {
    int n = snprintf(out, size, "%s/bfm_card%u.mcd%s", card_dir, slot, ext);
    return n > 0 && (size_t)n < size;
}

int bfm_plat_memcard_load(unsigned slot, uint8_t *card, size_t size) {
    char path[600];
    FILE *f;
    size_t got;
    int extra;
    if (slot >= BFM_MEMCARD_SLOTS || !card || size != BFM_MEMCARD_SIZE ||
        !card_path(slot, path, sizeof path, ""))
        return BFM_PLAT_INVALID;
    f = fopen(path, "rb");
    if (!f) return BFM_PLAT_NOT_FOUND;
    got = fread(card, 1, size, f);
    extra = fgetc(f);
    fclose(f);
    return (got == size && extra == EOF) ? BFM_PLAT_OK : BFM_PLAT_INVALID;
}

int bfm_plat_memcard_save(unsigned slot, const uint8_t *card, size_t size) {
    char path[600], tmp[600];
    FILE *f;
    int ok;
    if (slot >= BFM_MEMCARD_SLOTS || !card || size != BFM_MEMCARD_SIZE ||
        !card_path(slot, path, sizeof path, "") ||
        !card_path(slot, tmp, sizeof tmp, ".tmp"))
        return BFM_PLAT_INVALID;
    f = fopen(tmp, "wb");
    if (!f) return BFM_PLAT_ERROR;
    ok = fwrite(card, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
#ifdef _WIN32
    if (ok) remove(path);
#endif
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        return BFM_PLAT_ERROR;
    }
    return BFM_PLAT_OK;
}

void bfm_plat_storage_reset(void) {
    bfm_plat_disc_close();
    backend_count = 1;
    strcpy(card_dir, "saves");
}
