#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "bfm_plat_mods.h"
#include "bfm_plat_console.h"
#include "bfm_plat_ini.h"
#include "bfm_plat_watch.h"

int bfm_plat_watch_drop_from(int first_id);
int bfm_plat_watch_next_id(void);

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define BFM_PLUGIN_SUFFIX ".dll"
#else
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __APPLE__
#define BFM_PLUGIN_SUFFIX ".dylib"
#else
#define BFM_PLUGIN_SUFFIX ".so"
#endif
#endif

#define MAX_SUBS 256
#define MAX_STATIC 16
#define MAX_DECODERS 8
#define MAX_RUNTIMES 4

enum { KIND_TEXTURES = 0, KIND_FILES = 1, KIND_COUNT };
static const char *const kind_names[KIND_COUNT] = {"textures", "files"};

typedef struct Sub {
    BfmEvent event;
    BfmEventFn fn;
    void *user;
    int id;
} Sub;

typedef struct Asset {
    int kind;
    uint64_t hash;
    int mod;
    char path[640];
    int loaded;                   /* 0 no, 1 yes, -1 failed */
    uint8_t *data;
    size_t size;
    uint32_t width, height;       /* textures */
} Asset;

typedef struct StaticPlugin {
    char name[BFM_PLAT_MOD_NAME_MAX];
    BfmPluginInitFn init;
    BfmPluginShutdownFn shutdown;
} StaticPlugin;

typedef struct Decoder {
    char ext[16];
    BfmPlatTextureDecoder fn;
} Decoder;

typedef struct LoadedPlugin {
    void *handle;
    BfmPluginShutdownFn shutdown;
} LoadedPlugin;

static BfmPlatModInfo mods[BFM_PLAT_MODS_MAX];
static unsigned mod_count;
static BfmPluginHost hosts[BFM_PLAT_MODS_MAX];
static LoadedPlugin plugins[BFM_PLAT_MODS_MAX];
static Sub subs[MAX_SUBS];
static unsigned sub_count;
static int next_sub_id = 1;
static uint64_t event_counts[BFM_EVENT_COUNT];
static StaticPlugin statics[MAX_STATIC];
static unsigned static_count;
static Decoder decoders[MAX_DECODERS];
static unsigned decoder_count;
static const BfmPlatScriptRuntime *runtimes[MAX_RUNTIMES];
static unsigned runtime_count;
static Asset *assets;
static size_t asset_count, asset_cap;
static BfmPlatGuestMemory guest;
static int guest_bound;
static void (*log_fn)(const char *, int, const char *);
static int dump_textures;
static char dump_dir[512];
static uint64_t *dumped;
static size_t dumped_count, dumped_cap;
static int initialized;
/* Mod whose plugin/script is being loaded (-1 outside loading): owns the
 * cheats it registers so they are dropped with it. */
static int loading_mod = -1;
/* Subscriptions with ids >= this were made by mods and are dropped on
 * shutdown (their code may be unloaded). */
static int first_mod_sub_id = 1;
static int first_mod_watch_id = 1;

/* cheats.ini copy cheats: copy `width` bytes from src to address each frame. */
typedef struct CopyCheat {
    uint32_t src, dst;
    uint8_t width;
} CopyCheat;
static CopyCheat copy_cheats[BFM_PLAT_CHEATS_MAX];
static unsigned copy_cheat_count;

static void copy_cheat_apply(void *user) {
    CopyCheat *c = (CopyCheat *)user;
    uint8_t b[4];
    if (bfm_plat_guest_read(c->src, b, c->width) == BFM_PLAT_OK)
        bfm_plat_guest_write(c->dst, b, c->width);
}

/* ---------------------------------------------------------------- log */

void bfm_plat_mods_log(const char *mod, int level, const char *message) {
    if (log_fn) {
        log_fn(mod, level, message);
        return;
    }
    if (level <= 2)
        fprintf(stderr, "[%s] %s\n", mod ? mod : "mods", message ? message : "");
}

static void logf_(const char *mod, int level, const char *fmt, const char *a) {
    char buf[768];
    snprintf(buf, sizeof buf, fmt, a);
    bfm_plat_mods_log(mod, level, buf);
}

/* ------------------------------------------------------------- events */

int bfm_plat_mods_subscribe(BfmEvent event, BfmEventFn fn, void *user) {
    if ((unsigned)event >= BFM_EVENT_COUNT || !fn) return BFM_PLAT_INVALID;
    /* Append only (holes left by unsubscribe are not reused) so hooks keep
     * their registration order. */
    if (sub_count >= MAX_SUBS) return BFM_PLAT_NO_SPACE;
    subs[sub_count].event = event;
    subs[sub_count].fn = fn;
    subs[sub_count].user = user;
    subs[sub_count].id = next_sub_id++;
    return subs[sub_count++].id;
}

int bfm_plat_mods_unsubscribe(int id) {
    unsigned i;
    for (i = 0; i < sub_count; i++) {
        if (subs[i].fn && subs[i].id == id) {
            subs[i].fn = NULL;
            return BFM_PLAT_OK;
        }
    }
    return BFM_PLAT_NOT_FOUND;
}

void bfm_plat_mods_emit(BfmEvent event, void *payload) {
    unsigned i, n;
    if ((unsigned)event >= BFM_EVENT_COUNT) return;
    event_counts[event]++;
    n = sub_count; /* hooks added during emit run from the next emit */
    for (i = 0; i < n; i++)
        if (subs[i].fn && subs[i].event == event)
            subs[i].fn(subs[i].user, event, payload);
}

uint64_t bfm_plat_mods_event_count(BfmEvent event) {
    return (unsigned)event < BFM_EVENT_COUNT ? event_counts[event] : 0;
}

/* -------------------------------------------------------- guest memory */

void bfm_plat_guest_memory_bind(const BfmPlatGuestMemory *m) {
    if (m && m->read && m->write) {
        guest = *m;
        guest_bound = 1;
    } else {
        memset(&guest, 0, sizeof guest);
        guest_bound = 0;
    }
}

int bfm_plat_guest_read(uint32_t a, void *out, uint32_t n) {
    if (!guest_bound) return BFM_PLAT_NOT_READY;
    if (!out && n) return BFM_PLAT_INVALID;
    return guest.read(guest.user, a, out, n);
}

int bfm_plat_guest_write(uint32_t a, const void *data, uint32_t n) {
    if (!guest_bound) return BFM_PLAT_NOT_READY;
    if (!data && n) return BFM_PLAT_INVALID;
    return guest.write(guest.user, a, data, n);
}

static BfmPlatGuestRegs guest_regs;

void bfm_plat_guest_regs_bind(const BfmPlatGuestRegs *r) {
    if (r && r->read) guest_regs = *r;
    else memset(&guest_regs, 0, sizeof guest_regs);
}

int bfm_plat_guest_reg(unsigned reg, uint32_t *out) {
    if (!guest_regs.read) return BFM_PLAT_NOT_READY;
    if (reg > 31 || !out) return BFM_PLAT_INVALID;
    if (reg == 0) { *out = 0; return BFM_PLAT_OK; }
    return guest_regs.read(guest_regs.user, reg, out);
}

/* ------------------------------------------------------------- hashing */

uint64_t bfm_plat_hash64(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xcbf29ce484222325ull;
    size_t i;
    for (i = 0; i < size; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

uint64_t bfm_plat_texture_hash(const BfmPlatRect *r, const uint16_t *px) {
    uint64_t h = 0xcbf29ce484222325ull;
    uint8_t b[4];
    size_t i, n;
    if (!r || !px || r->w <= 0 || r->h <= 0) return 0;
    b[0] = (uint8_t)r->w; b[1] = (uint8_t)((uint16_t)r->w >> 8);
    b[2] = (uint8_t)r->h; b[3] = (uint8_t)((uint16_t)r->h >> 8);
    for (i = 0; i < 4; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
    n = (size_t)r->w * (size_t)r->h;
    for (i = 0; i < n; i++) {
        h ^= (uint8_t)(px[i] & 0xFFu); h *= 0x100000001b3ull;
        h ^= (uint8_t)(px[i] >> 8); h *= 0x100000001b3ull;
    }
    return h;
}

void bfm_plat_hash_hex(uint64_t h, char out[17]) {
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 15; i >= 0; i--) {
        out[i] = hex[h & 15u];
        h >>= 4;
    }
    out[16] = '\0';
}

static int parse_hash_name(const char *name, uint64_t *h) {
    uint64_t v = 0;
    int i;
    for (i = 0; i < 16; i++) {
        char c = (char)tolower((unsigned char)name[i]);
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint64_t)(c - 'a' + 10);
        else return 0;
    }
    if (name[16] != '.') return 0;
    *h = v;
    return 1;
}

/* --------------------------------------------------------- directories */

typedef void (*DirFn)(void *user, const char *name, int is_dir);

static int list_dir(const char *path, DirFn fn, void *user) {
#ifdef _WIN32
    char pattern[700];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pattern, sizeof pattern, "%s\\*", path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.cFileName[0] == '.') continue;
        fn(user, fd.cFileName,
           (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 1;
#else
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        char full[1200];
        struct stat st;
        if (e->d_name[0] == '.') continue;
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        if (stat(full, &st) != 0) continue;
        fn(user, e->d_name, S_ISDIR(st.st_mode));
    }
    closedir(d);
    return 1;
#endif
}

static void copy_str(char *dst, size_t size, const char *src) {
    size_t n = strlen(src);
    if (n >= size) n = size - 1u;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ----------------------------------------------------------- manifests */

static int manifest_cb(void *user, const char *section, const char *key,
                       const char *value, int line) {
    BfmPlatModInfo *m = (BfmPlatModInfo *)user;
    long v;
    (void)line;
    if (strcmp(section, "mod") != 0) return 1;
    if (strcmp(key, "name") == 0) copy_str(m->name, sizeof m->name, value);
    else if (strcmp(key, "version") == 0) copy_str(m->version, sizeof m->version, value);
    else if (strcmp(key, "plugin") == 0) copy_str(m->plugin, sizeof m->plugin, value);
    else if (strcmp(key, "script") == 0) copy_str(m->script, sizeof m->script, value);
    else if (strcmp(key, "priority") == 0 && bfm_plat_ini_long(value, &v) &&
             v >= -100000 && v <= 100000)
        m->priority = (int)v;
    else if (strcmp(key, "enabled") == 0) {
        int b;
        if (bfm_plat_ini_bool(value, &b)) m->enabled = b;
    }
    return 1;
}

typedef struct ScanCtx {
    const char *root;
} ScanCtx;

static void scan_mod_folder(void *user, const char *name, int is_dir) {
    ScanCtx *ctx = (ScanCtx *)user;
    BfmPlatModInfo m;
    char manifest[1200];
    unsigned i;
    if (!is_dir) return;
    if (mod_count >= BFM_PLAT_MODS_MAX) {
        bfm_plat_mods_log("mods", 1, "too many mods; ignoring the rest");
        return;
    }
    memset(&m, 0, sizeof m);
    m.priority = 100;
    m.enabled = 1;
    copy_str(m.folder, sizeof m.folder, name);
    copy_str(m.name, sizeof m.name, name);
    if (snprintf(m.dir, sizeof m.dir, "%s/%s", ctx->root, name) >=
        (int)sizeof m.dir)
        return;
    snprintf(manifest, sizeof manifest, "%s/mod.ini", m.dir);
    if (bfm_plat_ini_parse_file(manifest, manifest_cb, &m) < 0) return;
    for (i = 0; i < mod_count; i++) {
        if (strcmp(mods[i].name, m.name) == 0) {
            logf_("mods", 1, "duplicate mod name '%s' ignored", m.name);
            return;
        }
    }
    mods[mod_count++] = m;
}

static int mod_order(const void *a, const void *b) {
    const BfmPlatModInfo *x = (const BfmPlatModInfo *)a;
    const BfmPlatModInfo *y = (const BfmPlatModInfo *)b;
    if (x->priority != y->priority) return x->priority < y->priority ? -1 : 1;
    {
        int c = strcmp(x->folder, y->folder);
        return c ? c : strcmp(x->dir, y->dir);
    }
}

/* -------------------------------------------------------------- assets */

typedef struct AssetScan {
    int mod;
    int kind;
    const char *dir;
} AssetScan;

static void scan_asset(void *user, const char *name, int is_dir) {
    AssetScan *s = (AssetScan *)user;
    uint64_t h;
    Asset *a;
    if (is_dir || !parse_hash_name(name, &h)) return;
    if (asset_count == asset_cap) {
        size_t cap = asset_cap ? asset_cap * 2u : 64u;
        Asset *n = (Asset *)realloc(assets, cap * sizeof *n);
        if (!n) return;
        assets = n;
        asset_cap = cap;
    }
    a = &assets[asset_count];
    memset(a, 0, sizeof *a);
    a->kind = s->kind;
    a->hash = h;
    a->mod = s->mod;
    if (snprintf(a->path, sizeof a->path, "%s/%s", s->dir, name) >=
        (int)sizeof a->path)
        return;
    asset_count++;
    if (s->kind == KIND_TEXTURES) mods[s->mod].textures++;
    else mods[s->mod].files++;
}

/* Last registration (highest load order) wins. */
static Asset *find_asset(int kind, uint64_t h) {
    size_t i = asset_count;
    while (i-- > 0)
        if (assets[i].kind == kind && assets[i].hash == h) return &assets[i];
    return NULL;
}

static int read_whole(const char *path, uint8_t **out, size_t *size) {
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return 0;
    }
    buf = (uint8_t *)malloc(n ? (size_t)n : 1u);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = buf;
    *size = (size_t)n;
    return 1;
}

static int decode_bfmi(const char *path, uint32_t *w, uint32_t *h,
                       uint8_t **rgba) {
    uint8_t *raw;
    size_t size;
    uint32_t ww, hh;
    if (!read_whole(path, &raw, &size)) return BFM_PLAT_NOT_FOUND;
    if (size < 8 || memcmp(raw, "BFMI", 4) != 0) {
        free(raw);
        return BFM_PLAT_INVALID;
    }
    ww = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8);
    hh = (uint32_t)raw[6] | ((uint32_t)raw[7] << 8);
    if (ww == 0 || hh == 0 || size != 8u + (size_t)ww * hh * 4u) {
        free(raw);
        return BFM_PLAT_INVALID;
    }
    memmove(raw, raw + 8, size - 8u);
    *w = ww;
    *h = hh;
    *rgba = raw;
    return BFM_PLAT_OK;
}

static BfmPlatTextureDecoder decoder_for(const char *path) {
    const char *dot = strrchr(path, '.');
    unsigned i;
    if (!dot) return NULL;
    for (i = decoder_count; i-- > 0;)
        if (strcmp(decoders[i].ext, dot) == 0) return decoders[i].fn;
    if (strcmp(dot, ".bfmi") == 0) return decode_bfmi;
    return NULL;
}

int bfm_plat_mods_register_decoder(const char *ext, BfmPlatTextureDecoder fn) {
    unsigned i;
    if (!ext || ext[0] != '.' || strlen(ext) >= 16 || !fn) return BFM_PLAT_INVALID;
    for (i = 0; i < decoder_count; i++) {
        if (strcmp(decoders[i].ext, ext) == 0) {   /* re-register replaces */
            decoders[i].fn = fn;
            return BFM_PLAT_OK;
        }
    }
    if (decoder_count >= MAX_DECODERS) return BFM_PLAT_NO_SPACE;
    copy_str(decoders[decoder_count].ext, sizeof decoders[0].ext, ext);
    decoders[decoder_count++].fn = fn;
    return BFM_PLAT_OK;
}

int bfm_plat_mods_replace_asset(const char *kind, const void *data,
                                size_t size, const uint8_t **out,
                                size_t *out_size) {
    Asset *a;
    if (!kind || !out || !out_size || (size && !data)) return BFM_PLAT_INVALID;
    if (strcmp(kind, "files") != 0 || asset_count == 0) return BFM_PLAT_NOT_FOUND;
    a = find_asset(KIND_FILES, bfm_plat_hash64(data, size));
    if (!a) return BFM_PLAT_NOT_FOUND;
    if (a->loaded == 0)
        a->loaded = read_whole(a->path, &a->data, &a->size) ? 1 : -1;
    if (a->loaded < 0) return BFM_PLAT_NOT_FOUND;
    *out = a->data;
    *out_size = a->size;
    return BFM_PLAT_OK;
}

int bfm_plat_mods_replace_texture(const BfmPlatRect *r, const uint16_t *px,
                                  BfmPlatImage *out) {
    Asset *a;
    if (!r || !px || !out) return BFM_PLAT_INVALID;
    if (asset_count == 0) return BFM_PLAT_NOT_FOUND;
    a = find_asset(KIND_TEXTURES, bfm_plat_texture_hash(r, px));
    if (!a) return BFM_PLAT_NOT_FOUND;
    if (a->loaded == 0) {
        BfmPlatTextureDecoder dec = decoder_for(a->path);
        a->loaded = -1;
        if (dec && dec(a->path, &a->width, &a->height, &a->data) == BFM_PLAT_OK) {
            /* Must be the original size or an integer multiple of it. */
            if (a->width % (uint32_t)r->w == 0 && a->height % (uint32_t)r->h == 0 &&
                a->width / (uint32_t)r->w == a->height / (uint32_t)r->h)
                a->loaded = 1;
            else
                logf_(mods[a->mod].name, 1, "texture %s has a bad size; ignored",
                      a->path);
        }
    }
    if (a->loaded < 0) return BFM_PLAT_NOT_FOUND;
    out->width = a->width;
    out->height = a->height;
    out->rgba = a->data;
    return BFM_PLAT_OK;
}

static void make_dir(const char *p) {
#ifdef _WIN32
    CreateDirectoryA(p, NULL);
#else
    mkdir(p, 0755);
#endif
}

void bfm_plat_mods_observe_texture(const BfmPlatRect *r, const uint16_t *px) {
    uint64_t h;
    size_t i, n;
    char path[700], hex[17];
    FILE *f;
    if (!dump_textures || !r || !px) return;
    h = bfm_plat_texture_hash(r, px);
    for (i = 0; i < dumped_count; i++)
        if (dumped[i] == h) return;
    if (dumped_count == dumped_cap) {
        size_t cap = dumped_cap ? dumped_cap * 2u : 256u;
        uint64_t *nd = (uint64_t *)realloc(dumped, cap * sizeof *nd);
        if (!nd) return;
        dumped = nd;
        dumped_cap = cap;
    }
    dumped[dumped_count++] = h;
    bfm_plat_hash_hex(h, hex);
    make_dir(dump_dir);
    snprintf(path, sizeof path, "%s/textures", dump_dir);
    make_dir(path);
    snprintf(path, sizeof path, "%s/textures/%s.bfmi", dump_dir, hex);
    f = fopen(path, "wb");
    if (!f) return;
    {
        uint8_t hdr[8] = {'B', 'F', 'M', 'I', 0, 0, 0, 0};
        hdr[4] = (uint8_t)r->w; hdr[5] = (uint8_t)((uint16_t)r->w >> 8);
        hdr[6] = (uint8_t)r->h; hdr[7] = (uint8_t)((uint16_t)r->h >> 8);
        fwrite(hdr, 1, 8, f);
    }
    n = (size_t)r->w * (size_t)r->h;
    for (i = 0; i < n; i++) {
        /* 15-bit BGR555 + mask bit -> RGBA8888 (5-bit expanded). */
        uint16_t c = px[i];
        uint8_t rgba[4];
        rgba[0] = (uint8_t)(((c & 31u) << 3) | ((c & 31u) >> 2));
        rgba[1] = (uint8_t)((((c >> 5) & 31u) << 3) | (((c >> 5) & 31u) >> 2));
        rgba[2] = (uint8_t)((((c >> 10) & 31u) << 3) | (((c >> 10) & 31u) >> 2));
        rgba[3] = c == 0 ? 0 : 255;
        fwrite(rgba, 1, 4, f);
    }
    fclose(f);
}

/* ------------------------------------------------------------- cheats */

typedef struct CheatParse {
    int mod;
    char section[BFM_PLAT_INI_LINE_MAX];
    BfmCheat cheat;
    char name[64];
    char desc[128];
    int enabled;
    int have;
    int has_copy;
    uint32_t copy_from;
} CheatParse;

static void flush_cheat(CheatParse *p) {
    if (!p->have) return;
    p->cheat.name = p->name;
    p->cheat.description = p->desc;
    if (p->has_copy && (p->cheat.width == 1 || p->cheat.width == 2 || p->cheat.width == 4) &&
        copy_cheat_count < BFM_PLAT_CHEATS_MAX) {
        CopyCheat *c = &copy_cheats[copy_cheat_count++];
        c->src = p->copy_from;
        c->dst = p->cheat.address;
        c->width = p->cheat.width;
        p->cheat.apply = copy_cheat_apply;
        p->cheat.user = c;
    }
    if (p->cheat.width == 1 || p->cheat.width == 2 || p->cheat.width == 4) {
        if (bfm_plat_cheat_register_copy(&p->cheat, p->mod) == BFM_PLAT_OK) {
            mods[p->mod].cheats++;
            if (p->enabled) bfm_plat_cheat_set(p->name, 1);
        }
    } else {
        logf_(mods[p->mod].name, 1, "cheat '%s' has a bad width; ignored", p->name);
    }
    p->have = 0;
}

static int cheat_cb(void *user, const char *section, const char *key,
                    const char *value, int line) {
    CheatParse *p = (CheatParse *)user;
    long v;
    (void)line;
    if (strcmp(section, p->section) != 0) {
        flush_cheat(p);
        copy_str(p->section, sizeof p->section, section);
        if (strncmp(section, "cheat ", 6) == 0 && section[6]) {
            memset(&p->cheat, 0, sizeof p->cheat);
            p->cheat.width = 4;
            copy_str(p->name, sizeof p->name, section + 6);
            p->desc[0] = '\0';
            p->enabled = 0;
            p->have = 1;
            p->has_copy = 0;
            p->copy_from = 0;
        }
    }
    if (!p->have) return 1;
    if (strcmp(key, "address") == 0 && bfm_plat_ini_long(value, &v))
        p->cheat.address = (uint32_t)v;
    else if (strcmp(key, "value") == 0 && bfm_plat_ini_long(value, &v))
        p->cheat.value = (uint32_t)v;
    else if (strcmp(key, "copy_from") == 0 && bfm_plat_ini_long(value, &v)) {
        p->copy_from = (uint32_t)v;
        p->has_copy = 1;
    }
    else if (strcmp(key, "width") == 0 && bfm_plat_ini_long(value, &v))
        p->cheat.width = (uint8_t)v;
    else if (strcmp(key, "description") == 0)
        copy_str(p->desc, sizeof p->desc, value);
    else if (strcmp(key, "enabled") == 0)
        bfm_plat_ini_bool(value, &p->enabled);
    return 1;
}

static void load_cheats(unsigned mod) {
    CheatParse p;
    char path[700];
    memset(&p, 0, sizeof p);
    p.mod = (int)mod;
    snprintf(path, sizeof path, "%s/cheats.ini", mods[mod].dir);
    if (bfm_plat_ini_parse_file(path, cheat_cb, &p) >= 0) flush_cheat(&p);
}

/* ------------------------------------------------------------ plugins */

static const char *host_config_get(const char *key);
static const char *(*config_getter)(const char *key);

static int host_register_command(const char *name, const char *help,
                                 BfmCommandFn fn, void *user) {
    return bfm_plat_console_register_owned(name, help, fn, user, loading_mod);
}

static int host_register_cheat(const BfmCheat *c) {
    return bfm_plat_cheat_register(c, loading_mod);
}

static const char *host_config_get(const char *key) {
    return config_getter ? config_getter(key) : NULL;
}

static void init_host(unsigned i) {
    BfmPluginHost *h = &hosts[i];
    memset(h, 0, sizeof *h);
    h->abi_version = BFM_PLUGIN_ABI_VERSION;
    h->size = (uint32_t)sizeof *h;
    h->mod_name = mods[i].name;
    h->mod_dir = mods[i].dir;
    h->log = bfm_plat_mods_log;
    h->subscribe = bfm_plat_mods_subscribe;
    h->unsubscribe = bfm_plat_mods_unsubscribe;
    h->guest_read = bfm_plat_guest_read;
    h->guest_write = bfm_plat_guest_write;
    h->register_cheat = host_register_cheat;
    h->set_cheat = bfm_plat_cheat_set;
    h->register_command = host_register_command;
    h->config_get = host_config_get;
    h->watch_add = bfm_plat_watch_add;
    h->watch_remove = bfm_plat_watch_remove;
}

const BfmPluginHost *bfm_plat_mods_host(unsigned i) {
    return i < mod_count ? &hosts[i] : NULL;
}

void bfm_plat_mods_set_config_getter(const char *(*fn)(const char *)) {
    config_getter = fn;
}

int bfm_plat_mods_loading(void) { return loading_mod; }

int bfm_plat_mods_register_static_plugin(const char *name, BfmPluginInitFn init,
                                         BfmPluginShutdownFn shutdown) {
    unsigned i;
    if (!name || !*name || strlen(name) >= BFM_PLAT_MOD_NAME_MAX || !init)
        return BFM_PLAT_INVALID;
    for (i = 0; i < static_count; i++)
        if (strcmp(statics[i].name, name) == 0) return BFM_PLAT_INVALID;
    if (static_count >= MAX_STATIC) return BFM_PLAT_NO_SPACE;
    copy_str(statics[static_count].name, sizeof statics[0].name, name);
    statics[static_count].init = init;
    statics[static_count].shutdown = shutdown;
    static_count++;
    return BFM_PLAT_OK;
}

int bfm_plat_mods_register_script_runtime(const BfmPlatScriptRuntime *rt) {
    unsigned i;
    if (!rt || !rt->name || !rt->extension || !rt->load) return BFM_PLAT_INVALID;
    for (i = 0; i < runtime_count; i++)
        if (runtimes[i] == rt || strcmp(runtimes[i]->name, rt->name) == 0) return BFM_PLAT_OK;
    if (runtime_count >= MAX_RUNTIMES) return BFM_PLAT_NO_SPACE;
    runtimes[runtime_count++] = rt;
    return BFM_PLAT_OK;
}

static int plugin_name_ok(const char *s) {
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '_' || *s == '-')) return 0;
    return 1;
}

static void load_plugin(unsigned i, int allow_native) {
    BfmPlatModInfo *m = &mods[i];
    BfmPluginInitFn init = NULL;
    BfmPluginShutdownFn shutdown = NULL;
    void *handle = NULL;
    BfmPluginInfo info;
    unsigned s;
    int r;
    if (!m->plugin[0]) return;
    m->plugin_loaded = -1;
    if (!plugin_name_ok(m->plugin)) {
        logf_(m->name, 1, "plugin name '%s' is not a plain file name", m->plugin);
        return;
    }
    for (s = 0; s < static_count; s++) {
        if (strcmp(statics[s].name, m->plugin) == 0) {
            init = statics[s].init;
            shutdown = statics[s].shutdown;
        }
    }
    if (!init && allow_native) {
        char path[700];
        snprintf(path, sizeof path, "%s/%s%s", m->dir, m->plugin,
                 BFM_PLUGIN_SUFFIX);
#ifdef _WIN32
        handle = (void *)LoadLibraryA(path);
        if (handle) {
            init = (BfmPluginInitFn)(void (*)(void))GetProcAddress(
                (HMODULE)handle, "bfm_plugin_init");
            shutdown = (BfmPluginShutdownFn)(void (*)(void))GetProcAddress(
                (HMODULE)handle, "bfm_plugin_shutdown");
        }
#else
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            /* POSIX: object -> function pointer through a union. */
            union { void *obj; BfmPluginInitFn fn; } ui;
            union { void *obj; BfmPluginShutdownFn fn; } us;
            ui.obj = dlsym(handle, "bfm_plugin_init");
            us.obj = dlsym(handle, "bfm_plugin_shutdown");
            init = ui.fn;
            shutdown = us.fn;
        } else {
            logf_(m->name, 1, "plugin load failed: %s", dlerror());
        }
#endif
    }
    if (!init) {
        logf_(m->name, 1, "plugin '%s' not found or has no bfm_plugin_init",
              m->plugin);
        goto fail;
    }
    memset(&info, 0, sizeof info);
    loading_mod = (int)i;
    r = init(&hosts[i], &info);
    loading_mod = -1;
    if (r != 0) {
        logf_(m->name, 1, "plugin '%s' init refused", m->plugin);
        goto fail;
    }
    if (info.abi_version == 0 || info.abi_version > BFM_PLUGIN_ABI_VERSION) {
        logf_(m->name, 1, "plugin '%s' needs a newer ABI", m->plugin);
        if (shutdown) shutdown();
        goto fail;
    }
    plugins[i].handle = handle;
    plugins[i].shutdown = shutdown;
    m->plugin_loaded = 1;
    return;
fail:
#ifdef _WIN32
    if (handle) FreeLibrary((HMODULE)handle);
#else
    if (handle) dlclose(handle);
#endif
    return;
}

static void load_script(unsigned i) {
    BfmPlatModInfo *m = &mods[i];
    const char *dot;
    unsigned r;
    char path[700];
    if (!m->script[0]) return;
    m->script_loaded = -1;
    dot = strrchr(m->script, '.');
    if (strstr(m->script, "..") || strchr(m->script, '/') || strchr(m->script, '\\')) {
        logf_(m->name, 1, "script '%s' must be a file in the mod folder", m->script);
        return;
    }
    snprintf(path, sizeof path, "%s/%s", m->dir, m->script);
    for (r = 0; dot && r < runtime_count; r++) {
        if (strcmp(runtimes[r]->extension, dot) == 0) {
            loading_mod = (int)i;
            m->script_loaded =
                runtimes[r]->load(&hosts[i], path) == BFM_PLAT_OK ? 1 : -1;
            loading_mod = -1;
            return;
        }
    }
    logf_(m->name, 1, "no script runtime for '%s' (scripting not built in)",
          m->script);
}

/* ----------------------------------------------------------- lifecycle */

int bfm_plat_mods_init(const BfmPlatModsOptions *opt) {
    char dirs[1024];
    char *tok, *rest;
    unsigned i, enabled = 0;
    if (initialized) bfm_plat_mods_shutdown();
    log_fn = opt ? opt->log : NULL;
    dump_textures = opt && opt->dump_textures;
    copy_str(dump_dir, sizeof dump_dir, opt && opt->dump_dir ? opt->dump_dir : "dump");
    mod_count = 0;
    initialized = 1;
    first_mod_sub_id = next_sub_id;
    first_mod_watch_id = bfm_plat_watch_next_id();
    copy_cheat_count = 0;
    copy_str(dirs, sizeof dirs, opt && opt->dirs ? opt->dirs : "mods");
    for (tok = dirs; tok && *tok; tok = rest) {
        ScanCtx ctx;
        rest = strchr(tok, ';');
        if (rest) *rest++ = '\0';
        if (!*tok) continue;
        ctx.root = tok;
        list_dir(tok, scan_mod_folder, &ctx);
    }
    qsort(mods, mod_count, sizeof mods[0], mod_order);
    for (i = 0; i < mod_count; i++) {
        unsigned k;
        init_host(i);
        if (!mods[i].enabled) continue;
        enabled++;
        for (k = 0; k < KIND_COUNT; k++) {
            char path[700];
            AssetScan s;
            snprintf(path, sizeof path, "%s/assets/%s", mods[i].dir, kind_names[k]);
            s.mod = (int)i;
            s.kind = (int)k;
            s.dir = path;
            list_dir(path, scan_asset, &s);
        }
        load_cheats(i);
        load_plugin(i, opt ? opt->allow_plugins : 0);
        load_script(i);
    }
    bfm_plat_mods_emit(BFM_EVENT_BOOT, NULL);
    return (int)enabled;
}

void bfm_plat_mods_shutdown(void) {
    size_t i;
    unsigned r;
    if (!initialized) return;
    bfm_plat_mods_emit(BFM_EVENT_SHUTDOWN, NULL);
    for (r = 0; r < runtime_count; r++)
        if (runtimes[r]->shutdown) runtimes[r]->shutdown();
    for (i = mod_count; i-- > 0;) {
        if (plugins[i].shutdown) plugins[i].shutdown();
#ifdef _WIN32
        if (plugins[i].handle) FreeLibrary((HMODULE)plugins[i].handle);
#else
        if (plugins[i].handle) dlclose(plugins[i].handle);
#endif
        plugins[i].handle = NULL;
        plugins[i].shutdown = NULL;
    }
    for (i = 0; i < sub_count; i++)
        if (subs[i].id >= first_mod_sub_id) subs[i].fn = NULL;
    bfm_plat_cheat_drop_owned();
    bfm_plat_console_drop_owned();
    bfm_plat_watch_drop_from(first_mod_watch_id);
    for (i = 0; i < asset_count; i++) free(assets[i].data);
    free(assets);
    assets = NULL;
    asset_count = asset_cap = 0;
    free(dumped);
    dumped = NULL;
    dumped_count = dumped_cap = 0;
    mod_count = 0;
    initialized = 0;
}

unsigned bfm_plat_mods_count(void) { return mod_count; }

const BfmPlatModInfo *bfm_plat_mods_get(unsigned i) {
    return i < mod_count ? &mods[i] : NULL;
}

void bfm_plat_mods_reset(void) {
    bfm_plat_mods_shutdown();
    sub_count = 0;
    next_sub_id = 1;
    memset(event_counts, 0, sizeof event_counts);
    static_count = 0;
    decoder_count = 0;
    runtime_count = 0;
    bfm_plat_guest_memory_bind(NULL);
    bfm_plat_guest_regs_bind(NULL);
    log_fn = NULL;
    config_getter = NULL;
    dump_textures = 0;
}
