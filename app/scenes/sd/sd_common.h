#pragma once

#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* ── Key="value" field extractor ────────────────────────────────────────────
 * Scans a response line for  key="value"  and copies the value into out.
 * Returns true on success.  Truncates silently if value >= out_len.         */
static inline bool sd_parse_field(
    const char* line, const char* key, char* out, size_t out_len)
{
    char search[68];
    snprintf(search, sizeof(search), "%s=\"", key);
    const char* p = strstr(line, search);
    if(!p) return false;
    p += strlen(search);
    const char* end = strchr(p, '"');
    if(!end) return false;
    size_t len = (size_t)(end - p);
    if(len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/* ── Path helpers ────────────────────────────────────────────────────────── */

/* Strip the last path component.  "/foo/bar" → "/foo",  "/foo" → "/". */
static inline void sd_path_up(char* path) {
    size_t len = strlen(path);
    if(len <= 1) return; /* already at root */
    if(path[len - 1] == '/') path[--len] = '\0'; /* strip trailing slash */
    char* last = strrchr(path, '/');
    if(last == path) {
        path[1] = '\0'; /* become "/" */
    } else if(last) {
        *last = '\0';
    }
}

/* Append dirname to path (path_len is sizeof(path)).
 * "/foo" + "bar" → "/foo/bar",  "/" + "bar" → "/bar". */
static inline void sd_path_enter(char* path, size_t path_len, const char* dirname) {
    size_t plen = strlen(path);
    if(plen > 1 && plen < path_len - 1) { /* not root and has room for '/' */
        path[plen++] = '/';
        path[plen]   = '\0';
    }
    size_t dlen  = strlen(dirname);
    size_t avail = path_len - plen - 1;
    if(dlen > avail) dlen = avail;
    memcpy(path + plen, dirname, dlen);
    path[plen + dlen] = '\0';
}
