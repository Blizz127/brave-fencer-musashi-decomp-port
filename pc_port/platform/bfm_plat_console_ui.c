#include "bfm_plat_console_ui.h"
#include "bfm_plat_console.h"
#include "bfm_plat_renderer.h"

#include <stdio.h>
#include <string.h>

static char lines[BFM_CONSOLE_UI_LINES][BFM_CONSOLE_UI_COLS + 1];
static unsigned line_head;   /* next slot */
static unsigned line_count;
static char history[BFM_CONSOLE_UI_HISTORY][BFM_CONSOLE_UI_COLS + 1];
static unsigned hist_count, hist_pos;
static char edit[BFM_CONSOLE_UI_COLS + 1];
static unsigned edit_len, cursor;
static unsigned scroll;
static int open_;

void bfm_plat_console_ui_open(int o) {
    open_ = o != 0;
    bfm_plat_input_suppress(open_);
    if (open_) {
        BfmPlatTextEvent drop[16];
        /* Drop whatever was typed before opening (e.g. the toggle key). */
        while (bfm_plat_input_take_events(drop, 16) == 16) {}
    }
}

int bfm_plat_console_ui_is_open(void) { return open_; }
void bfm_plat_console_ui_toggle(void) { bfm_plat_console_ui_open(!open_); }

static void push_line(const char *s, size_t n) {
    if (n > BFM_CONSOLE_UI_COLS) n = BFM_CONSOLE_UI_COLS;
    memcpy(lines[line_head], s, n);
    lines[line_head][n] = '\0';
    line_head = (line_head + 1u) % BFM_CONSOLE_UI_LINES;
    if (line_count < BFM_CONSOLE_UI_LINES) line_count++;
}

void bfm_plat_console_ui_print(const char *text) {
    const char *p = text;
    if (!text) return;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        do {
            size_t take = n > BFM_CONSOLE_UI_COLS ? BFM_CONSOLE_UI_COLS : n;
            push_line(p, take);
            p += take;
            n -= take;
        } while (n);
        if (!nl) break;
        p = nl + 1;
        if (!*p) break;   /* trailing newline: no empty line */
    }
    scroll = 0;
}

unsigned bfm_plat_console_ui_line_count(void) { return line_count; }

const char *bfm_plat_console_ui_line(unsigned i) {
    if (i >= line_count) return NULL;
    return lines[(line_head + BFM_CONSOLE_UI_LINES - 1u - i) % BFM_CONSOLE_UI_LINES];
}

const char *bfm_plat_console_ui_input(void) { return edit; }
unsigned bfm_plat_console_ui_cursor(void) { return cursor; }

static void set_edit(const char *s) {
    snprintf(edit, sizeof edit, "%s", s);
    edit_len = (unsigned)strlen(edit);
    cursor = edit_len;
}

static void insert_text(const char *t) {
    for (; *t; t++) {
        unsigned char c = (unsigned char)*t;
        if (c < 0x20 || c > 0x7E || c == '`' || c == '~') continue; /* ASCII only */
        if (edit_len >= BFM_CONSOLE_UI_COLS) return;
        memmove(edit + cursor + 1, edit + cursor, edit_len - cursor + 1u);
        edit[cursor++] = (char)c;
        edit_len++;
    }
}

static void run_line(void) {
    char reply[2048];
    char echo[BFM_CONSOLE_UI_COLS + 3];
    if (edit_len == 0) return;
    snprintf(echo, sizeof echo, "> %s", edit);
    bfm_plat_console_ui_print(echo);
    if (hist_count == 0 || strcmp(history[(hist_count - 1u) % BFM_CONSOLE_UI_HISTORY], edit) != 0) {
        snprintf(history[hist_count % BFM_CONSOLE_UI_HISTORY], sizeof history[0], "%s", edit);
        hist_count++;
    }
    hist_pos = hist_count;
    bfm_plat_console_exec(edit, reply, sizeof reply);
    if (reply[0]) bfm_plat_console_ui_print(reply);
    set_edit("");
}

static void history_move(int d) {
    unsigned oldest = hist_count > BFM_CONSOLE_UI_HISTORY ? hist_count - BFM_CONSOLE_UI_HISTORY : 0;
    if (d < 0 && hist_pos > oldest) hist_pos--;
    else if (d > 0 && hist_pos < hist_count) hist_pos++;
    else return;
    set_edit(hist_pos == hist_count ? "" : history[hist_pos % BFM_CONSOLE_UI_HISTORY]);
}

static void complete(void) {
    char reply[2048];
    const char *p, *match = NULL;
    size_t mlen = 0;
    int matches = 0;
    if (strchr(edit, ' ') || edit_len == 0) return;
    /* The help listing is "name - help" per line. */
    bfm_plat_console_exec("help", reply, sizeof reply);
    for (p = reply; *p;) {
        const char *sp = strstr(p, " - ");
        const char *nl = strchr(p, '\n');
        if (!sp || !nl) break;
        if ((size_t)(sp - p) >= edit_len && strncmp(p, edit, edit_len) == 0) {
            matches++;
            match = p;
            mlen = (size_t)(sp - p);
        }
        p = nl + 1;
    }
    if (matches == 1 && mlen < BFM_CONSOLE_UI_COLS) {
        memcpy(edit, match, mlen);
        edit[mlen] = ' ';
        edit[mlen + 1] = '\0';
        edit_len = cursor = (unsigned)mlen + 1u;
    }
}

void bfm_plat_console_ui_event(const BfmPlatTextEvent *ev) {
    if (!ev || !open_) return;
    switch (ev->key) {
    case BFM_KEY_NONE: insert_text(ev->text); break;
    case BFM_KEY_ENTER: run_line(); break;
    case BFM_KEY_BACKSPACE:
        if (cursor) {
            memmove(edit + cursor - 1, edit + cursor, edit_len - cursor + 1u);
            cursor--;
            edit_len--;
        }
        break;
    case BFM_KEY_ESCAPE: bfm_plat_console_ui_open(0); break;
    case BFM_KEY_LEFT: if (cursor) cursor--; break;
    case BFM_KEY_RIGHT: if (cursor < edit_len) cursor++; break;
    case BFM_KEY_UP: history_move(-1); break;
    case BFM_KEY_DOWN: history_move(1); break;
    case BFM_KEY_PAGE_UP:
        if (line_count > BFM_CONSOLE_UI_VISIBLE) {
            scroll += BFM_CONSOLE_UI_VISIBLE / 2u;
            if (scroll > line_count - BFM_CONSOLE_UI_VISIBLE)
                scroll = line_count - BFM_CONSOLE_UI_VISIBLE;
        }
        break;
    case BFM_KEY_PAGE_DOWN:
        scroll = scroll > BFM_CONSOLE_UI_VISIBLE / 2u ? scroll - BFM_CONSOLE_UI_VISIBLE / 2u : 0;
        break;
    case BFM_KEY_TAB: complete(); break;
    default: break;
    }
}

void bfm_plat_console_ui_pump(void) {
    BfmPlatTextEvent evs[16];
    size_t n, i;
    while (open_ && (n = bfm_plat_input_take_events(evs, 16)) > 0)
        for (i = 0; i < n && open_; i++) bfm_plat_console_ui_event(&evs[i]);
}

void bfm_plat_console_ui_draw(void) {
    unsigned i, shown;
    char prompt[BFM_CONSOLE_UI_COLS + 4];
    if (!open_) return;
    shown = line_count < BFM_CONSOLE_UI_VISIBLE ? line_count : BFM_CONSOLE_UI_VISIBLE;
    for (i = 0; i < shown; i++) {
        const char *l = bfm_plat_console_ui_line(scroll + shown - 1u - i);
        if (l) bfm_plat_renderer_overlay_text(8, 8 + (int)i * 12, l);
    }
    /* Cursor shown as '_' inserted at its position. */
    snprintf(prompt, sizeof prompt, "> %.*s_%s", (int)cursor, edit, edit + cursor);
    bfm_plat_renderer_overlay_text(8, 8 + (int)shown * 12, prompt);
}

void bfm_plat_console_ui_reset(void) {
    memset(lines, 0, sizeof lines);
    line_head = line_count = 0;
    hist_count = hist_pos = 0;
    set_edit("");
    scroll = 0;
    open_ = 0;
    bfm_plat_input_suppress(0);
}
