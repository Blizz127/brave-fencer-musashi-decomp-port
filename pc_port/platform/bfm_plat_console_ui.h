#ifndef BFM_PLAT_CONSOLE_UI_H
#define BFM_PLAT_CONSOLE_UI_H

/* Console text front end, backend-agnostic.
 *
 * A scrollback of text lines plus one edit line. It consumes the input
 * layer's text/key queue (bfm_plat_input_push_text/_key), runs lines through
 * bfm_plat_console_exec, and draws itself with
 * bfm_plat_renderer_overlay_text. So any renderer that implements
 * overlay_text shows it, and any input backend that pushes text feeds it.
 *
 * Keys: Enter runs, Backspace, Left/Right move the cursor, Up/Down history,
 * PageUp/PageDown scroll, Esc (or the console hotkey) closes, Tab completes
 * a command name. '`' and '~' are dropped (they are the usual toggle key). */

#include "bfm_plat_input.h"

#define BFM_CONSOLE_UI_COLS 96
#define BFM_CONSOLE_UI_LINES 128
#define BFM_CONSOLE_UI_HISTORY 32
#define BFM_CONSOLE_UI_VISIBLE 12

void bfm_plat_console_ui_open(int open);
int bfm_plat_console_ui_is_open(void);
void bfm_plat_console_ui_toggle(void);

/* Handles one event / drains the input queue (the frame loop calls
 * pump while open). */
void bfm_plat_console_ui_event(const BfmPlatTextEvent *ev);
void bfm_plat_console_ui_pump(void);

/* Appends text to the scrollback (split on '\n', wrapped at the column
 * width). Also usable as a log sink. */
void bfm_plat_console_ui_print(const char *text);

/* Draws scrollback + prompt via the renderer overlay (no-op if closed).
 * Layout: x=8, from y=8, 12 px per line. */
void bfm_plat_console_ui_draw(void);

/* Inspection (tests, other front ends). Line 0 is the newest line. */
unsigned bfm_plat_console_ui_line_count(void);
const char *bfm_plat_console_ui_line(unsigned from_newest);
const char *bfm_plat_console_ui_input(void);
unsigned bfm_plat_console_ui_cursor(void);

void bfm_plat_console_ui_reset(void);

#endif
