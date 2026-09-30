/* ".png" texture decoder for mods (in-house; inflate via zlib).
 *
 * Supports non-interlaced PNG: greyscale, RGB, palette (1/2/4/8-bit, with
 * tRNS), grey+alpha, RGBA, at bit depth 8 (16-bit samples keep the high
 * byte). All five scanline filters. CRCs are checked. Adam7 interlaced
 * files are refused. That covers what image editors write for texture
 * replacements.
 *
 * License: this file is part of the repository. It links zlib (zlib
 * license), the system libz. No third-party PNG code is vendored.
 * Build: -DBFM_PLAT_WITH_PNG, link -lz. */
#include "../bfm_plat.h"

#include <zlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PNG_MAX_DIM 8192u
#define PNG_MAX_FILE (64u * 1024u * 1024u)

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

int bfm_plat_texture_png_decode_memory(const uint8_t *data, size_t size,
                                       uint32_t *out_w, uint32_t *out_h,
                                       uint8_t **out_rgba);

int bfm_plat_texture_png_decode_memory(const uint8_t *data, size_t size,
                                       uint32_t *out_w, uint32_t *out_h,
                                       uint8_t **out_rgba) {
    static const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    uint32_t w = 0, h = 0, depth = 0, ctype = 0, channels, bpp, stride, y, x;
    uint8_t palette[256][4];
    unsigned pal_count = 0;
    int have_ihdr = 0, have_iend = 0, trns_grey = -1;
    int trns_rgb[3] = {-1, -1, -1};
    uint8_t *idat = NULL, *raw = NULL, *rgba = NULL;
    size_t idat_len = 0, pos = 8;
    uLongf raw_len;
    int r = BFM_PLAT_INVALID;

    if (!data || size < 8 || memcmp(data, sig, 8) != 0) return BFM_PLAT_INVALID;
    memset(palette, 255, sizeof palette);
    while (pos + 12 <= size && !have_iend) {
        uint32_t len = be32(data + pos);
        const uint8_t *type = data + pos + 4, *body = data + pos + 8;
        if (len > size - pos - 12) goto done;
        if (crc32(crc32(0L, Z_NULL, 0), type, len + 4u) != be32(body + len)) goto done;
        if (memcmp(type, "IHDR", 4) == 0) {
            if (len != 13 || have_ihdr) goto done;
            w = be32(body);
            h = be32(body + 4);
            depth = body[8];
            ctype = body[9];
            if (body[10] != 0 || body[11] != 0) goto done;
            if (body[12] != 0) { r = BFM_PLAT_UNSUPPORTED; goto done; }  /* Adam7 */
            have_ihdr = 1;
        } else if (!have_ihdr) {
            goto done;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            unsigned i;
            if (len % 3 || len / 3 > 256) goto done;
            pal_count = len / 3;
            for (i = 0; i < pal_count; i++) {
                palette[i][0] = body[i * 3];
                palette[i][1] = body[i * 3 + 1];
                palette[i][2] = body[i * 3 + 2];
            }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            unsigned i;
            if (ctype == 3) {
                for (i = 0; i < len && i < 256; i++) palette[i][3] = body[i];
            } else if (ctype == 0 && len == 2) {
                trns_grey = (body[0] << 8) | body[1];
            } else if (ctype == 2 && len == 6) {
                for (i = 0; i < 3; i++) trns_rgb[i] = (body[i * 2] << 8) | body[i * 2 + 1];
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            uint8_t *n = (uint8_t *)realloc(idat, idat_len + len + 1u);
            if (!n) { r = BFM_PLAT_ERROR; goto done; }
            idat = n;
            memcpy(idat + idat_len, body, len);
            idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            have_iend = 1;
        } else if (!(type[0] & 0x20)) {
            r = BFM_PLAT_UNSUPPORTED;   /* unknown critical chunk */
            goto done;
        }
        pos += 12u + len;
    }
    if (!have_ihdr || !have_iend || !idat || w == 0 || h == 0 ||
        w > PNG_MAX_DIM || h > PNG_MAX_DIM)
        goto done;
    switch (ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: goto done;
    }
    if (ctype == 3 ? !(depth == 1 || depth == 2 || depth == 4 || depth == 8) || !pal_count
                   : !(depth == 8 || depth == 16))
        goto done;
    stride = (w * channels * depth + 7u) / 8u;
    bpp = (channels * depth + 7u) / 8u;
    raw_len = (uLongf)(stride + 1u) * h;
    raw = (uint8_t *)malloc(raw_len);
    rgba = (uint8_t *)malloc((size_t)w * h * 4u);
    if (!raw || !rgba) { r = BFM_PLAT_ERROR; goto done; }
    {
        uLongf got = raw_len;
        if (uncompress(raw, &got, idat, (uLong)idat_len) != Z_OK || got != raw_len)
            goto done;
    }
    for (y = 0; y < h; y++) {
        uint8_t *line = raw + (size_t)y * (stride + 1u);
        uint8_t *cur = line + 1;
        const uint8_t *prev = y ? line - stride : NULL;
        uint8_t filter = line[0];
        for (x = 0; x < stride; x++) {
            int a = x >= bpp ? cur[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (filter) {
            case 0: break;
            case 1: cur[x] = (uint8_t)(cur[x] + a); break;
            case 2: cur[x] = (uint8_t)(cur[x] + b); break;
            case 3: cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1)); break;
            case 4: cur[x] = (uint8_t)(cur[x] + paeth(a, b, c)); break;
            default: goto done;
            }
        }
        for (x = 0; x < w; x++) {
            uint8_t *o = rgba + ((size_t)y * w + x) * 4u;
            unsigned step = depth == 16 ? 2u : 1u;
            if (ctype == 3) {
                unsigned bit = x * depth, idx;
                idx = (cur[bit / 8u] >> (8u - depth - bit % 8u)) & ((1u << depth) - 1u);
                if (idx >= pal_count) goto done;
                memcpy(o, palette[idx], 4);
            } else {
                const uint8_t *s = cur + (size_t)x * channels * step;
                int full;
                switch (ctype) {
                case 0:
                    o[0] = o[1] = o[2] = s[0];
                    full = step == 2 ? (s[0] << 8) | s[1] : s[0];
                    o[3] = full == trns_grey ? 0 : 255;
                    break;
                case 2:
                    o[0] = s[0]; o[1] = s[step]; o[2] = s[2 * step];
                    if (step == 2)
                        full = ((s[0] << 8 | s[1]) == trns_rgb[0] &&
                                (s[2] << 8 | s[3]) == trns_rgb[1] &&
                                (s[4] << 8 | s[5]) == trns_rgb[2]);
                    else
                        full = (s[0] == trns_rgb[0] && s[1] == trns_rgb[1] &&
                                s[2] == trns_rgb[2]);
                    o[3] = full ? 0 : 255;
                    break;
                case 4:
                    o[0] = o[1] = o[2] = s[0]; o[3] = s[step];
                    break;
                default:
                    o[0] = s[0]; o[1] = s[step]; o[2] = s[2 * step]; o[3] = s[3 * step];
                    break;
                }
            }
        }
    }
    *out_w = w;
    *out_h = h;
    *out_rgba = rgba;
    rgba = NULL;
    r = BFM_PLAT_OK;
done:
    free(idat);
    free(raw);
    free(rgba);
    return r;
}

static int png_decode_file(const char *path, uint32_t *w, uint32_t *h,
                           uint8_t **rgba) {
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;
    int r;
    if (!f) return BFM_PLAT_NOT_FOUND;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 ||
        (unsigned long)n > PNG_MAX_FILE || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return BFM_PLAT_INVALID;
    }
    buf = (uint8_t *)malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return BFM_PLAT_ERROR;
    }
    fclose(f);
    r = bfm_plat_texture_png_decode_memory(buf, (size_t)n, w, h, rgba);
    free(buf);
    return r;
}

int bfm_plat_texture_png_register(void);
int bfm_plat_texture_png_register(void) {
    return bfm_plat_mods_register_decoder(".png", png_decode_file);
}
