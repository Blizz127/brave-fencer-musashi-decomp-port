#ifndef BFM_PLAT_IMAGE_H
#define BFM_PLAT_IMAGE_H

/* Image output for screenshots and texture dumps: a dependency-free PNG
 * writer (stored deflate blocks, so no zlib needed) plus VRAM conversions. */

#include "bfm_plat_types.h"

/* channels: 3 = RGB8, 4 = RGBA8. */
int bfm_plat_png_write(const char *path, uint32_t width, uint32_t height,
                       unsigned channels, const uint8_t *pixels);

/* PS1 15-bit (BGR555 + STP) halfwords -> RGB8, 5-bit channels expanded. */
void bfm_plat_vram15_to_rgb(const uint16_t *px, size_t count, uint8_t *rgb);
/* 24-bit display mode: `row` holds width*3 bytes packed across halfwords. */
void bfm_plat_vram24_to_rgb(const uint16_t *row_halfwords, size_t width,
                            uint8_t *rgb);

uint32_t bfm_plat_crc32(uint32_t crc, const uint8_t *data, size_t size);

#endif
