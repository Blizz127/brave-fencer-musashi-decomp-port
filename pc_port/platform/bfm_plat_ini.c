#include "bfm_plat_ini.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

typedef struct IniState {
    char section[BFM_PLAT_INI_LINE_MAX];
    BfmPlatIniFn fn;
    void *user;
    int stopped;
} IniState;

/* Returns 0 ok, 1 malformed. */
static int parse_line(IniState *st, char *raw, int line) {
    char *s = trim(raw);
    char *eq;
    if (*s == '\0' || *s == ';' || *s == '#') return 0;
    if (*s == '[') {
        char *close = strchr(s, ']');
        char *name;
        if (!close || trim(close + 1)[0] != '\0') return 1;
        *close = '\0';
        name = trim(s + 1);
        strcpy(st->section, name);
        return 0;
    }
    eq = strchr(s, '=');
    if (!eq) return 1;
    *eq = '\0';
    {
        char *key = trim(s);
        char *value = trim(eq + 1);
        if (*key == '\0') return 1;
        if (st->fn && !st->fn(st->user, st->section, key, value, line))
            st->stopped = 1;
    }
    return 0;
}

int bfm_plat_ini_parse_string(const char *text, BfmPlatIniFn fn, void *user) {
    IniState st;
    char buf[BFM_PLAT_INI_LINE_MAX + 1];
    int line = 0, bad = 0;
    if (!text) return -1;
    st.section[0] = '\0';
    st.fn = fn;
    st.user = user;
    st.stopped = 0;
    while (*text && !st.stopped) {
        const char *nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        line++;
        if (len > BFM_PLAT_INI_LINE_MAX) {
            if (!bad) bad = line;
        } else {
            memcpy(buf, text, len);
            buf[len] = '\0';
            if (len && buf[len - 1] == '\r') buf[len - 1] = '\0';
            if (parse_line(&st, buf, line) && !bad) bad = line;
        }
        text += len;
        if (*text == '\n') text++;
    }
    return bad;
}

int bfm_plat_ini_parse_file(const char *path, BfmPlatIniFn fn, void *user) {
    FILE *f;
    long size;
    char *text;
    int result;
    if (!path || !(f = fopen(path, "rb"))) return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        size > (1L << 20) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    text = (char *)malloc((size_t)size + 1u);
    if (!text) {
        fclose(f);
        return -1;
    }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return -1;
    }
    fclose(f);
    text[size] = '\0';
    result = bfm_plat_ini_parse_string(text, fn, user);
    free(text);
    return result;
}

int bfm_plat_ini_bool(const char *t, int *out) {
    static const char *yes[] = {"1", "true", "yes", "on"};
    static const char *no[] = {"0", "false", "no", "off"};
    size_t i;
    char low[8];
    size_t n;
    if (!t || !out) return 0;
    n = strlen(t);
    if (n == 0 || n >= sizeof low) return 0;
    for (i = 0; i <= n; i++) low[i] = (char)tolower((unsigned char)t[i]);
    for (i = 0; i < 4; i++) {
        if (strcmp(low, yes[i]) == 0) { *out = 1; return 1; }
        if (strcmp(low, no[i]) == 0) { *out = 0; return 1; }
    }
    return 0;
}

int bfm_plat_ini_long(const char *t, long *out) {
    char *end;
    long v;
    if (!t || !out || !*t) return 0;
    errno = 0;
    v = strtol(t, &end, 0);
    if (errno || *end != '\0') return 0;
    *out = v;
    return 1;
}

int bfm_plat_ini_double(const char *t, double *out) {
    char *end;
    double v;
    if (!t || !out || !*t) return 0;
    errno = 0;
    v = strtod(t, &end);
    if (errno || *end != '\0') return 0;
    *out = v;
    return 1;
}
