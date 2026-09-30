#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "bfm_plat_disc_check.h"
#include "bfm_plat_sha256.h"
#include "bfm_plat_storage.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static const char *const names[BFM_DISC_STATUS_COUNT] = {
    "ok", "no_path", "unreadable", "missing_tracks", "unsupported_format",
    "not_ps1", "wrong_region", "wrong_disc", "bad_dump"
};

static const char *const messages[BFM_DISC_STATUS_COUNT] = {
    "Disc OK: Brave Fencer Musashi (US, SLUS-00726).",
    "No disc image is configured. Put your own dump of the US disc (.cue + .bin, or .iso) "
    "in the disc folder, set [disc] path in the config, or start with --disc PATH.",
    "The disc image could not be read. Check the path and that the file is a complete dump "
    "(a raw .bin must be a whole number of 2352-byte sectors, an .iso of 2048-byte sectors).",
    "The .cue sheet names a track file that is missing. Keep the .cue next to all of its .bin "
    "files, with the names the .cue lists.",
    "This image format is not supported. CHD is not supported yet: convert it with "
    "`chdman extractcd -i game.chd -o game.cue`. Supported: .cue/.bin, .bin, .iso, .img.",
    "This is not a PlayStation game disc (no ISO9660 volume or no SYSTEM.CNF).",
    "This is Brave Fencer Musashi, but not the US release. The port needs the North "
    "American disc, SLUS-00726.",
    "This is a different PlayStation game. The port needs Brave Fencer Musashi (US, SLUS-00726).",
    "SLUS_007.26 is on the disc but does not match the retail US executable: the dump is "
    "damaged or the disc was modified. Re-dump the disc (check the dump against redump.org)."
};

const char *bfm_plat_disc_status_name(BfmPlatDiscStatus s) {
    return (unsigned)s < BFM_DISC_STATUS_COUNT ? names[s] : "unknown";
}

const char *bfm_plat_disc_status_message(BfmPlatDiscStatus s) {
    return (unsigned)s < BFM_DISC_STATUS_COUNT ? messages[s] : "Unknown disc status.";
}

void bfm_plat_disc_check_describe(const BfmPlatDiscCheck *c, char *out, size_t size) {
    if (!out || !size) return;
    if (!c) { out[0] = '\0'; return; }
    if (c->detail[0])
        snprintf(out, size, "%s (%s)", bfm_plat_disc_status_message(c->status), c->detail);
    else
        snprintf(out, size, "%s", bfm_plat_disc_status_message(c->status));
}

static int ext_is(const char *path, const char *ext) {
    size_t n = strlen(path), m = strlen(ext), i;
    if (m > n) return 0;
    for (i = 0; i < m; i++)
        if (tolower((unsigned char)path[n - m + i]) != ext[i]) return 0;
    return 1;
}

static int file_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int set(BfmPlatDiscCheck *c, BfmPlatDiscStatus s, const char *detail) {
    c->status = s;
    /* long paths are cut to the detail buffer on purpose */
    snprintf(c->detail, sizeof c->detail, "%.*s", (int)(sizeof c->detail - 1),
             detail ? detail : "");
    return s == BFM_DISC_OK ? BFM_PLAT_OK : BFM_PLAT_INVALID;
}

/* Every FILE "x" in the cue must exist next to it. */
static int check_cue(const char *cue, BfmPlatDiscCheck *c) {
    FILE *f = fopen(cue, "r");
    char line[600], dir[512];
    const char *slash = strrchr(cue, '/');
    size_t dlen = 0;
    int files = 0;
#ifdef _WIN32
    if (!slash || strrchr(cue, '\\') > slash) slash = strrchr(cue, '\\');
#endif
    if (!f) return set(c, BFM_DISC_UNREADABLE, cue);
    if (slash) dlen = (size_t)(slash - cue + 1);
    if (dlen >= sizeof dir) dlen = 0;
    memcpy(dir, cue, dlen);
    dir[dlen] = '\0';
    while (fgets(line, sizeof line, f)) {
        char *p = line, *q1, *q2, full[1100];
        while (isspace((unsigned char)*p)) p++;
        if (strncmp(p, "TRACK", 5) == 0) c->track_count++;
        if (strncmp(p, "FILE", 4) != 0) continue;
        q1 = strchr(p, '"');
        q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        if (!q2) continue;
        *q2 = '\0';
        snprintf(full, sizeof full, "%s%s", q1[1] == '/' ? "" : dir, q1 + 1);
        if (!file_exists(full)) {
            fclose(f);
            return set(c, BFM_DISC_MISSING_TRACKS, full);
        }
        if (files++ == 0) {
            size_t n = strlen(full);
            if (n >= sizeof c->data_file) {
                fclose(f);
                return set(c, BFM_DISC_UNREADABLE, "track path too long");
            }
            memcpy(c->data_file, full, n + 1);
        }
    }
    fclose(f);
    if (!files) return set(c, BFM_DISC_UNREADABLE, "the .cue lists no FILE");
    return BFM_PLAT_OK;
}

/* "BOOT = cdrom:\SLUS_007.26;1" -> "SLUS_007.26" */
static int parse_system_cnf(const char *text, size_t n, char *boot, size_t size) {
    size_t i = 0;
    while (i < n) {
        size_t end = i;
        while (end < n && text[end] != '\n' && text[end] != '\r') end++;
        if (end - i > 4 && strncmp(text + i, "BOOT", 4) == 0) {
            const char *s = text + i, *e = text + end, *start;
            const char *eq = memchr(s, '=', (size_t)(e - s));
            if (eq) {
                start = eq + 1;
                for (s = start; s < e; s++)
                    if (*s == '\\' || *s == '/' || *s == ':') start = s + 1;
                while (start < e && isspace((unsigned char)*start)) start++;
                for (s = start; s < e && *s != ';' && !isspace((unsigned char)*s); s++) {}
                if (s > start && (size_t)(s - start) < size) {
                    size_t k;
                    for (k = 0; k < (size_t)(s - start); k++)
                        boot[k] = (char)toupper((unsigned char)start[k]);
                    boot[s - start] = '\0';
                    return 1;
                }
            }
        }
        i = end + 1;
    }
    return 0;
}

/* Reads a whole file straight from the sectors: never through the mods
 * replacement hook, so a mod can't change what is validated. */
static int raw_read(const char *path, void *out, size_t cap, size_t *got) {
    uint32_t lba, size, sectors;
    uint8_t *buf;
    int r;
    r = bfm_plat_disc_find_file(path, &lba, &size);
    if (r != BFM_PLAT_OK) return r;
    if (size > cap) return BFM_PLAT_NO_SPACE;
    sectors = (size + BFM_DISC_DATA_SIZE - 1u) / BFM_DISC_DATA_SIZE;
    buf = (uint8_t *)malloc(sectors ? (size_t)sectors * BFM_DISC_DATA_SIZE : 1u);
    if (!buf) return BFM_PLAT_ERROR;
    r = sectors ? bfm_plat_disc_read(lba, sectors, BFM_DISC_READ_DATA, buf,
                                     (size_t)sectors * BFM_DISC_DATA_SIZE) : BFM_PLAT_OK;
    if (r == BFM_PLAT_OK) memcpy(out, buf, size);
    free(buf);
    *got = size;
    return r;
}

static BfmPlatDiscStatus classify_other(const char *boot) {
    /* Brave Fencer Musashi's other release is Japanese ("Brave Fencer
     * Musashiden"); any Japanese/Asian or PAL serial is reported as the
     * wrong region, other US serials as a different game. */
    if (!strncmp(boot, "SLPS", 4) || !strncmp(boot, "SLPM", 4) || !strncmp(boot, "SCPS", 4) ||
        !strncmp(boot, "SLES", 4) || !strncmp(boot, "SCES", 4) || !strncmp(boot, "SLKA", 4))
        return BFM_DISC_WRONG_REGION;
    return BFM_DISC_WRONG_DISC;
}

int bfm_plat_disc_validate_ex(const char *path, const char *want_boot,
                              const char *want_sha, BfmPlatDiscCheck *c) {
    BfmPlatDiscInfo info;
    char cnf[2048];
    size_t got;
    uint32_t lba, size;
    uint8_t *exe;
    int r;
    if (!c) return BFM_PLAT_INVALID;
    memset(c, 0, sizeof *c);
    bfm_plat_disc_close();
    if (!path || !*path) return set(c, BFM_DISC_NO_PATH, NULL);
    if (ext_is(path, ".chd")) return set(c, BFM_DISC_UNSUPPORTED_FORMAT, path);
    if (!(ext_is(path, ".cue") || ext_is(path, ".bin") || ext_is(path, ".iso") ||
          ext_is(path, ".img")))
        return set(c, BFM_DISC_UNSUPPORTED_FORMAT, path);
    if (!file_exists(path)) return set(c, BFM_DISC_UNREADABLE, path);
    if (ext_is(path, ".cue")) {
        if (check_cue(path, c) != BFM_PLAT_OK) return BFM_PLAT_INVALID;
    } else {
        snprintf(c->data_file, sizeof c->data_file, "%s", path);
        c->track_count = 1;
    }
    r = bfm_plat_disc_open("image", path);
    if (r != BFM_PLAT_OK) return set(c, BFM_DISC_UNREADABLE, c->data_file);
    bfm_plat_disc_info(&info);
    c->sector_size = info.sector_size;
    if (info.sector_size == BFM_DISC_RAW_SIZE) {
        uint8_t raw[BFM_DISC_RAW_SIZE];
        static const uint8_t sync[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                         0xFF, 0xFF, 0xFF, 0xFF, 0};
        if (bfm_plat_disc_read(16, 1, BFM_DISC_READ_RAW, raw, sizeof raw) != BFM_PLAT_OK ||
            memcmp(raw, sync, 12) != 0 || raw[15] != 2) {
            bfm_plat_disc_close();
            return set(c, BFM_DISC_UNREADABLE, "sector 16 is not a Mode 2 data sector");
        }
    }
    r = raw_read("SYSTEM.CNF", cnf, sizeof cnf - 1, &got);
    if (r == BFM_PLAT_INVALID) {
        bfm_plat_disc_close();
        return set(c, BFM_DISC_NOT_PS1, "no ISO9660 volume");
    }
    if (r != BFM_PLAT_OK) {
        bfm_plat_disc_close();
        return set(c, BFM_DISC_NOT_PS1, r == BFM_PLAT_NOT_FOUND ? "no SYSTEM.CNF" : "SYSTEM.CNF unreadable");
    }
    cnf[got] = '\0';
    if (!parse_system_cnf(cnf, got, c->boot_name, sizeof c->boot_name)) {
        bfm_plat_disc_close();
        return set(c, BFM_DISC_NOT_PS1, "SYSTEM.CNF has no BOOT line");
    }
    if (strcmp(c->boot_name, want_boot) != 0) {
        bfm_plat_disc_close();
        return set(c, classify_other(c->boot_name), c->boot_name);
    }
    if (bfm_plat_disc_find_file(c->boot_name, &lba, &size) != BFM_PLAT_OK || size == 0 ||
        size > 8u * 1024u * 1024u) {
        bfm_plat_disc_close();
        return set(c, BFM_DISC_UNREADABLE, c->boot_name);
    }
    c->boot_size = size;
    exe = (uint8_t *)malloc(size);
    if (!exe) {
        bfm_plat_disc_close();
        return set(c, BFM_DISC_UNREADABLE, "out of memory");
    }
    r = raw_read(c->boot_name, exe, size, &got);
    if (r == BFM_PLAT_OK) bfm_plat_sha256_hex(exe, got, c->boot_sha256);
    free(exe);
    if (r != BFM_PLAT_OK) {
        bfm_plat_disc_close();
        return set(c, BFM_DISC_UNREADABLE, c->boot_name);
    }
    if (strcmp(c->boot_sha256, want_sha) != 0) {
        bfm_plat_disc_close();
        return set(c, BFM_DISC_BAD_DUMP, c->boot_sha256);
    }
    return set(c, BFM_DISC_OK, NULL);
}

int bfm_plat_disc_validate(const char *path, BfmPlatDiscCheck *out) {
    return bfm_plat_disc_validate_ex(path, BFM_RETAIL_BOOT_NAME, BFM_RETAIL_BOOT_SHA256, out);
}

typedef struct Found {
    char best[1024];
    int rank;                     /* 3 cue, 2 iso/img, 1 bin, 0 chd */
    const char *dir;
} Found;

static void consider(Found *f, const char *name) {
    int rank = ext_is(name, ".cue") ? 4 : (ext_is(name, ".iso") || ext_is(name, ".img")) ? 3
             : ext_is(name, ".bin") ? 2 : ext_is(name, ".chd") ? 1 : 0;
    if (rank > f->rank) {
        f->rank = rank;
        snprintf(f->best, sizeof f->best, "%s/%s", f->dir, name);
    }
}

int bfm_plat_disc_autodetect(const char *dirs, char *out, size_t size) {
    char list[1024];
    char *tok, *rest;
    if (!dirs || !out || !size) return BFM_PLAT_INVALID;
    snprintf(list, sizeof list, "%s", dirs);
    for (tok = list; tok && *tok; tok = rest) {
        Found f;
        rest = strchr(tok, ';');
        if (rest) *rest++ = '\0';
        memset(&f, 0, sizeof f);
        f.dir = tok;
#ifdef _WIN32
        {
            char pattern[1100];
            WIN32_FIND_DATAA fd;
            HANDLE h;
            snprintf(pattern, sizeof pattern, "%s\\*", tok);
            h = FindFirstFileA(pattern, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do consider(&f, fd.cFileName); while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
#else
        {
            DIR *d = opendir(tok);
            struct dirent *e;
            if (d) {
                while ((e = readdir(d)) != NULL)
                    if (e->d_name[0] != '.') consider(&f, e->d_name);
                closedir(d);
            }
        }
#endif
        if (f.rank > 0) {
            if (strlen(f.best) >= size) return BFM_PLAT_NO_SPACE;
            snprintf(out, size, "%s", f.best);
            return BFM_PLAT_OK;
        }
    }
    return BFM_PLAT_NOT_FOUND;
}
