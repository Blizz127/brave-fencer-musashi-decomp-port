#ifndef BFM_PLAT_INI_H
#define BFM_PLAT_INI_H

/* Minimal INI reader shared by the user config and mod manifests.
 *
 * Grammar: "[section]" headers, "key = value" pairs, ';' or '#' comments at
 * line start, surrounding whitespace trimmed. Keys before any header have
 * section "". Section names may contain spaces ("[cheat Infinite HP]").
 * Lines longer than BFM_PLAT_INI_LINE_MAX are refused (callback gets
 * nothing for them and the parse result reports the line). */

#include <stddef.h>

#define BFM_PLAT_INI_LINE_MAX 512

/* Return nonzero to continue, zero to stop the parse. */
typedef int (*BfmPlatIniFn)(void *user, const char *section, const char *key,
                            const char *value, int line);

/* Returns 0 on success, or the first malformed line number (> 0). Stops early
 * without error if the callback returns zero. */
int bfm_plat_ini_parse_string(const char *text, BfmPlatIniFn fn, void *user);
/* Returns 0 on success, -1 if the file can't be opened, or the first
 * malformed line number. */
int bfm_plat_ini_parse_file(const char *path, BfmPlatIniFn fn, void *user);

/* Value helpers. Return 1 when the text was a valid value of that type. */
int bfm_plat_ini_bool(const char *text, int *out);
int bfm_plat_ini_long(const char *text, long *out);
int bfm_plat_ini_double(const char *text, double *out);

#endif
