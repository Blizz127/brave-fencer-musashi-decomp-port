#ifndef BFM_PLAT_FONT_H
#define BFM_PLAT_FONT_H

/* Built-in 5x7 debug font (ASCII 32..126, original to this repository) and
 * a text-to-primitive helper, so any renderer backend can draw the console,
 * cheat menu and "PAUSED" overlays without touching VRAM textures.
 * Glyph cells are 6x9 pixels at scale 1 (5x7 glyph + spacing). */

#include "bfm_plat_renderer.h"

#define BFM_FONT_GLYPH_W 5
#define BFM_FONT_GLYPH_H 7
#define BFM_FONT_CELL_W 6
#define BFM_FONT_CELL_H 9

/* 7 rows, bit 4 = leftmost pixel. Unknown characters map to '?'. */
const uint8_t *bfm_plat_font_glyph(char c);

/* Emits BFM_PRIM_TILE runs (one per horizontal pixel run) for `text` at
 * (x, y), scale 1..4, plus a semi-transparent black backing tile when
 * `backing` is set. Writes at most `max` prims; returns how many are needed
 * (so a caller can size its buffer by calling with max = 0). */
size_t bfm_plat_font_text_prims(int x, int y, const char *text, int scale,
                                uint8_t r, uint8_t g, uint8_t b, int backing,
                                BfmPlatPrim *out, size_t max);

#endif
