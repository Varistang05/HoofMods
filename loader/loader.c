/* HoofMods loader: starts every enabled DLL mod found in <game>\Mods\<mod folder>\.
 *
 * Mods\HoofMods.ini   [Enabled] <folder>=0 turns a mod off (missing = on, so a
 *                     folder dropped in by hand just works).
 * Mods\<folder>\mod.ini  optional [Mod] Main=<file>.dll picks the one DLL to load;
 *                     without it every DLL directly in the folder is loaded.
 * %LOCALAPPDATA%\HoofMods\start_without_mods (written by the app's "Start without mods")
 *                     makes the next launch load no mods; the loader deletes it right away.
 * Log:    %LOCALAPPDATA%\HoofMods\loader_log.txt
 * Result: %LOCALAPPDATA%\HoofMods\last_launch.ini (read by the app)
 *
 * Keep-out list: %LOCALAPPDATA%\HoofMods\keep_out.txt (one path per line, written by the app)
 * names tack/clothes/language files the player turned off or removed. Steam Cloud syncs the
 * game's save folder (which holds its Mods folder) and puts such files back before the game
 * starts. They are removed again here, inside the game session, so Steam records the removal
 * when the game closes. Only files inside the game's own Mods folder are ever touched.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define HM_VERSION 2
#define MAX_MODS 256

void proxy_init(void);

static wchar_t g_data[MAX_PATH];  /* %LOCALAPPDATA%\HoofMods */
static wchar_t g_status[MAX_PATH];
static FILE *g_log;

static void lg(const char *fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
}

static void status(const wchar_t *mod, const wchar_t *text) {
    WritePrivateProfileStringW(L"Mods", mod, text, g_status);
}

static int cmp_names(const void *a, const void *b) {
    return _wcsicmp((const wchar_t *)a, (const wchar_t *)b);
}

/* Exported so the app (and mods) can tell this hid.dll is the HoofMods loader. */
__declspec(dllexport) int HoofMods_Version(void) { return HM_VERSION; }

static wchar_t g_game[MAX_PATH];

static char *read_file(const wchar_t *p, DWORD *n) {
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(f, NULL), got = 0;
    char *b = malloc(sz ? sz : 1);
    if (b && (!ReadFile(f, b, sz, &got, NULL) || got != sz)) free(b), b = NULL;
    CloseHandle(f);
    *n = sz;
    return b;
}

/* The same file already loads itself from the game folder (a mod installed both the normal way,
   e.g. as version.dll, and through HoofMods): starting it twice would double every action. */
static int running_from_game_folder(const wchar_t *path) {
    static const wchar_t *names[] = {L"version.dll", L"winmm.dll", L"winhttp.dll", L"wininet.dll", L"dinput8.dll", L"dxgi.dll",
                                     L"d3d9.dll", L"d3d11.dll", L"d3d12.dll", L"dsound.dll", L"xinput1_3.dll", L"xinput1_4.dll",
                                     L"xinput9_1_0.dll", L"dbghelp.dll", L"iphlpapi.dll", L"opengl32.dll"};
    DWORD n;
    char *mine = read_file(path, &n);
    if (!mine) return 0;
    int same = 0;
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]) && !same; i++) {
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%ls\\%ls", g_game, names[i]);
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a) || a.nFileSizeLow != n || a.nFileSizeHigh) continue;
        DWORD m;
        char *other = read_file(p, &m);
        same = other && m == n && !memcmp(mine, other, n);
        free(other);
    }
    free(mine);
    return same;
}

static int load_one(const wchar_t *mod, const wchar_t *path) {
    if (running_from_game_folder(path)) {
        lg("skipped %ls: the same file already loads from the game folder", path);
        status(mod, L"skipped");
        return 2;
    }
    HMODULE h = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (h) {
        lg("loaded %ls", path);
        return 1;
    }
    DWORD e = GetLastError();
    lg("FAILED %ls (error %lu)", path, e);
    wchar_t msg[64];
    swprintf(msg, 64, L"failed (error %lu)", e);
    status(mod, msg);
    return 0;
}

static void load_mod(const wchar_t *mods, const wchar_t *name, const wchar_t *ini) {
    if (!GetPrivateProfileIntW(L"Enabled", name, 1, ini)) {
        lg("%ls: turned off", name);
        status(name, L"off");
        return;
    }
    wchar_t dir[MAX_PATH], path[MAX_PATH], main[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%ls\\%ls", mods, name);

    swprintf(path, MAX_PATH, L"%ls\\mod.ini", dir);
    GetPrivateProfileStringW(L"Mod", L"Main", L"", main, MAX_PATH, path);
    if (main[0]) {
        swprintf(path, MAX_PATH, L"%ls\\%ls", dir, main);
        if (load_one(name, path) == 1) status(name, L"loaded");
        return;
    }

    int found = 0, ok = 0, skipped = 0;
    WIN32_FIND_DATAW fd;
    swprintf(path, MAX_PATH, L"%ls\\*.dll", dir);
    HANDLE f = FindFirstFileW(path, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            swprintf(path, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            found++;
            int r = load_one(name, path);
            ok += r == 1;
            skipped += r == 2;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    if (!found) {
        lg("%ls: no DLL (content-only mod, nothing to start)", name);
        status(name, L"no dll");
    } else if (ok && ok + skipped == found) {
        status(name, L"loaded");
    }
}

static DWORD WINAPI load_all(void *unused) {
    (void)unused;
    wchar_t game[MAX_PATH], mods[MAX_PATH], ini[MAX_PATH], pat[MAX_PATH];
    GetModuleFileNameW(NULL, game, MAX_PATH);
    wchar_t *sl = wcsrchr(game, L'\\');
    if (sl) *sl = 0;
    wcscpy(g_game, game);
    swprintf(mods, MAX_PATH, L"%ls\\Mods", game);
    swprintf(ini, MAX_PATH, L"%ls\\HoofMods.ini", mods);

    WritePrivateProfileStringW(L"Mods", NULL, NULL, g_status); /* clear last launch */
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t when[32];
    swprintf(when, 32, L"%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    WritePrivateProfileStringW(L"Launch", L"Time", when, g_status);

    wchar_t once[MAX_PATH];
    swprintf(once, MAX_PATH, L"%ls\\start_without_mods", g_data);
    if (g_data[0] && GetFileAttributesW(once) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(once); /* only this one start */
        lg("Start without mods: no mods loaded this time");
        WritePrivateProfileStringW(L"Launch", L"SafeMode", L"1", g_status);
        return 0;
    }
    WritePrivateProfileStringW(L"Launch", L"SafeMode", L"0", g_status);

    static wchar_t names[MAX_MODS][MAX_PATH];
    int n = 0;
    WIN32_FIND_DATAW fd;
    swprintf(pat, MAX_PATH, L"%ls\\*", mods);
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) {
        lg("no Mods folder at %ls", mods);
        return 0;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.' || fd.cFileName[0] == L'_') continue; /* _ = reserved for the app */
        if (n < MAX_MODS) wcscpy(names[n++], fd.cFileName);
    } while (FindNextFileW(f, &fd));
    FindClose(f);

    qsort(names, n, sizeof names[0], cmp_names);
    lg("%d mod folder(s) in %ls", n, mods);
    for (int i = 0; i < n; i++) load_mod(mods, names[i], ini);
    lg("done");
    return 0;
}

/* Is `p` a file directly or deeper inside `root` (which ends with a backslash)? */
static int under(const wchar_t *p, const wchar_t *root) {
    size_t k = wcslen(root);
    return k > 3 && !_wcsnicmp(p, root, k) && p[k] && !wcsstr(p, L"..");
}

static void keep_out(void) {
    wchar_t list[MAX_PATH], root1[MAX_PATH], root2[MAX_PATH] = L"", ini[MAX_PATH], over[MAX_PATH];
    swprintf(list, MAX_PATH, L"%ls\\keep_out.txt", g_data);
    HANDLE f = CreateFileW(list, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(f, NULL), got = 0;
    char *buf = malloc(sz + 1);
    if (!buf || !ReadFile(f, buf, sz, &got, NULL)) got = 0;
    CloseHandle(f);
    if (!buf) return;
    buf[got] = 0;

    /* the game's Mods folder, plus the app's override of it if one is set */
    wchar_t prof[MAX_PATH];
    if (!GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH)) prof[0] = 0;
    swprintf(root1, MAX_PATH, L"%ls\\AppData\\LocalLow\\ThogliStudios\\Horses of Hoofprint Bay\\Saves~\\Mods\\", prof);
    swprintf(ini, MAX_PATH, L"%ls\\app.ini", g_data);
    GetPrivateProfileStringW(L"App", L"Content", L"", over, MAX_PATH, ini);
    if (over[0]) swprintf(root2, MAX_PATH, L"%ls\\", over);

    /* entries for files still put back by Steam stay listed; the rest are done */
    char *keep = malloc(got + 1), *k = keep;
    int removed = 0, done = 0;
    for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        wchar_t p[MAX_PATH];
        if (!MultiByteToWideChar(CP_UTF8, 0, line, -1, p, MAX_PATH) || !p[0]) continue;
        if (!under(p, root1) && !(root2[0] && under(p, root2))) {
            lg("keep-out: skipped %ls (outside the game's Mods folder)", p);
            continue;
        }
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) {
            done++;
            continue;
        }
        if (DeleteFileW(p)) {
            removed++;
            lg("keep-out: removed %ls", p);
        } else {
            lg("keep-out: could not remove %ls (error %lu)", p, GetLastError());
        }
        size_t l = strlen(line);
        memcpy(k, line, l);
        k += l;
        *k++ = '\n';
    }
    lg("keep-out: %d file(s) removed, %d already gone", removed, done);
    if (k == keep) DeleteFileW(list);
    else {
        f = CreateFileW(list, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(f, keep, (DWORD)(k - keep), &w, NULL);
            CloseHandle(f);
        }
    }
    free(keep);
    free(buf);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID unused) {
    (void)unused;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        proxy_init();
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", g_data, MAX_PATH)) {
            wcscat(g_data, L"\\HoofMods");
            CreateDirectoryW(g_data, NULL);
            wchar_t p[MAX_PATH];
            swprintf(p, MAX_PATH, L"%ls\\loader_log.txt", g_data);
            g_log = _wfopen(p, L"w");
            swprintf(g_status, MAX_PATH, L"%ls\\last_launch.ini", g_data);
        }
        lg("HoofMods loader %d", HM_VERSION);
        /* right away, before the game reads its Mods folder (file calls only, safe here) */
        if (g_data[0]) keep_out();
        /* Mods are loaded on a new thread: loading DLLs from inside DllMain can deadlock. */
        HANDLE th = CreateThread(NULL, 0, load_all, NULL, 0, NULL);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
