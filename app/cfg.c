/* Reading and editing BepInEx .cfg files while keeping every other line untouched. */
#include "cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static void u2w(const char *s, wchar_t *out, int n) {
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n)) out[0] = 0;
}

static void trim(wchar_t *s) {
    wchar_t *a = s;
    while (*a == L' ' || *a == L'\t') a++;
    memmove(s, a, (wcslen(a) + 1) * sizeof(wchar_t));
    size_t k = wcslen(s);
    while (k && (s[k - 1] == L' ' || s[k - 1] == L'\t' || s[k - 1] == L'\r')) s[--k] = 0;
}

static char *read_all(const wchar_t *path, size_t *len) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(f, NULL), got = 0;
    char *b = malloc(sz + 1);
    if (b && !ReadFile(f, b, sz, &got, NULL)) got = 0;
    CloseHandle(f);
    if (b) b[got] = 0;
    *len = got;
    return b;
}

static int starts(const wchar_t *s, const wchar_t *p) { return !wcsncmp(s, p, wcslen(p)); }

int cfg_load(Cfg *c, const wchar_t *path) {
    memset(c, 0, sizeof *c);
    wcsncpy(c->path, path, MAX_PATH - 1);
    size_t len;
    char *t = read_all(path, &len);
    if (!t) return 0;
    char *p = t;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    c->crlf = strstr(p, "\r\n") != NULL;
    int cap = 64;
    c->lines = malloc(cap * sizeof(char *));
    for (;;) {
        char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l && p[l - 1] == '\r') l--;
        if (c->nlines == cap) c->lines = realloc(c->lines, (cap *= 2) * sizeof(char *));
        c->lines[c->nlines] = malloc(l + 1);
        memcpy(c->lines[c->nlines], p, l);
        c->lines[c->nlines++][l] = 0;
        if (!e) break;
        p = e + 1;
    }
    free(t);

    const wchar_t *fn = wcsrchr(path, L'\\');
    wcsncpy(c->title, fn ? fn + 1 : path, 127);
    wchar_t *dot = wcsrchr(c->title, L'.');
    if (dot) *dot = 0;

    CfgEntry cur = {0};
    wchar_t section[64] = L"";
    int ecap = 0;
    for (int i = 0; i < c->nlines; i++) {
        wchar_t w[1024];
        u2w(c->lines[i], w, 1024);
        trim(w);
        if (!w[0]) continue;
        if (starts(w, L"## Settings file was created by plugin ")) {
            wcsncpy(c->title, w + 39, 127);
            wchar_t *v = wcsstr(c->title, L" v");
            if (v) *v = 0;
        } else if (w[0] == L'[' && w[wcslen(w) - 1] == L']') {
            w[wcslen(w) - 1] = 0;
            wcsncpy(section, w + 1, 63);
            memset(&cur, 0, sizeof cur);
        } else if (starts(w, L"## ")) {
            if (cur.desc[0]) wcsncat(cur.desc, L" ", 255 - wcslen(cur.desc));
            wcsncat(cur.desc, w + 3, 255 - wcslen(cur.desc));
        } else if (starts(w, L"# Setting type: ")) {
            wcsncpy(cur.type, w + 16, 31);
        } else if (starts(w, L"# Default value: ")) {
            wcsncpy(cur.def, w + 17, 95);
        } else if (starts(w, L"# Acceptable values: ")) {
            wchar_t *ctx = NULL, *tok = wcstok(w + 21, L",", &ctx);
            for (; tok && cur.nopts < CFG_MAX_OPTS; tok = wcstok(NULL, L",", &ctx)) {
                wcsncpy(cur.opts[cur.nopts], tok, 47);
                trim(cur.opts[cur.nopts++]);
            }
        } else if (starts(w, L"# Acceptable value range: From ")) {
            double lo, hi;
            if (swscanf(w + 31, L"%lf to %lf", &lo, &hi) == 2) cur.has_range = 1, cur.lo = lo, cur.hi = hi;
        } else if (w[0] != L'#' && wcschr(w, L'=')) {
            wchar_t *eq = wcschr(w, L'=');
            *eq = 0;
            CfgEntry e = cur;
            wcsncpy(e.key, w, 95);
            trim(e.key);
            wcsncpy(e.value, eq + 1, 255);
            trim(e.value);
            wcscpy(e.section, section);
            e.line = i;
            if (c->n == ecap) c->e = realloc(c->e, (ecap = ecap ? ecap * 2 : 32) * sizeof(CfgEntry));
            c->e[c->n++] = e;
            memset(&cur, 0, sizeof cur);
        }
    }
    return 1;
}

int cfg_set(Cfg *c, int i, const wchar_t *value) {
    CfgEntry *e = &c->e[i];
    char key[300], val[600];
    WideCharToMultiByte(CP_UTF8, 0, e->key, -1, key, sizeof key, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, value, -1, val, sizeof val, NULL, NULL);
    char *nl = malloc(strlen(key) + strlen(val) + 4);
    sprintf(nl, "%s = %s", key, val);
    char *old = c->lines[e->line];
    c->lines[e->line] = nl;

    size_t total = 0;
    for (int k = 0; k < c->nlines; k++) total += strlen(c->lines[k]) + 2;
    char *buf = malloc(total + 1), *o = buf;
    for (int k = 0; k < c->nlines; k++) {
        size_t l = strlen(c->lines[k]);
        memcpy(o, c->lines[k], l);
        o += l;
        if (k + 1 < c->nlines) {
            if (c->crlf) *o++ = '\r';
            *o++ = '\n';
        }
    }
    /* write to a temp file first so a failed save can't leave a half-written config */
    wchar_t tmp[MAX_PATH + 8];
    swprintf(tmp, MAX_PATH + 8, L"%ls.tmp", c->path);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w = 0;
    BOOL ok = f != INVALID_HANDLE_VALUE && WriteFile(f, buf, (DWORD)(o - buf), &w, NULL) && w == (DWORD)(o - buf);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    ok = ok && MoveFileExW(tmp, c->path, MOVEFILE_REPLACE_EXISTING);
    free(buf);
    if (!ok) {
        DeleteFileW(tmp);
        c->lines[e->line] = old;
        free(nl);
        return 0;
    }
    free(old);
    wcsncpy(e->value, value, 255);
    return 1;
}

void cfg_free(Cfg *c) {
    for (int k = 0; k < c->nlines; k++) free(c->lines[k]);
    free(c->lines);
    free(c->e);
    memset(c, 0, sizeof *c);
}

int cfg_is_bool(const CfgEntry *e) {
    return !wcscmp(e->type, L"Boolean") || (!e->type[0] && (!_wcsicmp(e->value, L"true") || !_wcsicmp(e->value, L"false")));
}

int cfg_is_int(const CfgEntry *e) {
    return !wcscmp(e->type, L"Int32") || !wcscmp(e->type, L"Int64") || !wcscmp(e->type, L"Byte") || !wcscmp(e->type, L"Int16") ||
           !wcscmp(e->type, L"UInt32");
}

int cfg_list(const wchar_t *game, wchar_t (*paths)[MAX_PATH], wchar_t (*titles)[128], int max) {
    wchar_t pat[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\BepInEx\\config\\*.cfg", game);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (!_wcsicmp(fd.cFileName, L"BepInEx.cfg")) continue; /* the loader's own settings: not a mod */
        swprintf(paths[n], MAX_PATH, L"%ls\\BepInEx\\config\\%ls", game, fd.cFileName);
        Cfg c;
        if (!cfg_load(&c, paths[n])) continue;
        wcsncpy(titles[n], c.title, 127);
        int has = c.n > 0;
        cfg_free(&c);
        if (has) n++;
    } while (n < max && FindNextFileW(f, &fd));
    FindClose(f);
    return n;
}
