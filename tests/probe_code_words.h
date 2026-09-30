/* Probe-side view of retail words, read at run time from the developer's own
 * extract (never compiled in). Include after "mips_formatter.c", then alias a
 * former word table to its words:
 *     #define kOverlaySc02_8013D53CWords PROBE_WORDS(0x8013d53cu, "SC02_031", kOverlaySc02_8013D53CWords)
 * "MAIN" reads MUSASHI_CODE_IMAGE (the local SLUS_007.26); any other image
 * name reads $MUSASHI_OVERLAY_DIR/<name>.bin loaded at the guest address in
 * $MUSASHI_OVERLAY_DIR/<name>.base (hex), written by the Python helper
 * tests/test_bios_event_callbacks._materialize_overlays. */
#ifndef PROBE_CODE_WORDS_H
#define PROBE_CODE_WORDS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProbeWords {
    struct ProbeWords *next;
    uint32_t base;
    const char *image;
    uint32_t *words;
} ProbeWords;

static __attribute__((unused)) const uint32_t *probe_words(uint32_t base, uint32_t count, const char *image) {
    static ProbeWords *cache;
    ProbeWords *entry;
    char path[4096];
    FILE *file = NULL;
    long offset;
    for (entry = cache; entry; entry = entry->next)
        if (entry->base == base && !strcmp(entry->image, image)) return entry->words;
    entry = calloc(1, sizeof(*entry));
    if (!entry) abort();
    entry->base = base;
    entry->image = image;
    entry->words = calloc(count ? count : 1u, sizeof(uint32_t));
    if (!entry->words) abort();
    if (!strcmp(image, "MAIN")) {
        const char *exe = getenv("MUSASHI_CODE_IMAGE");
        if (exe) file = fopen(exe, "rb");
        offset = (long)(base - 0x80010000u + 0x800u);
    } else {
        const char *dir = getenv("MUSASHI_OVERLAY_DIR");
        unsigned long load = 0;
        FILE *b;
        if (!dir) { fprintf(stderr, "probe: MUSASHI_OVERLAY_DIR unset for %s\n", image); abort(); }
        snprintf(path, sizeof path, "%s/%s.base", dir, image);
        b = fopen(path, "r");
        if (!b || fscanf(b, "%lx", &load) != 1) { fprintf(stderr, "probe: no %s\n", path); abort(); }
        fclose(b);
        snprintf(path, sizeof path, "%s/%s.bin", dir, image);
        file = fopen(path, "rb");
        offset = (long)(base - (uint32_t)load);
    }
    if (!file || fseek(file, offset, SEEK_SET) ||
        fread(entry->words, sizeof(uint32_t), count, file) != count) {
        fprintf(stderr, "probe: cannot read %u words at %08x from %s\n", count, base, image);
        abort();
    }
    fclose(file);
    entry->next = cache;
    cache = entry;
    return entry->words;
}

/* Load a whole image into guest RAM at its base (site admission verifies
 * each site group against the loaded code, so probes need the real image). */
#ifdef MUSASHI_BOOT_MEMORY_H
static __attribute__((unused)) void probe_load_image(MusashiBootMemory *memory, const char *image) {
    char path[4096];
    const char *dir = getenv("MUSASHI_OVERLAY_DIR");
    unsigned long load = 0;
    long size;
    FILE *b, *file;
    uint8_t *span;
    if (!dir) { fprintf(stderr, "probe: MUSASHI_OVERLAY_DIR unset for %s\n", image); abort(); }
    snprintf(path, sizeof path, "%s/%s.base", dir, image);
    b = fopen(path, "r");
    if (!b || fscanf(b, "%lx", &load) != 1) { fprintf(stderr, "probe: no %s\n", path); abort(); }
    fclose(b);
    snprintf(path, sizeof path, "%s/%s.bin", dir, image);
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET)) abort();
    span = musashi_boot_ram_span(memory, (uint32_t)load, (size_t)size);
    if (!span || fread(span, 1, (size_t)size, file) != (size_t)size) abort();
    fclose(file);
}
#endif

#define PROBE_WORDS(base, image, array) \
    probe_words((base), (uint32_t)(sizeof(array) / sizeof(uint32_t)), (image))
#endif
