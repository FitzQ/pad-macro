#include "config.h"
#include <switch.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CONF_FILE_PATH "sdmc:/config/pad-macro/config.ini"

// Internal storage
static u64 play_latest_btn = 0;
static u64 recorder_btn = 0;
static bool recorder_enable = false;
static bool player_enable = false;

typedef struct { u64 mask; char *path; } MacroEntry;
static MacroEntry *s_macros = NULL;
static size_t s_macros_count = 0, s_macros_cap = 0;

static void reset_state(void) {
    play_latest_btn = recorder_btn = 0;
    recorder_enable = player_enable = false;
    for (size_t i = 0; i < s_macros_count; ++i) free(s_macros[i].path);
    free(s_macros); s_macros = NULL; s_macros_count = s_macros_cap = 0;
}

static void trim(char *s) {
    if (!s) return;
    char *p = s; while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n] = 0;
}

static bool parse_bool(const char *v, bool *out) {
    if (!v||!out) return false;
    if (!strcasecmp(v, "true") || !strcasecmp(v, "1") || !strcasecmp(v, "yes") || !strcasecmp(v, "on")) { *out = true; return true; }
    if (!strcasecmp(v, "false")|| !strcasecmp(v, "0") || !strcasecmp(v, "no")  || !strcasecmp(v, "off")) { *out = false; return true; }
    return false;
}

static bool parse_u64_auto(const char *s, u64 *out) {
    if (!s||!out) return false;
    char *end=NULL; unsigned long long v = strtoull(s, &end, 0);
    if (end==s) return false;
    while (*end==' '||*end=='\t') ++end;
    if (*end!=0) return false;
    *out = (u64)v; return true;
}

static bool macros_push(u64 mask, const char *path) {
    if (!path) return false;
    if (s_macros_count == s_macros_cap) {
        size_t nc = s_macros_cap? s_macros_cap*2 : 8;
        MacroEntry *n = (MacroEntry*)realloc(s_macros, nc*sizeof(MacroEntry));
        if (!n) return false;
        s_macros = n; s_macros_cap = nc;
    }
    size_t len = strnlen(path, 2048);
    char *dup = (char*)malloc(len+1); if (!dup) return false;
    memcpy(dup, path, len); dup[len] = 0;
    s_macros[s_macros_count].mask = mask;
    s_macros[s_macros_count].path = dup;
    s_macros_count++;
    return true;
}

bool loadConfig(void) {
    reset_state();

    FILE *f = fopen(CONF_FILE_PATH, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (1<<20)) { fclose(f); return false; }
    char *buf = (char*)malloc((size_t)sz + 1); if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)sz, f); fclose(f); buf[rd] = 0;

    bool in_pad = false, in_macros = false;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        trim(line);
        if (!*line) continue;
        if (line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            in_pad = in_macros = false;
            char *end = strchr(line, ']'); if (!end) continue;
            *end = 0; char *sec = line+1; trim(sec);
            if (!strcasecmp(sec, "pad")) in_pad = true;
            else if (!strcasecmp(sec, "macros")) in_macros = true;
            continue;
        }

        if (in_pad) {
            char *eq = strchr(line, '='); if (!eq) continue;
            *eq = 0; char *k = line; char *v = eq+1; trim(k); trim(v);
            if (!*k || !*v) continue;
            if (!strcasecmp(k, "play_latest_btn")) { u64 x; if (parse_u64_auto(v, &x)) play_latest_btn = x; }
            else if (!strcasecmp(k, "recorder_btn")) { u64 x; if (parse_u64_auto(v, &x)) recorder_btn = x; }
            else if (!strcasecmp(k, "recorder_enable")) { bool b; if (parse_bool(v, &b)) recorder_enable = b; }
            else if (!strcasecmp(k, "player_enable")) { bool b; if (parse_bool(v, &b)) player_enable = b; }
            continue;
        }

        if (in_macros) {
            char *eq = strchr(line, '='); if (!eq) continue;
            *eq = 0; char *k = line; char *v = eq+1; trim(k); trim(v);
            if (!*k || !*v) continue;
            u64 mask; if (!parse_u64_auto(k, &mask)) continue;
            macros_push(mask, v);
            continue;
        }
    }

    free(buf);
    return true;
}

u64 getRecordButtonMask(void) { return recorder_btn; }
u64 getPlayLatestButtonMask(void) { return play_latest_btn; }
bool recorderEnable(void) { return recorder_enable; }
bool playerEnable(void) { return player_enable; }

bool maskMatch(u64 pressed, char **out_path) {
    if (!out_path) return false;
    for (size_t i = 0; i < s_macros_count; ++i) {
        if ((pressed & s_macros[i].mask) == s_macros[i].mask) {
            *out_path = s_macros[i].path;
            return true;
        }
    }
    return false;
}
