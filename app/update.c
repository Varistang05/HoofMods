/* checks if theres a newer HoofMods and can install it w/ one click (Update now) */
#include <windows.h>
#include "release.h"
#if HM_CHECK_UPDATES
#include <winhttp.h>
#include <bcrypt.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "update.h"
#include "zip.h"

static HWND g_notify;
static wchar_t g_ini[MAX_PATH], g_data[MAX_PATH];
static wchar_t g_latest[32];

/* 1 if a is newer than b (1.0.10 beats 1.0.9) */
static int newer(const wchar_t *a, const wchar_t *b) {
    for (;;) {
        long x = wcstol(a, (wchar_t **)&a, 10), y = wcstol(b, (wchar_t **)&b, 10);
        if (x != y) return x > y;
        if (*a != L'.' && *b != L'.') return 0;
        if (*a == L'.') a++;
        if (*b == L'.') b++;
    }
}

#if HM_CHECK_UPDATES /* internet stuff only gets built in when update checks are on */
/* downloads a whole file into a malloc'd buffer (follows GitHubs redirect to the real file on its own) */
static int fetch_all(const wchar_t *url, char **out, DWORD *outn, DWORD maxn) {
    *out = NULL, *outn = 0;
    URL_COMPONENTS u = {sizeof u};
    wchar_t host[256], path[2048];
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url, 0, 0, &u)) return 0;
    int ok = 0;
    HINTERNET s = WinHttpOpen(L"HoofMods", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &len, NULL);
        if (status == 200) {
            DWORD cap = 1 << 16, n = 0;
            char *b = malloc(cap + 1);
            ok = b != NULL;
            for (;;) {
                DWORD avail = 0, got = 0;
                if (!ok || !WinHttpQueryDataAvailable(r, &avail)) { ok = 0; break; }
                if (!avail) break;
                if (n + avail > maxn) { ok = 0; break; }
                if (n + avail > cap) {
                    while (n + avail > cap) cap *= 2;
                    char *nb = realloc(b, cap + 1);
                    if (!nb) { ok = 0; break; }
                    b = nb;
                }
                if (!WinHttpReadData(r, b + n, avail, &got)) { ok = 0; break; }
                n += got;
            }
            if (ok && n) b[n] = 0, *out = b, *outn = n;
            else free(b), ok = 0;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok;
}

/* version file = newest version on line 1, the zips SHA256 on line 2 (the update only installs if it matches) */
static int read_version_file(const char *t, wchar_t *ver, int vern, char *hash65) {
    int k = 0;
    const char *c = t;
    for (; *c && k < vern - 1 && (isdigit((unsigned char)*c) || *c == '.'); c++) ver[k++] = (wchar_t)*c;
    ver[k] = 0;
    hash65[0] = 0;
    while (*c && *c != '\n') c++;
    while (*c && isspace((unsigned char)*c)) c++;
    int h = 0;
    while (h < 64 && isxdigit((unsigned char)c[h])) hash65[h] = (char)tolower((unsigned char)c[h]), h++;
    hash65[h == 64 ? 64 : 0] = 0;
    return k > 0;
}

static DWORD WINAPI check_thread(void *unused) {
    (void)unused;
    char *buf;
    DWORD n;
    if (!fetch_all(HM_VERSION_FILE_URL, &buf, &n, 4096)) return 0;
    wchar_t v[32];
    char hash[65];
    int ok = read_version_file(buf, v, 32, hash);
    free(buf);
    if (!ok) return 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t day[16];
    swprintf(day, 16, L"%04d%02d%02d", t.wYear, t.wMonth, t.wDay);
    WritePrivateProfileStringW(L"Update", L"LastCheck", day, g_ini);
    WritePrivateProfileStringW(L"Update", L"Latest", v, g_ini);
    if (newer(v, HM_VERSION_TEXT)) {
        wcscpy(g_latest, v);
        PostMessageW(g_notify, WM_HM_UPDATE, 0, 0);
    }
    return 0;
}

static int sha256_hex(const void *data, DWORD n, char *hex65) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    unsigned char dig[32];
    int ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0 &&
             BCryptHashData(h, (PUCHAR)data, n, 0) == 0 && BCryptFinishHash(h, dig, 32, 0) == 0;
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    for (int i = 0; ok && i < 32; i++) sprintf(hex65 + i * 2, "%02x", dig[i]);
    return ok;
}

static int write_file(const wchar_t *path, const void *data, size_t n) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    int ok = WriteFile(f, data, (DWORD)n, &w, NULL) && w == n;
    CloseHandle(f);
    if (!ok) DeleteFileW(path);
    return ok;
}

static int zip_find(Zip *z, const char *name) {
    for (int i = 0; i < z->n; i++)
        if (!z->e[i].dir && !_stricmp(z->e[i].name, name)) return i;
    return -1;
}

int update_install(wchar_t *err, int errn) {
#define FAIL(...) return swprintf(err, errn, __VA_ARGS__), 0
    /* 1. fresh look at the version file, so we install exactly what it says */
    char *vf;
    DWORD vn;
    if (!fetch_all(HM_VERSION_FILE_URL, &vf, &vn, 4096)) FAIL(L"Couldn't reach the update server. Check your internet and try again.");
    wchar_t ver[32];
    char want[65];
    int vok = read_version_file(vf, ver, 32, want);
    free(vf);
    if (!vok) FAIL(L"Couldn't read the update info. Try again later.");
    if (!newer(ver, HM_VERSION_TEXT)) FAIL(L"You already have the newest HoofMods.");
    if (!want[0]) FAIL(L"This update can't install by itself. Grab it on my Patreon instead.");

    /* 2. download the zip and make sure its the real one */
    wchar_t url[600];
    swprintf(url, 600, HM_UPDATE_ZIP_URL, ver, ver);
    char *zb;
    DWORD zn;
    if (!fetch_all(url, &zb, &zn, 64u << 20)) FAIL(L"Couldn't download HoofMods %ls. Check your internet and try again.", ver);
    char got[65] = "";
    if (!sha256_hex(zb, zn, got) || strcmp(got, want)) {
        free(zb);
        FAIL(L"The download didn't come through right, so nothing was changed. Try again later.");
    }

    /* 3. save it + take the files out */
    wchar_t dir[MAX_PATH], zpath[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%ls\\update", g_data);
    CreateDirectoryW(dir, NULL);
    swprintf(zpath, MAX_PATH, L"%ls\\HoofMods_v%ls.zip", dir, ver);
    int saved = write_file(zpath, zb, zn);
    free(zb);
    if (!saved) FAIL(L"Couldn't save the download. Check there's free space and try again.");
    Zip z;
    const char *zerr = NULL;
    if (zip_open(&z, zpath, &zerr)) FAIL(L"The download didn't come through right, so nothing was changed. Try again later.");
    int ie = zip_find(&z, "HoofMods.exe"), il = zip_find(&z, "loader/hid.dll");
    unsigned char *exe = ie >= 0 ? zip_read(&z, ie, &zerr) : NULL;
    size_t exen = ie >= 0 ? z.e[ie].usize : 0;
    unsigned char *ld = il >= 0 ? zip_read(&z, il, &zerr) : NULL;
    size_t ldn = il >= 0 ? z.e[il].usize : 0;
    zip_close(&z);
    if (!exe) {
        free(ld);
        FAIL(L"The download didn't come through right, so nothing was changed. Try again later.");
    }

    /* 4. swap the exe. Windows lets us rename a running exe, so: new file next to it, old one -> .old, new one in */
    wchar_t me[MAX_PATH], tmp[MAX_PATH + 8], old[MAX_PATH + 8];
    GetModuleFileNameW(NULL, me, MAX_PATH);
    swprintf(tmp, MAX_PATH + 8, L"%ls.new", me);
    swprintf(old, MAX_PATH + 8, L"%ls.old", me);
    int wrote = write_file(tmp, exe, exen);
    free(exe);
    if (!wrote) {
        free(ld);
        FAIL(L"Windows didn't let HoofMods change its own files here. Move the HoofMods folder somewhere like your Desktop, or grab the update on my Patreon.");
    }
    DeleteFileW(old);
    if (!MoveFileExW(me, old, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp);
        free(ld);
        FAIL(L"Windows didn't let HoofMods change its own files here. Move the HoofMods folder somewhere like your Desktop, or grab the update on my Patreon.");
    }
    if (!MoveFileExW(tmp, me, MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old, me, MOVEFILE_REPLACE_EXISTING); /* put the old one back, nothing changed */
        DeleteFileW(tmp);
        free(ld);
        FAIL(L"Couldn't put the new HoofMods in place, so nothing was changed. Try again later.");
    }

    /* 5. the loader that sits next to the exe (HoofMods offers "Update loader" for the game folder after) */
    if (ld) {
        wchar_t ldir[MAX_PATH], lpath[MAX_PATH], ltmp[MAX_PATH + 8];
        wcscpy(ldir, me);
        wchar_t *sl = wcsrchr(ldir, L'\\');
        if (sl) {
            wcscpy(sl + 1, L"loader");
            CreateDirectoryW(ldir, NULL);
            swprintf(lpath, MAX_PATH, L"%ls\\hid.dll", ldir);
            swprintf(ltmp, MAX_PATH + 8, L"%ls.new", lpath);
            if (write_file(ltmp, ld, ldn) && !MoveFileExW(ltmp, lpath, MOVEFILE_REPLACE_EXISTING)) DeleteFileW(ltmp);
        }
        free(ld);
    }
    DeleteFileW(zpath);
    return 1;
#undef FAIL
}
#else
int update_install(wchar_t *err, int errn) { return swprintf(err, errn, L"This HoofMods can't update itself."), 0; }
#endif

/* the old exe from the last update, it can only be deleted once that copy closed */
static DWORD WINAPI cleanup_thread(void *unused) {
    (void)unused;
    wchar_t me[MAX_PATH], old[MAX_PATH + 8];
    GetModuleFileNameW(NULL, me, MAX_PATH);
    swprintf(old, MAX_PATH + 8, L"%ls.old", me);
    for (int i = 0; i < 40 && GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES; i++) {
        if (DeleteFileW(old)) break;
        Sleep(250);
    }
    return 0;
}

void update_start(HWND notify, const wchar_t *data_dir) {
    g_notify = notify;
    wcsncpy(g_data, data_dir, MAX_PATH - 1);
    swprintf(g_ini, MAX_PATH, L"%ls\\app.ini", data_dir);
    HANDLE ct = CreateThread(NULL, 0, cleanup_thread, NULL, 0, NULL);
    if (ct) CloseHandle(ct);
#if !HM_CHECK_UPDATES
    return; /* no internet code in this build */
#endif
    if (!HM_VERSION_FILE_URL[0]) return;
    /* if an earlier check already found a newer one, still show it */
    wchar_t last[32];
    GetPrivateProfileStringW(L"Update", L"Latest", L"", last, 32, g_ini);
    if (last[0] && newer(last, HM_VERSION_TEXT)) wcscpy(g_latest, last);
    /* check every time HoofMods opens so ppl find out about updates right away */
#if HM_CHECK_UPDATES
    HANDLE th = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (th) CloseHandle(th);
#endif
}

/* 1 the first time a newer version shows up, so the popup only comes once per version */
int update_should_tell(void) {
    if (!g_latest[0]) return 0;
    wchar_t told[32];
    GetPrivateProfileStringW(L"Update", L"Told", L"", told, 32, g_ini);
    if (!wcscmp(told, g_latest)) return 0;
    WritePrivateProfileStringW(L"Update", L"Told", g_latest, g_ini);
    return 1;
}

const wchar_t *update_latest(void) { return g_latest[0] ? g_latest : NULL; }

int update_has_page(void) { return HM_UPDATE_URL[0] != 0; }

void update_open_page(void) {
    if (HM_UPDATE_URL[0]) ShellExecuteW(NULL, L"open", HM_UPDATE_URL, NULL, NULL, SW_SHOWNORMAL);
}
