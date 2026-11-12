/* "Make bug report": grabs the logs that matter (HoofMods, every code mod, the game, crash reports, BepInEx)
   + a summary and zips them up so a player can just send one file. personal stuff gets taken out first:
   windows user name, pc name, steam id and anything that looks like an email */
#include <windows.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "report.h"
#include "release.h"

#define MAX_FILE (4u << 20) /* 4 MB per file, we keep the end if its bigger bc thats where the crash is */

/* ==================== tiny growing buffer */

typedef struct { char *p; size_t n, cap; } Buf;

static void put(Buf *b, const void *d, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 4096;
        while (c < b->n + n + 1) c *= 2;
        char *q = realloc(b->p, c);
        if (!q) return;
        b->p = q, b->cap = c;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
    b->p[b->n] = 0;
}

static void putf(Buf *b, const char *fmt, ...) {
    char tmp[2048];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, a);
    va_end(a);
    if (n > 0) put(b, tmp, n < (int)sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

static void put_wide(Buf *b, const wchar_t *w) { /* wide text in as utf8 */
    char tmp[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, tmp, sizeof tmp, NULL, NULL);
    if (n > 1) put(b, tmp, n - 1);
}

/* ==================== privacy scrub */

static char g_names[4][256]; /* utf8, things to swap out */
static const char *g_repl[4];
static int g_nnames;

static void add_name(const wchar_t *w, const char *repl) {
    if (!w || !w[0] || g_nnames >= 4) return;
    char u[256];
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL) <= 1) return;
    for (int i = 0; i < g_nnames; i++)
        if (!_stricmp(g_names[i], u)) return;
    strcpy(g_names[g_nnames], u);
    g_repl[g_nnames++] = repl;
}

static void setup_names(void) {
    g_nnames = 0;
    wchar_t v[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", v, MAX_PATH)) {
        wchar_t *sl = wcsrchr(v, L'\\');
        add_name(sl ? sl + 1 : v, "USER");
    }
    if (GetEnvironmentVariableW(L"USERNAME", v, MAX_PATH)) add_name(v, "USER");
    if (GetEnvironmentVariableW(L"COMPUTERNAME", v, MAX_PATH)) add_name(v, "PC");
}

static int is_mail_local(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr("._%+-", c)); }
static int is_mail_dom(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'; }
static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* emails first (they can have the user name in them), then steam ids, then names */
static void scrub(const char *in, size_t n, Buf *out) {
    Buf a = {0}, c = {0};
    /* emails */
    for (size_t i = 0; i < n;) {
        if (in[i] == '@') {
            size_t l = a.n; /* walk back over the part b4 the @ thats already in a */
            while (l > 0 && is_mail_local(a.p[l - 1])) l--;
            size_t r = i + 1, dot = 0;
            while (r < n && is_mail_dom(in[r])) {
                if (in[r] == '.') dot = r;
                r++;
            }
            while (r > i + 1 && (in[r - 1] == '.' || in[r - 1] == '-')) r--; /* no dot at the very end */
            if (l < a.n && dot > i + 1 && dot < r && r - dot - 1 >= 2 && is_alpha(in[r - 1])) {
                a.n = l;
                put(&a, "EMAIL", 5);
                i = r;
                continue;
            }
        }
        put(&a, in + i, 1);
        i++;
    }
    /* steam ids: 17 digits starting w/ 7656119 */
    for (size_t i = 0; i < a.n;) {
        if (a.p[i] >= '0' && a.p[i] <= '9' && (i == 0 || a.p[i - 1] < '0' || a.p[i - 1] > '9')) {
            size_t j = i;
            while (j < a.n && a.p[j] >= '0' && a.p[j] <= '9') j++;
            if (j - i == 17 && !strncmp(a.p + i, "7656119", 7)) {
                put(&c, "STEAMID", 7);
                i = j;
                continue;
            }
            put(&c, a.p + i, j - i);
            i = j;
            continue;
        }
        put(&c, a.p + i, 1);
        i++;
    }
    /* user + pc names, any case */
    for (size_t i = 0; i < c.n;) {
        int hit = -1;
        for (int k = 0; k < g_nnames && hit < 0; k++) {
            size_t L = strlen(g_names[k]);
            if (L < 2 || i + L > c.n || _strnicmp(c.p + i, g_names[k], L)) continue;
            /* short names (under 4 letters) only right after a slash so we dont wreck normal words */
            if (L < 4 && !(i > 0 && (c.p[i - 1] == '\\' || c.p[i - 1] == '/'))) continue;
            hit = k;
        }
        if (hit >= 0) {
            put(out, g_repl[hit], strlen(g_repl[hit]));
            i += strlen(g_names[hit]);
        } else {
            put(out, c.p + i, 1);
            i++;
        }
    }
    if (!out->p) put(out, "", 0);
    free(a.p);
    free(c.p);
}

/* ==================== zip writer (stored, no compression, thats fine for logs) */

typedef struct { char name[300]; unsigned crc, size, off; } ZEnt;
typedef struct { Buf data; ZEnt *e; int n, cap; } Zip;

static unsigned crc32(const unsigned char *p, size_t n) {
    static unsigned t[256];
    if (!t[1])
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void le16(Buf *b, unsigned v) { unsigned char x[2] = {v & 0xFF, (v >> 8) & 0xFF}; put(b, x, 2); }
static void le32(Buf *b, unsigned v) { unsigned char x[4] = {v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24}; put(b, x, 4); }

static unsigned g_dtime, g_ddate;

static void zip_add(Zip *z, const char *name, const char *d, size_t n) {
    for (int i = 0; i < z->n; i++)
        if (!_stricmp(z->e[i].name, name)) return; /* already got it */
    if (z->n == z->cap) {
        z->cap = z->cap ? z->cap * 2 : 32;
        z->e = realloc(z->e, z->cap * sizeof *z->e);
    }
    ZEnt *e = &z->e[z->n++];
    snprintf(e->name, sizeof e->name, "%s", name);
    e->crc = crc32((const unsigned char *)d, n);
    e->size = (unsigned)n;
    e->off = (unsigned)z->data.n;
    size_t nl = strlen(e->name);
    le32(&z->data, 0x04034b50), le16(&z->data, 20), le16(&z->data, 0x0800), le16(&z->data, 0);
    le16(&z->data, g_dtime), le16(&z->data, g_ddate), le32(&z->data, e->crc), le32(&z->data, e->size), le32(&z->data, e->size);
    le16(&z->data, (unsigned)nl), le16(&z->data, 0);
    put(&z->data, e->name, nl);
    put(&z->data, d, n);
}

static int zip_save(Zip *z, const wchar_t *path) {
    size_t cd = z->data.n;
    for (int i = 0; i < z->n; i++) {
        ZEnt *e = &z->e[i];
        size_t nl = strlen(e->name);
        le32(&z->data, 0x02014b50), le16(&z->data, 20), le16(&z->data, 20), le16(&z->data, 0x0800), le16(&z->data, 0);
        le16(&z->data, g_dtime), le16(&z->data, g_ddate), le32(&z->data, e->crc), le32(&z->data, e->size), le32(&z->data, e->size);
        le16(&z->data, (unsigned)nl), le16(&z->data, 0), le16(&z->data, 0), le16(&z->data, 0), le16(&z->data, 0);
        le32(&z->data, 0), le32(&z->data, e->off);
        put(&z->data, e->name, nl);
    }
    size_t cdn = z->data.n - cd;
    le32(&z->data, 0x06054b50), le16(&z->data, 0), le16(&z->data, 0), le16(&z->data, (unsigned)z->n), le16(&z->data, (unsigned)z->n);
    le32(&z->data, (unsigned)cdn), le32(&z->data, (unsigned)cd), le16(&z->data, 0);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    BOOL ok = WriteFile(f, z->data.p, (DWORD)z->data.n, &w, NULL) && w == z->data.n;
    CloseHandle(f);
    if (!ok) DeleteFileW(path);
    return ok;
}

/* ==================== collecting files */

static Buf g_skipped; /* files we couldnt read, goes in report.txt */

/* reads a file even while the game has it open. 0 = not there, minus 1 = there but couldnt read */
static int slurp_shared(const wchar_t *p, Buf *out, int *cut) {
    *cut = 0;
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ? 0 : -1;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz)) return CloseHandle(f), -1;
    DWORD want = sz.QuadPart > MAX_FILE ? MAX_FILE : (DWORD)sz.QuadPart;
    if (sz.QuadPart > MAX_FILE) {
        LARGE_INTEGER at;
        at.QuadPart = sz.QuadPart - MAX_FILE;
        SetFilePointerEx(f, at, NULL, FILE_BEGIN);
        *cut = 1;
    }
    char *d = malloc(want ? want : 1);
    DWORD got = 0;
    BOOL ok = d && ReadFile(f, d, want, &got, NULL);
    CloseHandle(f);
    if (!ok) return free(d), -1;
    /* utf16 files (some ini files) get turned into utf8 so the scrub works */
    if (got >= 2 && (unsigned char)d[0] == 0xFF && (unsigned char)d[1] == 0xFE) {
        int wn = (int)(got - 2) / 2;
        int un = WideCharToMultiByte(CP_UTF8, 0, (wchar_t *)(d + 2), wn, NULL, 0, NULL, NULL);
        char *u = malloc(un + 1);
        if (u) WideCharToMultiByte(CP_UTF8, 0, (wchar_t *)(d + 2), wn, u, un, NULL, NULL), put(out, u, un), free(u);
    } else {
        put(out, d, got);
    }
    free(d);
    if (!out->p) put(out, "", 0);
    return 1;
}

/* adds one file (scrubbed). quiet when its not there, noted when it wont open */
static int add_file(Zip *z, const wchar_t *path, const char *zname) {
    Buf raw = {0}, clean = {0};
    int cut;
    int r = slurp_shared(path, &raw, &cut);
    if (r <= 0) {
        if (r < 0) putf(&g_skipped, "  couldn't read: %s\r\n", zname);
        free(raw.p);
        return 0;
    }
    if (cut) put(&clean, "[file was too big, only the last 4 MB is here]\r\n", 49);
    scrub(raw.p, raw.n, &clean);
    zip_add(z, zname, clean.p, clean.n);
    free(raw.p);
    free(clean.p);
    return 1;
}

static void zname_of(char *out, size_t n, const char *folder, const wchar_t *file) {
    char f[260];
    WideCharToMultiByte(CP_UTF8, 0, file, -1, f, sizeof f, NULL, NULL);
    snprintf(out, n, "%s/%s", folder, f);
}

static int has_ext(const wchar_t *name, const wchar_t *const *exts) {
    const wchar_t *d = wcsrchr(name, L'.');
    if (!d) return 0;
    for (int i = 0; exts[i]; i++)
        if (!_wcsicmp(d, exts[i])) return 1;
    return 0;
}

static const wchar_t *LOG_EXT[] = {L".log", L".txt", L".ini", NULL};
static const wchar_t *CRASH_EXT[] = {L".log", L".txt", L".json", L".xml", L".ini", NULL};

/* every matching file right in dir (no subfolders) */
static void add_dir(Zip *z, const wchar_t *dir, const wchar_t *const *exts, const char *zfolder) {
    wchar_t pat[MAX_PATH], p[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!has_ext(fd.cFileName, exts)) continue;
        char zn[400];
        swprintf(p, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        zname_of(zn, sizeof zn, zfolder, fd.cFileName);
        add_file(z, p, zn);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* mod logs in %LOCALAPPDATA%\<something>, each folder only once */
static wchar_t g_seen[64][MAX_PATH];
static int g_nseen;

static void add_appdata(Zip *z, const wchar_t *name, const char *zfolder) {
    wchar_t dir[MAX_PATH], la[MAX_PATH];
    if (!name[0] || wcschr(name, L'\\') || wcsstr(name, L"..")) return;
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return;
    if (!_wcsicmp(name, L"HoofMods")) return; /* thats us, done separately */
    swprintf(dir, MAX_PATH, L"%ls\\%ls", la, name);
    for (int i = 0; i < g_nseen; i++)
        if (!_wcsicmp(g_seen[i], dir)) return;
    if (g_nseen < 64) wcscpy(g_seen[g_nseen++], dir);
    DWORD a = GetFileAttributesW(dir);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return;
    char zf[400], nm[260];
    WideCharToMultiByte(CP_UTF8, 0, name, -1, nm, sizeof nm, NULL, NULL);
    snprintf(zf, sizeof zf, "%s/LocalAppData %s", zfolder, nm);
    add_dir(z, dir, LOG_EXT, zf);
}

static void add_code_mods(Zip *z, Lib *L) {
    g_nseen = 0;
    if (!L->game[0]) return;
    wchar_t mods[MAX_PATH], pat[MAX_PATH];
    swprintf(mods, MAX_PATH, L"%ls\\Mods", L->game);
    swprintf(pat, MAX_PATH, L"%ls\\*", mods);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == L'.' || fd.cFileName[0] == L'_') continue;
            char zf[300], nm[260];
            WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nm, sizeof nm, NULL, NULL);
            snprintf(zf, sizeof zf, "mods/%s", nm);
            wchar_t dir[MAX_PATH], dpat[MAX_PATH];
            swprintf(dir, MAX_PATH, L"%ls\\%ls", mods, fd.cFileName);
            add_dir(z, dir, LOG_EXT, zf);
            add_appdata(z, fd.cFileName, zf);
            /* mods often log to a folder named after their dll, like BusyStud.dll -> %LOCALAPPDATA%\BusyStud */
            swprintf(dpat, MAX_PATH, L"%ls\\*.dll", dir);
            WIN32_FIND_DATAW dd;
            HANDLE hd = FindFirstFileW(dpat, &dd);
            if (hd != INVALID_HANDLE_VALUE) {
                do {
                    wchar_t base[MAX_PATH];
                    wcscpy(base, dd.cFileName);
                    wchar_t *dot = wcsrchr(base, L'.');
                    if (dot) *dot = 0;
                    add_appdata(z, base, zf);
                } while (FindNextFileW(hd, &dd));
                FindClose(hd);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    /* mods that load straight from the game folder (like StickHorse as version.dll) only have a name */
    for (int i = 0; i < L->n; i++) {
        Mod *m = &L->mods[i];
        if (m->kind != MK_DLL || m->id[0] != L'@') continue;
        wchar_t nm[128];
        int k = 0;
        for (const wchar_t *c = m->name; *c && k < 127; c++)
            if (*c != L' ') nm[k++] = *c;
        nm[k] = 0;
        char zf[300], u[200];
        WideCharToMultiByte(CP_UTF8, 0, m->name, -1, u, sizeof u, NULL, NULL);
        snprintf(zf, sizeof zf, "mods/%s (game folder)", u);
        add_appdata(z, m->name, zf);
        add_appdata(z, nm, zf);
    }
}

static void add_crash(Zip *z, Buf *rep) {
    wchar_t la[MAX_PATH], dir[MAX_PATH], pat[MAX_PATH], best[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return;
    swprintf(dir, MAX_PATH, L"%ls\\Temp\\ThogliStudios\\Horses of Hoofprint Bay\\Crashes", la);
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    FILETIME bt = {0};
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        putf(rep, "Newest crash report: none\r\n");
        return;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        if (CompareFileTime(&fd.ftLastWriteTime, &bt) > 0) bt = fd.ftLastWriteTime, wcscpy(best, fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!best[0]) {
        putf(rep, "Newest crash report: none\r\n");
        return;
    }
    SYSTEMTIME st, lt;
    FileTimeToSystemTime(&bt, &st);
    SystemTimeToTzSpecificLocalTime(NULL, &st, &lt);
    char nm[260], zf[300];
    WideCharToMultiByte(CP_UTF8, 0, best, -1, nm, sizeof nm, NULL, NULL);
    putf(rep, "Newest crash report: %s (%04d-%02d-%02d %02d:%02d)\r\n", nm, lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute);
    snprintf(zf, sizeof zf, "crash/%s", nm);
    wchar_t cd[MAX_PATH];
    swprintf(cd, MAX_PATH, L"%ls\\%ls", dir, best);
    add_dir(z, cd, CRASH_EXT, zf); /* text only, never crash.dmp */
}

/* ==================== report.txt */

static void win_version(Buf *b) {
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW *);
    RtlGetVersionFn f = (RtlGetVersionFn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW v = {sizeof v};
    if (f && !f(&v)) putf(b, "Windows: %lu.%lu build %lu\r\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    else putf(b, "Windows: unknown\r\n");
}

static void game_version(Buf *b, Lib *L) {
    wchar_t exe[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\Horses of Hoofprint Bay.exe", L->game);
    DWORD h = 0, n = L->game[0] ? GetFileVersionInfoSizeW(exe, &h) : 0;
    void *d = n ? malloc(n) : NULL;
    VS_FIXEDFILEINFO *fi = NULL;
    UINT fl = 0;
    if (d && GetFileVersionInfoW(exe, 0, n, d) && VerQueryValueW(d, L"\\", (void **)&fi, &fl) && fi)
        putf(b, "Game exe version: %u.%u.%u.%u\r\n", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS),
             LOWORD(fi->dwFileVersionLS));
    else putf(b, "Game exe version: unknown\r\n");
    free(d);
}

static const char *KIND_TXT[MK_COUNT] = {"Code mod", "BepInEx mod", "Tack", "Clothes", "Language"};

static void summary(Buf *b, Lib *L) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    putf(b, "HoofMods bug report\r\n===================\r\n");
    put_wide(b, L"HoofMods version: " HM_VERSION_TEXT L"\r\n");
    putf(b, "Made: %04d-%02d-%02d %02d:%02d\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    win_version(b);
    game_version(b, L);
    putf(b, "Game folder: "), put_wide(b, L->game[0] ? L->game : L"not found"), putf(b, "\r\n");
    putf(b, "Game running right now: %s\r\n", lib_game_running() ? "yes" : "no");
    static const char *ld[] = {"not installed", "installed", "someone else's hid.dll is there", "older version installed (update ready)"};
    putf(b, "HoofMods loader: %s%s\r\n", L->loader >= 0 && L->loader <= 3 ? ld[L->loader] : "unknown",
         L->loader_gone ? " (it was installed before but is gone now)" : "");
    putf(b, "BepInEx: %s\r\n", !L->bepinex ? "not installed" : L->bep_on ? "installed, on" : "installed, turned off");

    putf(b, "\r\nMods\r\n----\r\n");
    int any = 0;
    for (int i = 0; i < L->n; i++) {
        Mod *m = &L->mods[i];
        if (m->hidden) continue;
        any = 1;
        putf(b, "[%s] ", KIND_TXT[m->kind]), put_wide(b, m->name);
        if (m->count > 1) putf(b, " (%d items)", m->count);
        putf(b, ": %s", m->enabled ? "on" : "off");
        if (m->info[0]) putf(b, "  |  "), put_wide(b, m->info);
        putf(b, "\r\n");
    }
    if (!any) putf(b, "none\r\n");

    putf(b, "\r\nProblems HoofMods found\r\n-----------------------\r\n");
    if (!L->nprobs) putf(b, "none\r\n");
    for (int i = 0; i < L->nprobs; i++) {
        Problem *p = &L->probs[i];
        putf(b, "%d. ", i + 1), put_wide(b, p->title), putf(b, "\r\n   "), put_wide(b, p->what), putf(b, "\r\n");
        if (p->details[0]) putf(b, "   "), put_wide(b, p->details), putf(b, "\r\n");
    }
    putf(b, "\r\n");
}

/* ==================== main */

int report_make(Lib *L, const wchar_t *out_dir, wchar_t *path, int pathn, wchar_t *err, int errn) {
    path[0] = err[0] = 0;
    setup_names();
    free(g_skipped.p);
    g_skipped = (Buf){0};

    wchar_t dir[MAX_PATH];
    if (out_dir) wcsncpy(dir, out_dir, MAX_PATH - 1), dir[MAX_PATH - 1] = 0;
    else {
        PWSTR d = NULL;
        if (SHGetKnownFolderPath(&FOLDERID_Desktop, 0, NULL, &d) != S_OK || !d) {
            if (d) CoTaskMemFree(d);
            return swprintf(err, errn, L"HoofMods couldn't find your Desktop folder."), 0;
        }
        wcsncpy(dir, d, MAX_PATH - 1), dir[MAX_PATH - 1] = 0;
        CoTaskMemFree(d);
    }

    SYSTEMTIME t;
    GetLocalTime(&t);
    g_dtime = (t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2);
    g_ddate = ((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay;

    Zip z = {0};
    Buf rep = {0};
    summary(&rep, L);

    /* HoofMods own stuff */
    static const wchar_t *mine[] = {L"loader_log.txt", L"last_launch.ini", L"app.ini", L"keep_out.txt"};
    for (int i = 0; i < 4; i++) {
        wchar_t p[MAX_PATH];
        char zn[200];
        swprintf(p, MAX_PATH, L"%ls\\%ls", L->data, mine[i]);
        zname_of(zn, sizeof zn, "hoofmods", mine[i]);
        if (!add_file(&z, p, zn) && i < 2 && GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES)
            putf(&g_skipped, "  not there: %s (the game hasn't started with the loader yet?)\r\n", zn);
    }

    add_code_mods(&z, L);

    /* the games own logs */
    wchar_t prof[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH)) {
        static const wchar_t *pl[] = {L"Player.log", L"Player-prev.log"};
        for (int i = 0; i < 2; i++) {
            wchar_t p[MAX_PATH];
            char zn[200];
            swprintf(p, MAX_PATH, L"%ls\\AppData\\LocalLow\\ThogliStudios\\Horses of Hoofprint Bay\\%ls", prof, pl[i]);
            zname_of(zn, sizeof zn, "game", pl[i]);
            if (!add_file(&z, p, zn) && GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) putf(&g_skipped, "  not there: %s\r\n", zn);
        }
    }

    add_crash(&z, &rep);

    /* BepInEx */
    if (L->game[0]) {
        static const wchar_t *bep[] = {L"BepInEx\\LogOutput.log", L"BepInEx\\ErrorLog.log", L"doorstop_config.ini"};
        static const char *bz[] = {"bepinex/LogOutput.log", "bepinex/ErrorLog.log", "bepinex/doorstop_config.ini"};
        for (int i = 0; i < 3; i++) {
            wchar_t p[MAX_PATH];
            swprintf(p, MAX_PATH, L"%ls\\%ls", L->game, bep[i]);
            add_file(&z, p, bz[i]);
        }
    }

    putf(&rep, "\r\nFiles in this zip\r\n-----------------\r\n");
    for (int i = 0; i < z.n; i++) putf(&rep, "  %s\r\n", z.e[i].name);
    if (g_skipped.n) putf(&rep, "\r\nSkipped\r\n-------\r\n%s", g_skipped.p);
    putf(&rep, "\r\nYour Windows user name, PC name, Steam ID and any email addresses were swapped for USER, PC, STEAMID and EMAIL.\r\n");

    Buf clean = {0};
    scrub(rep.p, rep.n, &clean);
    /* report.txt goes first so its on top when the author opens the zip */
    Zip z2 = {0};
    zip_add(&z2, "report.txt", clean.p, clean.n);
    for (int i = 0; i < z.n; i++) {
        ZEnt *e = &z.e[i];
        size_t nl = strlen(e->name);
        zip_add(&z2, e->name, z.data.p + e->off + 30 + nl, e->size);
    }

    wchar_t name[MAX_PATH];
    int ok = 0;
    for (int k = 1; k < 50 && !ok; k++) {
        swprintf(name, MAX_PATH, k == 1 ? L"%ls\\HoofMods bug report %04d-%02d-%02d %02d%02d.zip" : L"%ls\\HoofMods bug report %04d-%02d-%02d %02d%02d (%d).zip",
                 dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, k);
        if (GetFileAttributesW(name) != INVALID_FILE_ATTRIBUTES) continue;
        ok = zip_save(&z2, name);
        if (!ok) break;
    }
    if (ok) wcsncpy(path, name, pathn - 1), path[pathn - 1] = 0;
    else swprintf(err, errn, L"HoofMods couldn't save the zip in %ls. Check there's free space and try again.", dir);

    free(rep.p), free(clean.p), free(z.data.p), free(z.e), free(z2.data.p), free(z2.e);
    return ok;
}
