#include "mods.h"
#include <shlobj.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define GAME_DIR L"HorsesOfHoofprintBay"
#define GAME_EXE L"Horses of Hoofprint Bay.exe"
#define LOADER_FILE L"hid.dll"

/* the loader sits next to the app in loader\hid.dll and gets copied to the game folder from there */
static void loader_source(wchar_t *out) {
    GetModuleFileNameW(NULL, out, MAX_PATH);
    wchar_t *sl = wcsrchr(out, L'\\');
    if (sl) sl[1] = 0;
    wcscat(out, L"loader\\" LOADER_FILE);
}

const wchar_t *KIND_TITLE[MK_COUNT] = {L"Code mods", L"BepInEx mods", L"Tack", L"Clothes", L"Language"};
const wchar_t *KIND_ITEM[MK_COUNT] = {L"code mod", L"BepInEx plugin", L"tack item", L"clothing item", L"language file"};
static const wchar_t *CONTENT_DIR[MK_COUNT] = {0, 0, L"Tack", L"Clothes", L"Language"};

static int exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
static int is_dir(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

#define mkdirs hm_mkdirs
#define content_dir hm_content_dir

void hm_mkdirs(const wchar_t *path) {
    wchar_t p[MAX_PATH];
    wcscpy(p, path);
    for (wchar_t *c = p + 3; *c; c++)
        if (*c == L'\\') {
            *c = 0;
            CreateDirectoryW(p, NULL);
            *c = L'\\';
        }
    CreateDirectoryW(p, NULL);
}

/* reads a whole small file, free it after */
static char *slurp(const wchar_t *path, DWORD max, DWORD *len) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(f, NULL), got = 0;
    if (sz > max) sz = max;
    char *b = malloc(sz + 1);
    if (b && !ReadFile(f, b, sz, &got, NULL)) got = 0;
    CloseHandle(f);
    if (b) b[got] = 0;
    if (len) *len = got;
    return b;
}

static int memfind(const char *hay, DWORD n, const char *needle) {
    size_t k = strlen(needle);
    for (DWORD i = 0; i + k <= n; i++)
        if (!memcmp(hay + i, needle, k)) return 1;
    return 0;
}

void lib_init(Lib *L) {
    memset(L, 0, sizeof *L);
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", L->data, MAX_PATH)) {
        wcscat(L->data, L"\\HoofMods");
        mkdirs(L->data);
    }
    PWSTR low = NULL;
    if (SHGetKnownFolderPath(&FOLDERID_LocalAppDataLow, 0, NULL, &low) == S_OK) {
        swprintf(L->content, MAX_PATH, L"%ls\\ThogliStudios\\Horses of Hoofprint Bay\\Saves~\\Mods", low);
        CoTaskMemFree(low);
    }
    /* app.ini [App] Content= changes where the games Mods folder is (for odd setups like Proton) */
    wchar_t ini[MAX_PATH], over[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%ls\\app.ini", L->data);
    GetPrivateProfileStringW(L"App", L"Content", L"", over, MAX_PATH, ini);
    if (over[0]) wcscpy(L->content, over);
}

/* ==== finding the game ==== */

static void find_appid(Lib *L, const wchar_t *steamapps) {
    wchar_t pat[MAX_PATH], p[MAX_PATH];
    WIN32_FIND_DATAW fd;
    swprintf(pat, MAX_PATH, L"%ls\\appmanifest_*.acf", steamapps);
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        swprintf(p, MAX_PATH, L"%ls\\%ls", steamapps, fd.cFileName);
        DWORD n;
        char *t = slurp(p, 1 << 16, &n);
        if (t && memfind(t, n, "\"" "HorsesOfHoofprintBay" "\"")) {
            const wchar_t *d = fd.cFileName + wcslen(L"appmanifest_");
            int k = 0;
            while (iswdigit(d[k]) && k < 15) L->appid[k] = d[k], k++;
            L->appid[k] = 0;
        }
        free(t);
    } while (!L->appid[0] && FindNextFileW(f, &fd));
    FindClose(f);
}

static int try_library(Lib *L, const wchar_t *lib) {
    wchar_t g[MAX_PATH], exe[MAX_PATH], apps[MAX_PATH];
    swprintf(apps, MAX_PATH, L"%ls\\steamapps", lib);
    swprintf(g, MAX_PATH, L"%ls\\common\\" GAME_DIR, apps);
    swprintf(exe, MAX_PATH, L"%ls\\" GAME_EXE, g);
    if (!exists(exe)) return 0;
    wcscpy(L->game, g);
    find_appid(L, apps);
    return 1;
}

int lib_set_game(Lib *L, const wchar_t *dir) {
    wchar_t exe[MAX_PATH], ini[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\" GAME_EXE, dir);
    if (!exists(exe)) return 0;
    wcscpy(L->game, dir);
    swprintf(ini, MAX_PATH, L"%ls\\app.ini", L->data);
    WritePrivateProfileStringW(L"App", L"Game", dir, ini);
    return 1;
}

int lib_find_game(Lib *L) {
    wchar_t ini[MAX_PATH], saved[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%ls\\app.ini", L->data);
    GetPrivateProfileStringW(L"App", L"Game", L"", saved, MAX_PATH, ini);
    if (saved[0] && lib_set_game(L, saved)) return 1;

    wchar_t steam[MAX_PATH];
    DWORD sz = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, steam, &sz))
        return 0;
    for (wchar_t *c = steam; *c; c++)
        if (*c == L'/') *c = L'\\';
    if (try_library(L, steam)) return 1;

    /* other Steam libraries, lines like   "path"   "D:\\SteamLibrary" */
    wchar_t vdf[MAX_PATH];
    swprintf(vdf, MAX_PATH, L"%ls\\steamapps\\libraryfolders.vdf", steam);
    DWORD n;
    char *t = slurp(vdf, 1 << 20, &n);
    if (!t) return 0;
    for (char *s = t; (s = strstr(s, "\"path\"")); s += 6) {
        char *q = strchr(s + 6, '"');
        if (!q) break;
        char raw[MAX_PATH];
        int k = 0;
        for (char *c = q + 1; *c && *c != '"' && k < MAX_PATH - 1; c++) {
            if (*c == '\\' && c[1] == '\\') c++;
            raw[k++] = *c;
        }
        raw[k] = 0;
        wchar_t lib[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, raw, -1, lib, MAX_PATH);
        if (try_library(L, lib)) {
            free(t);
            return 1;
        }
    }
    free(t);
    return 0;
}

/* ==== scanning ==== */

static Mod *add(Lib *L, ModKind k, const wchar_t *id, int on) {
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 64;
        L->mods = realloc(L->mods, L->cap * sizeof(Mod));
    }
    Mod *m = &L->mods[L->n++];
    memset(m, 0, sizeof *m);
    m->kind = k;
    m->enabled = on;
    m->pkg = -1;
    wcsncpy(m->id, id, MAX_PATH - 1);
    wcsncpy(m->name, id, 127);
    return m;
}

static void scan_dll_mods(Lib *L) {
    wchar_t mods[MAX_PATH], ini[MAX_PATH], last[MAX_PATH], pat[MAX_PATH];
    swprintf(mods, MAX_PATH, L"%ls\\Mods", L->game);
    swprintf(ini, MAX_PATH, L"%ls\\HoofMods.ini", mods);
    swprintf(last, MAX_PATH, L"%ls\\last_launch.ini", L->data);
    swprintf(pat, MAX_PATH, L"%ls\\*", mods);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.' || fd.cFileName[0] == L'_') continue;
        Mod *m = add(L, MK_DLL, fd.cFileName, GetPrivateProfileIntW(L"Enabled", fd.cFileName, 1, ini));
        wchar_t mi[MAX_PATH], author[64], ver[32], st[64];
        swprintf(mi, MAX_PATH, L"%ls\\%ls\\mod.ini", mods, fd.cFileName);
        GetPrivateProfileStringW(L"Mod", L"Name", fd.cFileName, m->name, 128, mi);
        GetPrivateProfileStringW(L"Mod", L"Author", L"", author, 64, mi);
        GetPrivateProfileStringW(L"Mod", L"Version", L"", ver, 32, mi);
        GetPrivateProfileStringW(L"Mods", fd.cFileName, L"", st, 64, last);
        wchar_t *o = m->info;
        int left = 256;
        if (ver[0]) o += swprintf(o, left, L"v%ls  ", ver);
        if (author[0]) o += swprintf(o, left - (int)(o - m->info), L"by %ls  ", author);
        if (wcsncmp(st, L"failed", 6) == 0)
            swprintf(o, left - (int)(o - m->info), L"\x2022 Failed to start last time");
        else if (!wcscmp(st, L"skipped"))
            swprintf(o, left - (int)(o - m->info), L"\x2022 Not started: the same file is in the game folder (it would run twice)");
        else if (!wcscmp(st, L"no dll"))
            swprintf(o, left - (int)(o - m->info), L"\x2022 No DLL inside, nothing to start");
        else if (!wcscmp(st, L"loaded"))
            swprintf(o, left - (int)(o - m->info), L"\x2022 Started fine last time");
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

/* ==== mods that load straight from the game folder (not thru HoofMods) ==== */

const wchar_t *PROXY_NAMES[] = {L"version.dll", L"winmm.dll", L"winhttp.dll", L"wininet.dll", L"dinput8.dll", L"dxgi.dll",
                                L"d3d9.dll", L"d3d11.dll", L"d3d12.dll", L"dsound.dll", L"xinput1_3.dll", L"xinput1_4.dll",
                                L"xinput9_1_0.dll", L"dbghelp.dll", L"iphlpapi.dll", L"opengl32.dll", NULL};

int hm_is_proxy_name(const wchar_t *file) {
    for (int i = 0; PROXY_NAMES[i]; i++)
        if (!_wcsicmp(file, PROXY_NAMES[i])) return 1;
    return 0;
}

/* a nice name for that file, from names inside the file itself */
static void guess_direct_name(const char *t, DWORD n, const wchar_t *file, wchar_t *out, int outn) {
    static const struct { const char *needle; const wchar_t *name; } known[] = {
        {"StickHorse", L"StickHorse"}, {"ReShade", L"ReShade"}, {"MelonLoader", L"MelonLoader"}, {"Doorstop", L"BepInEx"}, {"SpecialK", L"Special K"}};
    for (int k = 0; k < (int)(sizeof known / sizeof known[0]); k++)
        if (memfind(t, n, known[k].needle)) {
            wcsncpy(out, known[k].name, outn - 1);
            return;
        }
    swprintf(out, outn, L"Mod in the game folder (%ls)", file);
}

static void scan_direct_mods(Lib *L) {
    for (int i = 0; PROXY_NAMES[i]; i++) {
        for (int off = 0; off < 2; off++) {
            wchar_t p[MAX_PATH], id[MAX_PATH];
            swprintf(p, MAX_PATH, L"%ls\\%ls%ls", L->game, PROXY_NAMES[i], off ? L".off" : L"");
            if (!exists(p)) continue;
            DWORD n;
            char *t = slurp(p, 64u << 20, &n);
            if (!t) continue;
            int bepinex_core = memfind(t, n, "Doorstop") && !_wcsicmp(PROXY_NAMES[i], L"winhttp.dll");
            if (!bepinex_core) { /* thats just BepInEx's starter, not a mod to switch here */
                swprintf(id, MAX_PATH, L"@%ls", PROXY_NAMES[i]);
                Mod *m = add(L, MK_DLL, id, !off);
                guess_direct_name(t, n, PROXY_NAMES[i], m->name, 128);
                if (!off) swprintf(m->info, 256, L"Installed straight in the game folder (%ls)", PROXY_NAMES[i]);
            }
            free(t);
        }
    }
}

/* same mod in the game folder AND thru HoofMods? then both copies would run */
static void flag_twins(Lib *L) {
    for (int i = 0; i < L->n; i++) {
        Mod *d = &L->mods[i];
        if (d->kind != MK_DLL || d->id[0] != L'@' || !d->enabled) continue;
        for (int j = 0; j < L->n; j++) {
            Mod *m = &L->mods[j];
            if (j == i || m->kind != MK_DLL || m->id[0] == L'@' || !m->enabled) continue;
            if (!_wcsnicmp(m->name, d->name, wcslen(d->name)) || !_wcsnicmp(d->name, m->name, wcslen(m->name)))
                swprintf(d->info, 256, L"\x2022 Also installed through HoofMods, so it would run twice. Turn one off.");
        }
    }
}

static void scan_bepinex_dir(Lib *L, const wchar_t *dir, int on) {
    wchar_t pat[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        int d = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
        if (!d && !(ext && !_wcsicmp(ext, L".dll"))) continue;
        Mod *m = add(L, MK_BEPINEX, fd.cFileName, on);
        if (!d && ext) m->name[ext - fd.cFileName] = 0;
        if (on) wcscpy(m->info, d ? L"Folder in BepInEx\\plugins" : L"File in BepInEx\\plugins");
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

/* string value of the first "key": "value" from `from` on, gives a pointer past it or NULL */
static const char *json_str(const char *from, const char *key, char *buf, int bufn) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *s = strstr(from, pat);
    if (!s) return NULL;
    s = strchr(s + strlen(pat), ':');
    if (!s) return NULL;
    s++;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s != '"') return NULL;
    const char *e = strchr(s + 1, '"');
    if (!e) return NULL;
    int len = (int)(e - s - 1) < bufn - 1 ? (int)(e - s - 1) : bufn - 1;
    memcpy(buf, s + 1, len);
    buf[len] = 0;
    return e + 1;
}

/* the name of a tack/clothes item, the game keeps it in
   "namesPerLanguage": [{"lang": "EN", "value": "Blue Ascot Jumping Pad"}, ...] */
static void json_name(const wchar_t *path, wchar_t *out, int outn) {
    DWORD n;
    char *t = slurp(path, 1 << 16, &n);
    if (!t) return;
    char v[256] = "";
    const char *names = strstr(t, "\"namesPerLanguage\"");
    if (names) {
        char lang[16];
        const char *p = names;
        while ((p = json_str(p, "lang", lang, sizeof lang))) {
            if (!_stricmp(lang, "EN")) {
                json_str(p, "value", v, sizeof v);
                break;
            }
        }
        if (!v[0]) json_str(names, "value", v, sizeof v); /* no English name, just take the first one */
    }
    static const char *keys[] = {"displayName", "itemName", "name", "Name"};
    for (int k = 0; k < 4 && !v[0]; k++) json_str(t, keys[k], v, sizeof v);
    if (v[0]) MultiByteToWideChar(CP_UTF8, 0, v, -1, out, outn);
    free(t);
}

static int has_mod(Lib *L, ModKind k, const wchar_t *id) {
    for (int i = 0; i < L->n; i++)
        if (L->mods[i].kind == k && !_wcsicmp(L->mods[i].id, id)) return 1;
    return 0;
}

void hm_content_dir(Lib *L, ModKind k, int on, wchar_t *out) {
    if (on) swprintf(out, MAX_PATH, L"%ls\\%ls", L->content, CONTENT_DIR[k]);
    else swprintf(out, MAX_PATH, L"%ls\\Off\\%ls", L->data, CONTENT_DIR[k]);
}

/* ==== pics named inside item files ("spritesWearingLayer1": ["pad_layer1.png"]) ==== */

#define MAX_REFS 1024
typedef struct {
    wchar_t name[MAX_REFS][MAX_PATH]; /* pic file names */
    wchar_t owner[MAX_REFS][MAX_PATH]; /* which item file names it */
    int n;
} Refs;

static void refs_of_json(const wchar_t *path, const wchar_t *owner, Refs *R) {
    DWORD n;
    char *t = slurp(path, 1 << 18, &n);
    if (!t) return;
    for (DWORD a = 0; a < n && R->n < MAX_REFS; a++) {
        if (t[a] != '"') continue;
        DWORD b = a + 1;
        while (b < n && t[b] != '"' && b - a < MAX_PATH) b++;
        if (b >= n) break;
        if (b - a > 5 && !_strnicmp(t + b - 4, ".png", 4)) {
            char ref[MAX_PATH];
            memcpy(ref, t + a + 1, b - a - 1);
            ref[b - a - 1] = 0;
            if (!strpbrk(ref, "\\/:")) {
                MultiByteToWideChar(CP_UTF8, 0, ref, -1, R->name[R->n], MAX_PATH);
                wcscpy(R->owner[R->n++], owner);
            }
        }
        a = b;
    }
    free(t);
}

static void refs_of_dir(const wchar_t *dir, Refs *R) {
    R->n = 0;
    wchar_t pat[MAX_PATH], p[MAX_PATH], base[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\*.json", dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        swprintf(p, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        wcscpy(base, fd.cFileName);
        *wcsrchr(base, L'.') = 0;
        refs_of_json(p, base, R);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

/* files (in dir) that belong to item id: everything called id.*, plus pics its file names
   that no other item uses (shared pics stay put) */
static int item_files(const wchar_t *dir, const wchar_t *id, wchar_t (*out)[MAX_PATH], int max) {
    static Refs R;
    int n = 0;
    wchar_t pat[MAX_PATH], base[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\%ls.*", dir, id);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            wcscpy(base, fd.cFileName);
            wchar_t *ext = wcsrchr(base, L'.');
            if (ext) *ext = 0;
            if (!_wcsicmp(base, id) && n < max) wcscpy(out[n++], fd.cFileName); /* "Saddle.*" also matches "Saddle.old.png" */
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    refs_of_dir(dir, &R);
    for (int r = 0; r < R.n; r++) {
        if (_wcsicmp(R.owner[r], id)) continue;
        int dup = 0, shared = 0;
        for (int k = 0; k < n; k++) dup |= !_wcsicmp(out[k], R.name[r]);
        for (int q = 0; q < R.n; q++) shared |= _wcsicmp(R.owner[q], id) && !_wcsicmp(R.name[q], R.name[r]);
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%ls\\%ls", dir, R.name[r]);
        if (!dup && !shared && n < max && exists(p)) wcscpy(out[n++], R.name[r]);
    }
    return n;
}

static void scan_content(Lib *L, ModKind k, int on) {
    static Refs R;
    wchar_t dir[MAX_PATH], pat[MAX_PATH];
    content_dir(L, k, on, dir);
    refs_of_dir(dir, &R);
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t base[MAX_PATH];
        wcscpy(base, fd.cFileName);
        wchar_t *ext = wcsrchr(base, L'.');
        if (ext) *ext = 0;
        if (has_mod(L, k, base)) continue;
        if (!ext || _wcsicmp(ext + 1, L"json")) { /* a pic another item names belongs to that item */
            int owned = 0;
            for (int r = 0; r < R.n && !owned; r++) owned = !_wcsicmp(R.name[r], fd.cFileName) && _wcsicmp(R.owner[r], base);
            wchar_t js[MAX_PATH];
            swprintf(js, MAX_PATH, L"%ls\\%ls.json", dir, base);
            if (owned && !exists(js)) continue;
        }
        Mod *m = add(L, k, base, on);
        wchar_t js[MAX_PATH];
        swprintf(js, MAX_PATH, L"%ls\\%ls.json", dir, base);
        json_name(js, m->name, 128);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

/* packs: %LOCALAPPDATA%\HoofMods\packages\<id>.ini  [Package] Name, Kind  [Items] 0=<item id> ...
   items in a pack get hidden and show up as one pack row */
static void free_packages(Lib *L) {
    for (int p = 0; p < L->npkgs; p++) free(L->pkgs[p].items);
    free(L->pkgs);
    L->pkgs = NULL;
    L->npkgs = 0;
}

static void load_packages(Lib *L) {
    free_packages(L);
    wchar_t pat[MAX_PATH], ini[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\packages\\*.ini", L->data);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    int base_n = L->n;
    do {
        swprintf(ini, MAX_PATH, L"%ls\\packages\\%ls", L->data, fd.cFileName);
        int kind = GetPrivateProfileIntW(L"Package", L"Kind", -1, ini);
        int cnt = GetPrivateProfileIntW(L"Package", L"Items", 0, ini);
        if (kind < 0 || kind >= MK_COUNT || cnt <= 0) continue;
        L->pkgs = realloc(L->pkgs, (L->npkgs + 1) * sizeof(Pkg));
        Pkg *P = &L->pkgs[L->npkgs];
        memset(P, 0, sizeof *P);
        wcsncpy(P->id, fd.cFileName, 63);
        wchar_t *dot = wcsrchr(P->id, L'.');
        if (dot) *dot = 0;
        GetPrivateProfileStringW(L"Package", L"Name", P->id, P->name, 128, ini);
        P->kind = kind;
        P->items = calloc(cnt, sizeof *P->items);
        int members = 0, on = 0, first = -1;
        for (int k = 0; k < cnt; k++) {
            wchar_t key[16];
            swprintf(key, 16, L"%d", k);
            GetPrivateProfileStringW(L"Items", key, L"", P->items[P->nitems], MAX_PATH, ini);
            if (!P->items[P->nitems][0]) continue;
            for (int i = 0; i < base_n; i++) {
                Mod *m = &L->mods[i];
                if (m->kind == kind && !m->hidden && !_wcsicmp(m->id, P->items[P->nitems])) {
                    m->hidden = 1;
                    m->pkg = L->npkgs;
                    members++;
                    on += m->enabled;
                    if (first < 0) first = i;
                }
            }
            P->nitems++;
        }
        if (!members) { /* everything got removed by hand, forget the pack */
            free(P->items);
            DeleteFileW(ini);
            continue;
        }
        wchar_t info[256];
        if (members == 1) wcscpy(info, L->mods[first].info);
        else swprintf(info, 256, L"%d %lss", members, KIND_ITEM[kind]);
        Mod *row = add(L, kind, P->id, on > 0); /* the mods list can move around so dont hold on to pointers */
        row->pkg = L->npkgs;
        row->count = members;
        wcsncpy(row->name, P->name, 127);
        wcscpy(row->info, info);
        L->npkgs++;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

static int cmp_mod(const void *a, const void *b) {
    const Mod *x = a, *y = b;
    if (x->kind != y->kind) return x->kind - y->kind;
    return _wcsicmp(x->name, y->name);
}

static void detect_loader(Lib *L) {
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\" LOADER_FILE, L->game);
    L->loader = 0;
    DWORD n;
    char *t = slurp(p, 64u << 20, &n);
    if (!t) return;
    L->loader = memfind(t, n, "HoofMods_Version") ? 1 : 2; /* 2: another mod uses that name, never overwrite it */
    if (L->loader == 1) { /* ours, but is it the same one that came w/ this app? */
        wchar_t src[MAX_PATH];
        loader_source(src);
        DWORD sn;
        char *s = slurp(src, 64u << 20, &sn);
        if (s && (sn != n || memcmp(s, t, n))) L->loader = 3; /* older (or newer) loader, offer the update */
        free(s);
    }
    free(t);
}

void hm_keepout(Lib *L, const wchar_t *path, int add) {
    wchar_t list[MAX_PATH];
    swprintf(list, MAX_PATH, L"%ls\\keep_out.txt", L->data);
    char want[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, want, sizeof want, NULL, NULL);
    DWORD n = 0;
    char *t = slurp(list, 1 << 20, &n);
    size_t cap = n + strlen(want) + 4;
    char *out = malloc(cap), *o = out;
    if (t)
        for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
            if (!*line || !_stricmp(line, want)) continue; /* take it out, gets added back below if needed */
            size_t l = strlen(line);
            memcpy(o, line, l);
            o += l;
            *o++ = '\n';
        }
    if (add) {
        size_t l = strlen(want);
        memcpy(o, want, l);
        o += l;
        *o++ = '\n';
    }
    free(t);
    if (o == out) DeleteFileW(list);
    else {
        HANDLE f = CreateFileW(list, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(f, out, (DWORD)(o - out), &w, NULL);
            CloseHandle(f);
        }
    }
    free(out);
}

static int same_file(const wchar_t *a, const wchar_t *b) {
    DWORD na, nb;
    char *x = slurp(a, 64u << 20, &na), *y = slurp(b, 64u << 20, &nb);
    int same = x && y && na == nb && !memcmp(x, y, na);
    free(x);
    free(y);
    return same;
}

static int keepout_has(Lib *L, const wchar_t *path) {
    wchar_t list[MAX_PATH];
    swprintf(list, MAX_PATH, L"%ls\\keep_out.txt", L->data);
    char want[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, want, sizeof want, NULL, NULL);
    DWORD n;
    char *t = slurp(list, 1 << 20, &n);
    int found = 0;
    if (t)
        for (char *line = strtok(t, "\r\n"); line && !found; line = strtok(NULL, "\r\n")) found = !_stricmp(line, want);
    free(t);
    return found;
}

/* when Steam Cloud brings back a turned off file it can end up in the game folder AND in storage
   on the keep out list = its meant to be off, so the game folder copy goes (if Steam brings it back again the loader deletes it)
   not on the list = its meant to be on, so the extra copy in storage goes */
static void settle_duplicates(Lib *L, ModKind k) {
    wchar_t on_dir[MAX_PATH], off_dir[MAX_PATH], pat[MAX_PATH], on_p[MAX_PATH], off_p[MAX_PATH];
    content_dir(L, k, 1, on_dir);
    content_dir(L, k, 0, off_dir);
    swprintf(pat, MAX_PATH, L"%ls\\*", off_dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        swprintf(on_p, MAX_PATH, L"%ls\\%ls", on_dir, fd.cFileName);
        swprintf(off_p, MAX_PATH, L"%ls\\%ls", off_dir, fd.cFileName);
        if (!exists(on_p)) continue;
        if (keepout_has(L, on_p)) DeleteFileW(on_p);
        else if (same_file(on_p, off_p)) DeleteFileW(off_p);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

/* BepInEx counts as installed when winhttp.dll and BepInEx\core are there. just a BepInEx folder
   isnt enough bc installing a BepInEx mod makes one too */
static int bepinex_installed(Lib *L) {
    wchar_t a[MAX_PATH], b[MAX_PATH];
    swprintf(a, MAX_PATH, L"%ls\\winhttp.dll", L->game);
    swprintf(b, MAX_PATH, L"%ls\\BepInEx\\core", L->game);
    return exists(a) && is_dir(b);
}

/* for this game BepInEx 6 needs [IL2CPP] UnityBaseLibrariesSource left empty or it tries to download Unitys
   base libraries. we write that b4 the first start, and if it ever changes we blank it again and clear the
   folders it made so BepInEx rebuilds them. only for a BepInEx HoofMods installed, never someones own setup */
void hm_bepinex_config(Lib *L) {
    wchar_t ini[MAX_PATH], cfg[MAX_PATH], dir[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%ls\\app.ini", L->data);
    if (!GetPrivateProfileIntW(L"BepInEx", L"InstalledByHoofMods", 0, ini)) return;
    swprintf(dir, MAX_PATH, L"%ls\\BepInEx\\config", L->game);
    swprintf(cfg, MAX_PATH, L"%ls\\BepInEx.cfg", dir);
    DWORD n = 0;
    char *t = slurp(cfg, 1 << 20, &n);
    const char *fresh = "[IL2CPP]\r\n\r\nUnityBaseLibrariesSource = \r\n";
    char *out = NULL;
    int changed = 0;
    if (!t) { /* no first start yet, start the config w/ the right value */
        mkdirs(dir);
        out = _strdup(fresh);
    } else {
        char *line = strstr(t, "UnityBaseLibrariesSource");
        if (!line) {
            out = malloc(n + strlen(fresh) + 4);
            sprintf(out, "%s\r\n%s", t, fresh);
            changed = 1;
        } else {
            char *eq = strchr(line, '='), *end = strpbrk(line, "\r\n");
            if (!end) end = t + n;
            if (eq && eq < end) {
                char *v = eq + 1;
                while (v < end && (*v == ' ' || *v == '\t')) v++;
                if (v < end) { /* it has a value, empty it */
                    size_t head = (size_t)(eq + 1 - t);
                    out = malloc(n + 4);
                    memcpy(out, t, head);
                    out[head] = ' ';
                    strcpy(out + head + 1, end);
                    changed = 1;
                }
            }
        }
    }
    if (out) {
        HANDLE f = CreateFileW(cfg, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(f, out, (DWORD)strlen(out), &w, NULL);
            CloseHandle(f);
        }
        if (changed) { /* made w/ the wrong setting, let BepInEx make them again */
            wchar_t gone[2][MAX_PATH];
            const wchar_t *ptr[2];
            swprintf(gone[0], MAX_PATH, L"%ls\\BepInEx\\interop", L->game);
            swprintf(gone[1], MAX_PATH, L"%ls\\BepInEx\\unity-libs", L->game);
            int k = 0;
            for (int i = 0; i < 2; i++)
                if (is_dir(gone[i])) ptr[k] = gone[i], k++;
            hm_recycle(ptr, k);
        }
        free(out);
    }
    free(t);
}

/* BepInEx starts w/ the game thru Doorstop: doorstop_config.ini [General] enabled = true/false
   finds that value in t (the "enabled" line, not "debug_enabled"), *end = where the value ends */
static char *doorstop_value(char *t, char **end) {
    for (char *l = t; l && *l; l = strpbrk(l, "\n") ? strpbrk(l, "\n") + 1 : NULL) {
        char *c = l;
        while (*c == ' ' || *c == '\t') c++;
        if (_strnicmp(c, "enabled", 7) || (c[7] != ' ' && c[7] != '\t' && c[7] != '=')) continue;
        char *eq = strchr(c, '='), *e = strpbrk(c, "\r\n");
        if (!e) e = c + strlen(c);
        if (!eq || eq > e) continue;
        *end = e;
        return eq + 1;
    }
    return NULL;
}

static int bepinex_on(Lib *L) {
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\doorstop_config.ini", L->game);
    char *t = slurp(p, 1 << 20, NULL), *end;
    if (!t) return 1; /* no config, Doorstop starts anyway */
    char *v = doorstop_value(t, &end);
    int on = 1;
    if (v) {
        while (v < end && (*v == ' ' || *v == '\t')) v++;
        on = _strnicmp(v, "false", 5) != 0;
    }
    free(t);
    return on;
}

int lib_set_bepinex(Lib *L, int on, wchar_t *err, int errn) {
    if (lib_game_running()) return swprintf(err, errn, L"Close the game first."), 0;
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\doorstop_config.ini", L->game);
    DWORD n = 0;
    char *t = slurp(p, 1 << 20, &n), *end;
    char *v = t ? doorstop_value(t, &end) : NULL;
    if (!v) {
        free(t);
        return swprintf(err, errn, L"Couldn't find BepInEx's start switch in doorstop_config.ini."), 0;
    }
    const char *val = on ? " true" : " false";
    char *out = malloc(n + 8);
    size_t head = (size_t)(v - t);
    memcpy(out, t, head);
    strcpy(out + head, val);
    strcat(out, end);
    int ok = 0;
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w;
        ok = WriteFile(f, out, (DWORD)strlen(out), &w, NULL);
        CloseHandle(f);
    }
    free(out);
    free(t);
    if (!ok) return swprintf(err, errn, L"Couldn't change doorstop_config.ini."), 0;
    L->bep_on = on;
    return 1;
}

void lib_scan(Lib *L) {
    L->n = 0;
    L->bepinex = 0;
    L->bep_on = 0;
    if (L->game[0]) {
        detect_loader(L);
        scan_dll_mods(L);
        scan_direct_mods(L);
        wchar_t b[MAX_PATH];
        L->bepinex = bepinex_installed(L);
        if (L->bepinex) L->bep_on = bepinex_on(L);
        if (L->bepinex && !lib_game_running()) hm_bepinex_config(L);
        swprintf(b, MAX_PATH, L"%ls\\BepInEx\\plugins", L->game);
        scan_bepinex_dir(L, b, 1);
        swprintf(b, MAX_PATH, L"%ls\\BepInEx\\HoofMods_off", L->game);
        scan_bepinex_dir(L, b, 0);
    }
    for (int k = MK_TACK; k <= MK_LANGUAGE; k++) {
        settle_duplicates(L, k);
        scan_content(L, k, 1);
        scan_content(L, k, 0);
    }
    load_packages(L);
    flag_twins(L);
    qsort(L->mods, L->n, sizeof(Mod), cmp_mod);
}

/* ==== changing stuff ==== */

static int move_item(const wchar_t *from, const wchar_t *to_dir, const wchar_t *name, wchar_t *err, int errn) {
    wchar_t to[MAX_PATH];
    mkdirs(to_dir);
    swprintf(to, MAX_PATH, L"%ls\\%ls", to_dir, name);
    if (MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return 1;
    DWORD e = GetLastError();
    if (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED)
        swprintf(err, errn, L"%ls is in use. Close the game and try again.", name);
    else
        swprintf(err, errn, L"Could not move %ls (error %lu).", name, e);
    return 0;
}

int lib_set_enabled(Lib *L, int i, int on, wchar_t *err, int errn) {
    Mod *m = &L->mods[i];
    if (m->pkg >= 0 && !m->hidden) { /* pack row: switch every item in it */
        int ok = 1;
        for (int j = 0; j < L->n; j++)
            if (L->mods[j].hidden && L->mods[j].pkg == m->pkg && !lib_set_enabled(L, j, on, err, errn)) ok = 0;
        if (ok) m->enabled = on;
        m->pic_state = 0; /* files moved, find the pic again */
        return ok;
    }
    if (m->enabled == on) return 1;
    wchar_t a[MAX_PATH], b[MAX_PATH], from[MAX_PATH];
    switch (m->kind) {
    case MK_DLL:
        if (m->id[0] == L'@') { /* right in the game folder: version.dll vs version.dll.off */
            wchar_t off_name[MAX_PATH];
            swprintf(off_name, MAX_PATH, L"%ls.off", m->id + 1);
            swprintf(from, MAX_PATH, L"%ls\\%ls", L->game, on ? off_name : m->id + 1);
            if (!move_item(from, L->game, on ? m->id + 1 : off_name, err, errn)) return 0;
            break;
        }
        swprintf(a, MAX_PATH, L"%ls\\Mods\\HoofMods.ini", L->game);
        WritePrivateProfileStringW(L"Enabled", m->id, on ? NULL : L"0", a);
        break;
    case MK_BEPINEX:
        swprintf(a, MAX_PATH, L"%ls\\BepInEx\\%ls", L->game, on ? L"HoofMods_off" : L"plugins");
        swprintf(b, MAX_PATH, L"%ls\\BepInEx\\%ls", L->game, on ? L"plugins" : L"HoofMods_off");
        swprintf(from, MAX_PATH, L"%ls\\%ls", a, m->id);
        if (!move_item(from, b, m->id, err, errn)) return 0;
        break;
    default: {
        static wchar_t files[64][MAX_PATH];
        content_dir(L, m->kind, m->enabled, a);
        content_dir(L, m->kind, on, b);
        int nf = item_files(a, m->id, files, 64);
        if (!nf) {
            swprintf(err, errn, L"The files for %ls are missing.", m->name);
            return 0;
        }
        int ok = 1;
        for (int k = 0; k < nf; k++) {
            swprintf(from, MAX_PATH, L"%ls\\%ls", a, files[k]);
            if (!move_item(from, b, files[k], err, errn)) {
                ok = 0;
                continue;
            }
            /* Steam Cloud would bring back a turned off file, the loader keeps it out */
            wchar_t game_path[MAX_PATH];
            swprintf(game_path, MAX_PATH, L"%ls\\%ls", on ? b : a, files[k]);
            hm_keepout(L, game_path, !on);
        }
        if (!ok) return 0;
        if (on) { /* a pic shared w/ a turned off item might have stayed behind, copy it over */
            static Refs R;
            wchar_t js[MAX_PATH], to[MAX_PATH];
            R.n = 0;
            swprintf(js, MAX_PATH, L"%ls\\%ls.json", b, m->id);
            refs_of_json(js, m->id, &R);
            for (int r = 0; r < R.n; r++) {
                swprintf(from, MAX_PATH, L"%ls\\%ls", a, R.name[r]);
                swprintf(to, MAX_PATH, L"%ls\\%ls", b, R.name[r]);
                if (!exists(to) && exists(from)) CopyFileW(from, to, TRUE);
                hm_keepout(L, to, 0);
            }
        }
    }
    }
    m->enabled = on;
    m->pic_state = 0;
    return 1;
}

/* sends stuff to the Recycle Bin so you can always get it back */
int hm_recycle(const wchar_t *const *paths, int n) {
    if (n <= 0) return 1;
    size_t len = 1;
    for (int i = 0; i < n; i++) len += wcslen(paths[i]) + 1;
    wchar_t *buf = calloc(len, sizeof(wchar_t)), *o = buf;
    for (int i = 0; i < n; i++) {
        wcscpy(o, paths[i]);
        o += wcslen(paths[i]) + 1;
    }
    SHFILEOPSTRUCTW op = {.wFunc = FO_DELETE, .pFrom = buf,
                          .fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI};
    int r = SHFileOperationW(&op);
    free(buf);
    if (r || op.fAnyOperationsAborted) return 0;
    for (int i = 0; i < n; i++)
        if (exists(paths[i])) return 0;
    return 1;
}

static int remove_item(Lib *L, Mod *m, wchar_t *err, int errn) {
    static wchar_t list[64][MAX_PATH];
    const wchar_t *ptr[64];
    static wchar_t files[64][MAX_PATH];
    int n = 0, nf = 0;
    wchar_t a[MAX_PATH];
    switch (m->kind) {
    case MK_DLL:
        if (m->id[0] == L'@') swprintf(list[n++], MAX_PATH, L"%ls\\%ls%ls", L->game, m->id + 1, m->enabled ? L"" : L".off");
        else swprintf(list[n++], MAX_PATH, L"%ls\\Mods\\%ls", L->game, m->id);
        break;
    case MK_BEPINEX:
        swprintf(list[n++], MAX_PATH, L"%ls\\BepInEx\\%ls\\%ls", L->game, m->enabled ? L"plugins" : L"HoofMods_off", m->id);
        break;
    default:
        content_dir(L, m->kind, m->enabled, a);
        nf = item_files(a, m->id, files, 64);
        for (int k = 0; k < nf; k++) swprintf(list[n++], MAX_PATH, L"%ls\\%ls", a, files[k]);
    }
    for (int i = 0; i < n; i++) ptr[i] = list[i];
    if (!hm_recycle(ptr, n)) {
        swprintf(err, errn, lib_game_running() ? L"%ls is in use. Close the game and try again." : L"Could not remove %ls.", m->name);
        return 0;
    }
    if (nf) { /* removed tack/clothes cant come back thru Steam Cloud either */
        wchar_t game_dir[MAX_PATH], game_path[MAX_PATH];
        content_dir(L, m->kind, 1, game_dir);
        for (int k = 0; k < nf; k++) {
            swprintf(game_path, MAX_PATH, L"%ls\\%ls", game_dir, files[k]);
            hm_keepout(L, game_path, 1);
        }
    }
    if (m->kind == MK_DLL && m->id[0] != L'@') {
        swprintf(a, MAX_PATH, L"%ls\\Mods\\HoofMods.ini", L->game);
        WritePrivateProfileStringW(L"Enabled", m->id, NULL, a);
    }
    return 1;
}

int lib_uninstall(Lib *L, int i, wchar_t *err, int errn) {
    Mod *m = &L->mods[i];
    if (m->pkg >= 0 && !m->hidden) {
        int ok = 1;
        for (int j = 0; j < L->n; j++)
            if (L->mods[j].hidden && L->mods[j].pkg == m->pkg && !remove_item(L, &L->mods[j], err, errn)) ok = 0;
        if (ok) {
            wchar_t ini[MAX_PATH];
            swprintf(ini, MAX_PATH, L"%ls\\packages\\%ls.ini", L->data, L->pkgs[m->pkg].id);
            DeleteFileW(ini);
        }
        return ok;
    }
    return remove_item(L, m, err, errn);
}

/* ==== saves (only read) ==== */

static void games_dir(Lib *L, wchar_t *out) {
    wcscpy(out, L->content); /* Saves~\Mods becomes Saves~\Games */
    wchar_t *sl = wcsrchr(out, L'\\');
    if (sl) wcscpy(sl + 1, L"Games");
}

/* the games own id for a tack/clothes item ("item.tack.saddlepad.x"), from its file */
static int item_game_id(Lib *L, const Mod *m, char *out, int outn) {
    wchar_t dir[MAX_PATH], js[MAX_PATH];
    content_dir(L, m->kind, m->enabled, dir);
    swprintf(js, MAX_PATH, L"%ls\\%ls.json", dir, m->id);
    DWORD n;
    char *t = slurp(js, 1 << 16, &n);
    if (!t) return 0;
    int ok = json_str(t, "id", out, outn) && out[0];
    free(t);
    return ok;
}

int lib_saves_using(Lib *L, int i, wchar_t *item, int itemn, wchar_t *savename, int saven) {
    item[0] = savename[0] = 0;
    Mod *row = &L->mods[i];
    if (row->kind != MK_TACK && row->kind != MK_CLOTHES) return 0;
    /* what to look for: the row itself, or every item in its pack */
    static char ids[512][160];
    static wchar_t names[512][128];
    int nid = 0;
    for (int j = 0; j < L->n && nid < 512; j++) {
        Mod *m = &L->mods[j];
        int member = (row->pkg >= 0 && !row->hidden) ? (m->hidden && m->pkg == row->pkg) : j == i;
        if (!member) continue;
        char id[160];
        if (item_game_id(L, m, id, sizeof id)) {
            /* the save has it in quotes: "item.tack.saddlepad.x" */
            snprintf(ids[nid], sizeof ids[nid], "\"%s\"", id);
            wcscpy(names[nid++], m->name);
        }
    }
    if (!nid) return 0;
    wchar_t games[MAX_PATH], pat[MAX_PATH], save[MAX_PATH];
    games_dir(L, games);
    swprintf(pat, MAX_PATH, L"%ls\\*", games);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    int found = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        swprintf(save, MAX_PATH, L"%ls\\%ls\\save.json", games, fd.cFileName);
        DWORD n;
        char *t = slurp(save, 256u << 20, &n);
        if (!t) continue;
        for (int k = 0; k < nid; k++)
            if (strstr(t, ids[k])) {
                if (!found) {
                    wcsncpy(item, names[k], itemn - 1);
                    item[itemn - 1] = 0;
                    wcsncpy(savename, fd.cFileName, saven - 1);
                    savename[saven - 1] = 0;
                }
                found++;
                break;
            }
        free(t);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return found;
}

static int cmp_wstr(const void *a, const void *b) { return wcscmp((const wchar_t *)a, (const wchar_t *)b); }

int lib_backup_saves(Lib *L, wchar_t *where, int wheren) {
    wchar_t games[MAX_PATH + 2] = {0}, root[MAX_PATH], dest[MAX_PATH + 2] = {0};
    games_dir(L, games);
    if (!is_dir(games)) return 0;
    swprintf(root, MAX_PATH, L"%ls\\Save backups", L->data);
    mkdirs(root);
    SYSTEMTIME t;
    GetLocalTime(&t);
    swprintf(dest, MAX_PATH, L"%ls\\%04d-%02d-%02d %02d.%02d.%02d", root, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    /* games and dest end w/ two nulls for SHFileOperation */
    SHFILEOPSTRUCTW op = {.wFunc = FO_COPY, .pFrom = games, .pTo = dest,
                          .fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR};
    if (SHFileOperationW(&op) || op.fAnyOperationsAborted || !is_dir(dest)) return 0;
    if (where) swprintf(where, wheren, L"%ls", dest);
    /* keep the newest 10 backups (names sort by date) */
    static wchar_t names[256][MAX_PATH];
    int n = 0;
    wchar_t pat[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\*", root);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.' && n < 256) wcscpy(names[n++], fd.cFileName);
        while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    qsort(names, n, sizeof names[0], cmp_wstr);
    for (int k = 0; k + 10 < n; k++) {
        wchar_t old[MAX_PATH + 2] = {0};
        swprintf(old, MAX_PATH, L"%ls\\%ls", root, names[k]);
        SHFILEOPSTRUCTW del = {.wFunc = FO_DELETE, .pFrom = old, .fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI};
        SHFileOperationW(&del);
    }
    return 1;
}

static int find_preview(Lib *L, int i, wchar_t *out);

/* kept per row till the next scan bc it gets asked for on every redraw */
int lib_preview_path(Lib *L, int i, wchar_t *out) {
    Mod *m = &L->mods[i];
    if (!m->pic_state) m->pic_state = find_preview(L, i, m->pic) ? 1 : 2;
    if (m->pic_state == 1) wcscpy(out, m->pic);
    return m->pic_state == 1;
}

static int find_preview(Lib *L, int i, wchar_t *out) {
    Mod *m = &L->mods[i];
    if (m->kind != MK_TACK && m->kind != MK_CLOTHES) return 0;
    if (m->pkg >= 0 && !m->hidden) { /* pack row: first items pic */
        for (int j = 0; j < L->n; j++)
            if (L->mods[j].hidden && L->mods[j].pkg == m->pkg) return lib_preview_path(L, j, out);
        return 0;
    }
    wchar_t dir[MAX_PATH], js[MAX_PATH];
    content_dir(L, m->kind, m->enabled, dir);
    /* the games shop pic, if the item has one */
    swprintf(js, MAX_PATH, L"%ls\\%ls.json", dir, m->id);
    DWORD n;
    char *t = slurp(js, 1 << 16, &n);
    if (t) {
        char pic[MAX_PATH];
        int found = json_str(t, "spriteShopLayer1", pic, sizeof pic) && pic[0] && !strpbrk(pic, "\\/:");
        free(t);
        if (found) {
            wchar_t wp[MAX_PATH];
            MultiByteToWideChar(CP_UTF8, 0, pic, -1, wp, MAX_PATH);
            swprintf(out, MAX_PATH, L"%ls\\%ls", dir, wp);
            if (exists(out)) return 1;
        }
    }
    swprintf(out, MAX_PATH, L"%ls\\%ls.png", dir, m->id);
    return exists(out);
}

int lib_game_running(void) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {.dwSize = sizeof pe};
    int found = 0;
    for (BOOL ok = Process32FirstW(s, &pe); ok && !found; ok = Process32NextW(s, &pe))
        if (!_wcsicmp(pe.szExeFile, GAME_EXE)) found = 1;
    CloseHandle(s);
    return found;
}


/* puts the loader (hid.dll) in place or updates it. never touches other mods files like
   version.dll (StickHorse, MelonLoader) or winhttp.dll (BepInEx) */
int lib_install_loader(Lib *L, wchar_t *err, int errn) {
    if (!L->game[0]) return swprintf(err, errn, L"The game folder was not found."), 0;
    if (lib_game_running()) return swprintf(err, errn, L"Close the game first."), 0;
    detect_loader(L);
    if (L->loader == 2)
        return swprintf(err, errn, L"Another mod is already using " LOADER_FILE L" in the game folder."), 0;
    wchar_t dll[MAX_PATH], mods[MAX_PATH], src[MAX_PATH];
    loader_source(src);
    if (!exists(src))
        return swprintf(err, errn, L"The loader is missing: keep the loader folder next to HoofMods.exe (it came in the zip)."), 0;
    swprintf(dll, MAX_PATH, L"%ls\\" LOADER_FILE, L->game);
    swprintf(mods, MAX_PATH, L"%ls\\Mods", L->game);
    mkdirs(mods);
    if (!CopyFileW(src, dll, FALSE)) {
        swprintf(err, errn, L"Could not copy " LOADER_FILE L" into the game folder (error %lu).", GetLastError());
        return 0;
    }
    detect_loader(L);
    return 1;
}

void lib_open_folder(Lib *L, ModKind k) {
    wchar_t p[MAX_PATH];
    if (k == MK_DLL) swprintf(p, MAX_PATH, L"%ls\\Mods", L->game);
    else if (k == MK_BEPINEX) swprintf(p, MAX_PATH, L"%ls\\BepInEx\\plugins", L->game);
    else content_dir(L, k, 1, p);
    mkdirs(p);
    ShellExecuteW(NULL, L"open", p, NULL, NULL, SW_SHOWNORMAL);
}

int lib_start_without_mods(Lib *L) {
    wchar_t p[MAX_PATH];
    if (!L->data[0]) return 0;
    swprintf(p, MAX_PATH, L"%ls\\start_without_mods", L->data);
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(f);
    lib_start_game(L);
    return 1;
}

void lib_start_game(Lib *L) {
    wchar_t p[MAX_PATH];
    if (L->appid[0]) {
        swprintf(p, MAX_PATH, L"steam://rungameid/%ls", L->appid);
        ShellExecuteW(NULL, L"open", p, NULL, NULL, SW_SHOWNORMAL);
    } else {
        swprintf(p, MAX_PATH, L"%ls\\" GAME_EXE, L->game);
        ShellExecuteW(NULL, L"open", p, NULL, L->game, SW_SHOWNORMAL);
    }
}
