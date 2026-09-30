#include "bfm_plat_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t bfm_plat_crc32(uint32_t crc, const uint8_t *p, size_t n) {
    static uint32_t table[256];
    static int ready;
    size_t i;
    if (!ready) {
        uint32_t c, k;
        for (i = 0; i < 256; i++) {
            c = (uint32_t)i;
            for (k = 0; k < 8; k++) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    for (i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int chunk(FILE *f, const char *type, const uint8_t *data, size_t n) {
    uint8_t hdr[8], crcb[4];
    uint32_t crc;
    put32(hdr, (uint32_t)n);
    memcpy(hdr + 4, type, 4);
    crc = bfm_plat_crc32(0, hdr + 4, 4);
    crc = bfm_plat_crc32(crc, data, n);
    put32(crcb, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) &&
           fwrite(crcb, 1, 4, f) == 4;
}

int bfm_plat_png_write(const char *path, uint32_t w, uint32_t h,
                       unsigned channels, const uint8_t *px) {
    static const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    uint8_t ihdr[13];
    size_t row, raw_len, blocks, z_len, i, pos = 0, done = 0;
    uint8_t *raw, *z;
    uint32_t a = 1, b = 0;
    FILE *f;
    int ok;
    if (!path || !px || w == 0 || h == 0 || w > 16384 || h > 16384 ||
        (channels != 3 && channels != 4))
        return BFM_PLAT_INVALID;
    row = (size_t)w * channels;
    raw_len = (row + 1u) * h;
    blocks = (raw_len + 65534u) / 65535u;
    z_len = 2u + raw_len + blocks * 5u + 4u;
    raw = (uint8_t *)malloc(raw_len);
    z = (uint8_t *)malloc(z_len);
    if (!raw || !z) { free(raw); free(z); return BFM_PLAT_ERROR; }
    for (i = 0; i < h; i++) {
        raw[i * (row + 1u)] = 0;   /* filter: none */
        memcpy(raw + i * (row + 1u) + 1u, px + i * row, row);
    }
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    while (done < raw_len) {
        size_t n = raw_len - done > 65535u ? 65535u : raw_len - done;
        z[pos++] = done + n == raw_len ? 1 : 0;       /* BFINAL, BTYPE=00 */
        z[pos++] = (uint8_t)n; z[pos++] = (uint8_t)(n >> 8);
        z[pos++] = (uint8_t)~n; z[pos++] = (uint8_t)(~n >> 8);
        memcpy(z + pos, raw + done, n);
        pos += n;
        done += n;
    }
    for (i = 0; i < raw_len; i++) {            /* adler32 */
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(z + pos, (b << 16) | a);
    pos += 4;
    put32(ihdr, w);
    put32(ihdr + 4, h);
    ihdr[8] = 8;
    ihdr[9] = channels == 4 ? 6 : 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    f = fopen(path, "wb");
    ok = f && fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) &&
         chunk(f, "IDAT", z, pos) && chunk(f, "IEND", NULL, 0);
    if (f && fclose(f) != 0) ok = 0;
    free(raw);
    free(z);
    if (!ok && f) remove(path);
    return ok ? BFM_PLAT_OK : BFM_PLAT_ERROR;
}

static uint8_t expand5(unsigned v) { return (uint8_t)((v << 3) | (v >> 2)); }

void bfm_plat_vram15_to_rgb(const uint16_t *px, size_t n, uint8_t *rgb) {
    size_t i;
    for (i = 0; i < n; i++) {
        rgb[i * 3] = expand5(px[i] & 31u);
        rgb[i * 3 + 1] = expand5((px[i] >> 5) & 31u);
        rgb[i * 3 + 2] = expand5((px[i] >> 10) & 31u);
    }
}

void bfm_plat_vram24_to_rgb(const uint16_t *row, size_t w, uint8_t *rgb) {
    size_t i;
    for (i = 0; i < w * 3u; i++) {
        uint16_t hw = row[i / 2u];
        rgb[i] = (uint8_t)((i & 1u) ? hw >> 8 : hw & 0xFFu);
    }
}
