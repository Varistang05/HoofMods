/* installing mods from a zip, a folder or loose files */
#include "mods.h"
#include "zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

typedef struct {
    wchar_t rel[MAX_PATH]; /* path inside the download, w/ \ between folders */
    int zi;                /* zip entry, or minus 1 for a file on disk */
    wchar_t file[MAX_PATH];
} Ent;

typedef struct {
    Ent *e;
    int n, cap;
    Zip zip;
    int is_zip;
    wchar_t name[128]; /* download name w/o the extension */
} Src;

static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

static Ent *src_add(Src *s, const wchar_t *rel) {
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->e = realloc(s->e, s->cap * sizeof(Ent));
    }
    Ent *e = &s->e[s->n++];
    memset(e, 0, sizeof *e);
    wcsncpy(e->rel, rel, MAX_PATH - 1);
    e->zi = -1;
    return e;
}

static void add_folder(Src *s, const wchar_t *dir, const wchar_t *rel) {
    wchar_t pat[MAX_PATH], p[MAX_PATH], r[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        swprintf(p, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        swprintf(r, MAX_PATH, L"%ls\\%ls", rel, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) add_folder(s, p, r);
        else wcscpy(src_add(s, r)->file, p);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

static unsigned char *read_disk(const wchar_t *path, size_t *len) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(f, NULL), got = 0;
    unsigned char *b = malloc(sz ? sz : 1);
    if (b && !ReadFile(f, b, sz, &got, NULL)) got = 0;
    CloseHandle(f);
    if (got != sz) return free(b), NULL;
    *len = sz;
    return b;
}

static unsigned char *src_read(Src *s, int i, size_t *len) {
    const char *err;
    if (s->e[i].zi >= 0) {
        *len = s->zip.e[s->e[i].zi].usize;
        return zip_read(&s->zip, s->e[i].zi, &err);
    }
    return read_disk(s->e[i].file, len);
}

/* ==== path helpers ==== */

static const wchar_t *ext_of(const wchar_t *p) {
    const wchar_t *d = wcsrchr(p, L'.'), *sl = wcsrchr(p, L'\\');
    return d && (!sl || d > sl) ? d : L"";
}

static const wchar_t *file_of(const wchar_t *p) {
    const wchar_t *sl = wcsrchr(p, L'\\');
    return sl ? sl + 1 : p;
}

/* index right after the folder called name (any case), or minus 1 */
static int seg_after(const wchar_t *p, const wchar_t *name) {
    size_t k = wcslen(name);
    for (const wchar_t *c = p; *c;) {
        const wchar_t *e = wcschr(c, L'\\');
        if (!e) return -1; /* the last part is the file itself */
        if ((size_t)(e - c) == k && !_wcsnicmp(c, name, k)) return (int)(e + 1 - p);
        c = e + 1;
    }
    return -1;
}

/* does any folder in the path have word in it? (any case) */
static int folder_has(const wchar_t *p, const wchar_t *word) {
    wchar_t low[MAX_PATH];
    int i = 0;
    for (; p[i] && i < MAX_PATH - 1; i++) low[i] = towlower(p[i]);
    low[i] = 0;
    wchar_t *sl = wcsrchr(low, L'\\');
    if (!sl) return 0;
    *sl = 0;
    return wcsstr(low, word) != NULL;
}

static int dir_len(const wchar_t *p) {
    const wchar_t *sl = wcsrchr(p, L'\\');
    return sl ? (int)(sl - p) + 1 : 0;
}

/* how much folder path every file shares (to skip a wrapper folder like "MyMod 1.0\") */
static int common_root(Src *s, int (*want)(const Ent *)) {
    int len = -1;
    const wchar_t *first = NULL;
    for (int i = 0; i < s->n; i++) {
        if (want && !want(&s->e[i])) continue;
        int d = dir_len(s->e[i].rel);
        if (!first) {
            first = s->e[i].rel;
            len = d;
            continue;
        }
        if (d < len) len = d;
        int k = 0;
        while (k < len && towlower(first[k]) == towlower(s->e[i].rel[k])) k++;
        while (k > 0 && first[k - 1] != L'\\') k--;
        len = k;
    }
    return len < 0 ? 0 : len;
}

static void sanitize(const wchar_t *in, wchar_t *out, int outn) {
    int k = 0;
    for (const wchar_t *c = in; *c && k < outn - 1; c++)
        if (iswalnum(*c) || wcschr(L" -_.()'&", *c)) out[k++] = *c;
    while (k && (out[k - 1] == L' ' || out[k - 1] == L'.')) k--;
    out[k] = 0;
    wchar_t *st = out;
    while (*st == L' ') st++;
    memmove(out, st, (wcslen(st) + 1) * sizeof(wchar_t));
    if (!out[0]) wcscpy(out, L"Mod");
}

/* download names come w/ extra junk from the site, like the nexus id, version and upload time at the end,
   or a patreon date stamp + random code. we cut that off so just the name is left */
static void pretty_name(const wchar_t *in, wchar_t *out, int outn) {
    wcsncpy(out, in, outn - 1);
    out[outn - 1] = 0;
    for (wchar_t *c = out; *c; c++) /* cut it off at a date stamp and everything after */
        if (iswdigit(c[0]) && iswdigit(c[1]) && iswdigit(c[2]) && iswdigit(c[3]) && c[4] == L'-' && iswdigit(c[5]) && iswdigit(c[6]) &&
            c[7] == L'-' && iswdigit(c[8]) && iswdigit(c[9]) && c > out) {
            *c = 0;
            break;
        }
    for (;;) { /* drop number only bits and separators at the end */
        size_t k = wcslen(out);
        while (k && (out[k - 1] == L'-' || out[k - 1] == L'_' || out[k - 1] == L' ')) out[--k] = 0;
        size_t j = k;
        while (j && iswdigit(out[j - 1])) j--;
        if (j == k || j == 0 || (out[j - 1] != L'-' && out[j - 1] != L'_' && out[j - 1] != L' ')) break;
        out[j - 1] = 0;
    }
    for (wchar_t *c = out; *c; c++)
        if (*c == L'_') *c = L' ';
    if (!out[0]) wcsncpy(out, in, outn - 1);
}

/* the games tack/clothes files say what they are: "id": "item.tack..." or "item.clothes..." / "item.rider..." */
static int json_kind(const unsigned char *d, size_t n) {
    char tmp[4097];
    size_t k = n < 4096 ? n : 4096;
    memcpy(tmp, d, k);
    tmp[k] = 0;
    const char *s = strstr(tmp, "\"id\"");
    if (!s) return -1;
    s = strchr(s + 4, '"');
    if (!s) return -1;
    if (!_strnicmp(s + 1, "item.tack.", 10)) return MK_TACK;
    if (!_strnicmp(s + 1, "item.clothes.", 13) || !_strnicmp(s + 1, "item.rider.", 11)) return MK_CLOTHES;
    return -1;
}

/* value of key= in a small UTF8 ini thats already in memory */
static int ini_value(const unsigned char *d, size_t n, const char *key, wchar_t *out, int outn) {
    size_t k = strlen(key);
    for (size_t i = 0; i < n; i++) {
        if (i && d[i - 1] != '\n') continue;
        size_t j = i;
        while (j < n && (d[j] == ' ' || d[j] == '\t')) j++;
        if (j + k < n && !_strnicmp((const char *)d + j, key, k) && d[j + k] == '=') {
            size_t a = j + k + 1, b = a;
            while (b < n && d[b] != '\r' && d[b] != '\n') b++;
            char tmp[512];
            size_t len = b - a < sizeof tmp - 1 ? b - a : sizeof tmp - 1;
            memcpy(tmp, d + a, len);
            tmp[len] = 0;
            MultiByteToWideChar(CP_UTF8, 0, tmp, -1, out, outn);
            return 1;
        }
    }
    return 0;
}

/* 1 = .NET DLL (BepInEx mod), 0 = normal DLL, minus 1 = not a DLL */
static int is_managed(const unsigned char *d, size_t n) {
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return -1;
    size_t pe = rd32(d + 0x3C);
    if (pe + 26 > n || rd32(d + pe) != 0x4550) return -1;
    size_t dd = pe + 24 + (rd16(d + pe + 24) == 0x20B ? 112 : 96);
    if (dd + 15 * 8 > n) return -1;
    return rd32(d + dd + 14 * 8) != 0;
}

static int write_out(const wchar_t *path, const unsigned char *d, size_t n, wchar_t *msg, int msgn) {
    wchar_t dir[MAX_PATH];
    wcscpy(dir, path);
    wchar_t *sl = wcsrchr(dir, L'\\');
    if (sl) *sl = 0;
    hm_mkdirs(dir);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w = 0;
    BOOL ok = f != INVALID_HANDLE_VALUE && WriteFile(f, d, (DWORD)n, &w, NULL) && w == n;
    DWORD e = GetLastError();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) {
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED)
            swprintf(msg, msgn, L"%ls is in use. Close the game and try again.", file_of(path));
        else
            swprintf(msg, msgn, L"Could not write %ls (error %lu).", file_of(path), e);
    }
    return ok;
}

static int copy_ent(Src *s, int i, const wchar_t *dest, wchar_t *msg, int msgn) {
    size_t n = 0;
    unsigned char *d = src_read(s, i, &n);
    if (!d) return swprintf(msg, msgn, L"Could not read %ls from the download.", file_of(s->e[i].rel)), 0;
    int ok = write_out(dest, d, n, msg, msgn);
    free(d);
    return ok;
}

static void save_package(Lib *L, const wchar_t *id, const wchar_t *name, ModKind kind, wchar_t (*items)[MAX_PATH], int n) {
    wchar_t dir[MAX_PATH], ini[MAX_PATH], key[16];
    swprintf(dir, MAX_PATH, L"%ls\\packages", L->data);
    hm_mkdirs(dir);
    swprintf(ini, MAX_PATH, L"%ls\\%ls.ini", dir, id);
    DeleteFileW(ini);
    WritePrivateProfileStringW(L"Package", L"Name", name, ini);
    swprintf(key, 16, L"%d", (int)kind);
    WritePrivateProfileStringW(L"Package", L"Kind", key, ini);
    wchar_t cnt[16];
    swprintf(cnt, 16, L"%d", n);
    WritePrivateProfileStringW(L"Package", L"Items", cnt, ini);
    for (int i = 0; i < n; i++) {
        swprintf(key, 16, L"%d", i);
        WritePrivateProfileStringW(L"Items", key, items[i], ini);
    }
}

static int add_unique(wchar_t (*items)[MAX_PATH], int n, int max, const wchar_t *id) {
    for (int i = 0; i < n; i++)
        if (!_wcsicmp(items[i], id)) return n;
    if (n < max) wcsncpy(items[n++], id, MAX_PATH - 1);
    return n;
}

#define MAX_ITEMS 512
static wchar_t g_items[MAX_ITEMS][MAX_PATH];

/* ==== the three kinds of download ==== */

/* BepInEx itself (winhttp.dll + doorstop next to its BepInEx folder): it all goes in the game folder,
   then its config gets set up for this game b4 the first start */
static int install_bepinex_core(Lib *L, Src *s, int starter, wchar_t *msg, int msgn) {
    int il2cpp = 0, mono = 0;
    for (int i = 0; i < s->n; i++) {
        if (!_wcsicmp(file_of(s->e[i].rel), L"BepInEx.Unity.IL2CPP.dll")) il2cpp = 1;
        if (!_wcsicmp(file_of(s->e[i].rel), L"BepInEx.Unity.Mono.dll") || !_wcsicmp(file_of(s->e[i].rel), L"BepInEx.dll")) mono = 1;
    }
    if (!il2cpp && mono) /* ppl prob grab the wrong one bc the download page has both right next to each other */
        return swprintf(msg, msgn,
                        L"That's the Mono version of BepInEx, but this game needs the IL2CPP one. Press Get BepInEx in the BepInEx section to "
                        L"download the right file (the IL2CPP win x64 one, build 793), then drag that zip in."),
               0;
    size_t n = 0;
    unsigned char *w = src_read(s, starter, &n);
    int x64 = w && n > 0x40 && rd32(w + 0x3C) + 6 < n && rd16(w + rd32(w + 0x3C) + 4) == 0x8664;
    free(w);
    if (!il2cpp || !x64)
        return swprintf(msg, msgn, L"This BepInEx won't work with Horses of Hoofprint Bay. The game needs BepInEx 6 for IL2CPP, the win x64 one."), 0;
    if (lib_game_running()) return swprintf(msg, msgn, L"Close the game first, then drag BepInEx in again."), 0;
    int root = dir_len(s->e[starter].rel); /* wherever winhttp.dll is, thats the game folder */
    for (int i = 0; i < s->n; i++) {
        if ((int)wcslen(s->e[i].rel) <= root || _wcsnicmp(s->e[i].rel, s->e[starter].rel, root)) continue;
        wchar_t dest[MAX_PATH];
        swprintf(dest, MAX_PATH, L"%ls\\%ls", L->game, s->e[i].rel + root);
        if (!copy_ent(s, i, dest, msg, msgn)) return 0;
    }
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%ls\\app.ini", L->data);
    WritePrivateProfileStringW(L"BepInEx", L"InstalledByHoofMods", L"1", ini);
    hm_bepinex_config(L);
    L->bepinex = 1;
    swprintf(msg, msgn, L"BepInEx is installed. The next game start takes a minute or two while it sets itself up.");
    return 1;
}

static int install_bepinex_tree(Lib *L, Src *s, const wchar_t *pname, wchar_t *msg, int msgn) {
    for (int i = 0; i < s->n; i++) {
        const wchar_t *f = file_of(s->e[i].rel);
        if (!_wcsicmp(f, L"winhttp.dll") || !_wcsicmp(f, L"doorstop_config.ini")) {
            for (int j = 0; j < s->n; j++)
                if (!_wcsicmp(file_of(s->e[j].rel), L"winhttp.dll")) return install_bepinex_core(L, s, j, msg, msgn);
            return swprintf(msg, msgn, L"This BepInEx download is missing winhttp.dll."), 0;
        }
    }
    int n = 0, files = 0, data = 0;
    for (int i = 0; i < s->n; i++) {
        int a = seg_after(s->e[i].rel, L"BepInEx");
        if (a < 0) {
            /* some plugins bring their own stuff for the games save Mods folder too (like Paint the Bay's Mods\Coats packs).
               b4 this they just got skipped and the plugin ran w/ nothing to use */
            int m = seg_after(s->e[i].rel, L"Mods");
            if (m < 0 || !L->content[0]) continue;
            wchar_t dest[MAX_PATH];
            swprintf(dest, MAX_PATH, L"%ls\\%ls", L->content, s->e[i].rel + m);
            /* a file right in the plugins own folder (Mods\Coats\assignments.json) is the players own settings,
               an update doesnt get to wipe it. pack files deeper down do get updated */
            const wchar_t *sub = wcschr(s->e[i].rel + m, L'\\');
            if (sub && !wcschr(sub + 1, L'\\') && GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) continue;
            if (!copy_ent(s, i, dest, msg, msgn)) return 0;
            data++;
            continue;
        }
        wchar_t dest[MAX_PATH];
        swprintf(dest, MAX_PATH, L"%ls\\BepInEx\\%ls", L->game, s->e[i].rel + a);
        if (!copy_ent(s, i, dest, msg, msgn)) return 0;
        files++;
        int p = seg_after(s->e[i].rel, L"plugins");
        if (p >= 0) {
            wchar_t top[MAX_PATH];
            wcsncpy(top, s->e[i].rel + p, MAX_PATH - 1);
            wchar_t *sl = wcschr(top, L'\\');
            if (sl) *sl = 0;
            n = add_unique(g_items, n, MAX_ITEMS, top);
        }
    }
    wchar_t id[64];
    sanitize(pname, id, 64);
    if (n) save_package(L, id, pname, MK_BEPINEX, g_items, n);
    wchar_t extra[96] = L"";
    if (data) swprintf(extra, 96, L" and %d into the game's Mods folder", data);
    swprintf(msg, msgn, L"Installed %ls (%d file%ls into BepInEx%ls).%ls", pname, files, files == 1 ? L"" : L"s", extra,
             L->bepinex ? L"" : L" It needs BepInEx to run.");
    return 1;
}

static int want_any(const Ent *e) {
    (void)e;
    return 1;
}

/* "StickHorse v1.0" becomes "StickHorse" + "1.0", so a newer download updates the same folder */
static void split_version(wchar_t *name, wchar_t *ver, int vern) {
    ver[0] = 0;
    wchar_t *sp = wcsrchr(name, L' ');
    if (!sp || sp == name) return;
    wchar_t *v = sp + 1;
    if (*v == L'v' || *v == L'V') v++;
    if (!iswdigit(*v)) return;
    for (wchar_t *c = v; *c; c++)
        if (!iswdigit(*c) && *c != L'.') return;
    wcsncpy(ver, v, vern - 1);
    ver[vern - 1] = 0;
    *sp = 0;
}

/* is the same mod already loading from the game folder (like StickHorse as version.dll)?
   same bytes as what we just installed, or the file has the mods name in it */
static void find_twin(Lib *L, const wchar_t *name, const unsigned char *dll, size_t dll_n) {
    L->twin[0] = 0;
    char ascii[128];
    int an = WideCharToMultiByte(CP_UTF8, 0, name, -1, ascii, sizeof ascii, NULL, NULL) - 1;
    for (int i = 0; PROXY_NAMES[i] && !L->twin[0]; i++) {
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%ls\\%ls", L->game, PROXY_NAMES[i]);
        size_t n;
        unsigned char *d = read_disk(p, &n);
        if (!d) continue;
        int same = dll && n == dll_n && !memcmp(d, dll, n);
        int named = 0;
        if (an >= 5)
            for (size_t k = 0; k + an <= n && !named; k++) named = !memcmp(d + k, ascii, an);
        if (same || named) swprintf(L->twin, MAX_PATH, L"@%ls", PROXY_NAMES[i]);
        free(d);
    }
}

static int want_modini(const Ent *e) { return !_wcsicmp(file_of(e->rel), L"mod.ini"); }
static int want_dll(const Ent *e) { return !_wcsicmp(ext_of(e->rel), L".dll"); }
static int is_source(const Ent *e) {
    static const wchar_t *src[] = {L".c", L".h", L".cpp", L".cs", L".sln", L".csproj", L".vcxproj"};
    for (int i = 0; i < 7; i++)
        if (!_wcsicmp(ext_of(e->rel), src[i])) return 1;
    return 0;
}

static int install_dlls(Lib *L, Src *s, const wchar_t *pname, int managed, wchar_t *msg, int msgn) {
    /* ppl sometimes drag a whole project folder (src, build\ModName\mod.dll...). if theres a mod.ini
       its folder is the mod, if theres source code the folder w/ the DLLs is. otherwise take it all */
    int (*pick)(const Ent *) = want_any, any_ini = 0, any_src = 0;
    for (int i = 0; i < s->n; i++) any_ini |= want_modini(&s->e[i]), any_src |= is_source(&s->e[i]);
    if (any_ini) pick = want_modini;
    else if (any_src) pick = want_dll;
    int root = common_root(s, pick);
    wchar_t prefix[MAX_PATH] = L"";
    for (int i = 0; i < s->n; i++)
        if (pick(&s->e[i])) {
            wcsncpy(prefix, s->e[i].rel, root);
            prefix[root] = 0;
            break;
        }
    wchar_t name[128], id[64], ver[32] = L"";
    int has_ini = 0;
    wcscpy(name, pname);
    for (int i = 0; i < s->n; i++) /* a mod.ini can give the name */
        if (want_modini(&s->e[i]) && !_wcsnicmp(s->e[i].rel, prefix, root)) {
            size_t n;
            unsigned char *d = src_read(s, i, &n);
            if (d) ini_value(d, n, "Name", name, 128), ini_value(d, n, "Version", ver, 32), free(d);
            has_ini = 1;
            break;
        }
    if (!has_ini) split_version(name, ver, 32);
    sanitize(name, id, 64);
    unsigned char *proxy_bytes = NULL;
    size_t proxy_n = 0;
    for (int i = 0; i < s->n; i++) {
        wchar_t dest[MAX_PATH];
        if (_wcsnicmp(s->e[i].rel, prefix, root)) continue; /* outside the mods folder, like src or notes */
        const wchar_t *rel = s->e[i].rel + root;
        /* a DLL made to load as smth like version.dll gets the mods own name here,
           so it cant get mixed up w/ the Windows file */
        int proxy = !managed && !wcschr(rel, L'\\') && hm_is_proxy_name(rel);
        if (managed) swprintf(dest, MAX_PATH, L"%ls\\BepInEx\\plugins\\%ls\\%ls", L->game, id, rel);
        else if (proxy) swprintf(dest, MAX_PATH, L"%ls\\Mods\\%ls\\%ls.dll", L->game, id, id);
        else swprintf(dest, MAX_PATH, L"%ls\\Mods\\%ls\\%ls", L->game, id, rel);
        if (!copy_ent(s, i, dest, msg, msgn)) return free(proxy_bytes), 0;
        if (proxy && !proxy_bytes) proxy_bytes = src_read(s, i, &proxy_n);
    }
    if (!managed && !has_ini) { /* remember name and version for the list */
        wchar_t ini[MAX_PATH];
        swprintf(ini, MAX_PATH, L"%ls\\Mods\\%ls\\mod.ini", L->game, id);
        WritePrivateProfileStringW(L"Mod", L"Name", name, ini);
        if (ver[0]) WritePrivateProfileStringW(L"Mod", L"Version", ver, ini);
    }
    wcscpy(g_items[0], id);
    save_package(L, id, name, managed ? MK_BEPINEX : MK_DLL, g_items, 1);
    if (!managed) find_twin(L, name, proxy_bytes, proxy_n);
    free(proxy_bytes);
    if (managed)
        swprintf(msg, msgn, L"Installed %ls as a BepInEx plugin.%ls", name, L->bepinex ? L"" : L" It needs BepInEx to run.");
    else
        swprintf(msg, msgn, L"Installed %ls.%ls", name,
                 L->loader == 1 || L->loader == 3 ? L" It starts with the game." : L" Install the HoofMods loader so it can start with the game.");
    return 1;
}

static int content_kind_hint(const wchar_t *rel) {
    if (folder_has(rel, L"language") || seg_after(rel, L"lang") >= 0) return MK_LANGUAGE;
    if (folder_has(rel, L"cloth")) return MK_CLOTHES;
    if (folder_has(rel, L"tack")) return MK_TACK;
    return -1;
}

static int install_content(Lib *L, Src *s, const wchar_t *pname, AskKind ask, void *ctx, wchar_t *msg, int msgn) {
    /* items: every .json (w/ its .png and other files w/ the same name), in a Language
       folder every file counts */
    int nitems = 0, kinds[MAX_ITEMS], hinted = -1, mixed = 0;
    int item_of[4096];
    if (s->n > 4096) return swprintf(msg, msgn, L"That download has too many files."), 0;
    for (int i = 0; i < s->n; i++) item_of[i] = -1;
    for (int i = 0; i < s->n && nitems < MAX_ITEMS; i++) {
        const wchar_t *x = ext_of(s->e[i].rel);
        int hint = content_kind_hint(s->e[i].rel);
        if (_wcsicmp(x, L".json") && hint != MK_LANGUAGE) continue;
        unsigned char *jd = NULL;
        size_t jn = 0;
        if (hint != MK_LANGUAGE) { /* the files own id wins over folder names */
            jd = src_read(s, i, &jn);
            int jk = jd ? json_kind(jd, jn) : -1;
            if (jk >= 0) hint = jk;
        }
        wchar_t base[MAX_PATH];
        wcscpy(base, file_of(s->e[i].rel));
        *(wchar_t *)ext_of(base) = 0;
        int it = -1;
        for (int k = 0; k < nitems; k++)
            if (!_wcsicmp(g_items[k], base)) it = k;
        if (it < 0) {
            it = nitems++;
            wcscpy(g_items[it], base);
            kinds[it] = hint;
        }
        if (hint >= 0) {
            if (hinted >= 0 && hinted != hint) mixed = 1;
            hinted = hint;
        }
        /* grab every file in the same folder w/ the same name */
        int dl = dir_len(s->e[i].rel);
        for (int j = 0; j < s->n; j++) {
            if (dir_len(s->e[j].rel) != dl || _wcsnicmp(s->e[j].rel, s->e[i].rel, dl)) continue;
            wchar_t b2[MAX_PATH];
            wcscpy(b2, file_of(s->e[j].rel));
            *(wchar_t *)ext_of(b2) = 0;
            if (!_wcsicmp(b2, base)) item_of[j] = it;
        }
        /* plus every pic the items file names, like "spritesWearingLayer1": ["pad_layer1.png"] */
        for (size_t a = 0; jd && a < jn; a++) {
            if (jd[a] != '"') continue;
            size_t b = a + 1;
            while (b < jn && jd[b] != '"' && b - a < MAX_PATH) b++;
            if (b >= jn) break;
            if (b - a > 5 && !_strnicmp((const char *)jd + b - 4, ".png", 4)) {
                char ref[MAX_PATH];
                memcpy(ref, jd + a + 1, b - a - 1);
                ref[b - a - 1] = 0;
                wchar_t wref[MAX_PATH];
                MultiByteToWideChar(CP_UTF8, 0, ref, -1, wref, MAX_PATH);
                for (int j = 0; j < s->n; j++)
                    if (item_of[j] < 0 && dir_len(s->e[j].rel) == dl && !_wcsnicmp(s->e[j].rel, s->e[i].rel, dl) &&
                        !_wcsicmp(file_of(s->e[j].rel), wref))
                        item_of[j] = it;
            }
            a = b;
        }
        free(jd);
    }
    if (!nitems) return 0;
    int fallback = mixed ? -1 : hinted;
    for (int k = 0; k < nitems; k++)
        if (kinds[k] < 0) {
            if (fallback < 0) {
                fallback = ask(ctx, pname);
                if (fallback < 0) return swprintf(msg, msgn, L"Install cancelled."), -1;
            }
            kinds[k] = fallback;
        }
    int per_kind[MK_COUNT] = {0};
    for (int j = 0; j < s->n; j++) {
        if (item_of[j] < 0) continue;
        wchar_t dir[MAX_PATH], dest[MAX_PATH];
        hm_content_dir(L, kinds[item_of[j]], 1, dir);
        swprintf(dest, MAX_PATH, L"%ls\\%ls", dir, file_of(s->e[j].rel));
        if (!copy_ent(s, j, dest, msg, msgn)) return 0;
        hm_keepout(L, dest, 0); /* installed again after being removed, let it stay */
    }
    /* one pack per kind (a download could have tack and clothes mixed) */
    wchar_t id[64], nm[160];
    int used = 0;
    for (int k = 0; k < nitems; k++) per_kind[kinds[k]]++;
    for (int kind = MK_TACK; kind <= MK_LANGUAGE; kind++) {
        if (!per_kind[kind]) continue;
        static wchar_t list[MAX_ITEMS][MAX_PATH];
        int n = 0;
        for (int k = 0; k < nitems; k++)
            if (kinds[k] == kind) wcscpy(list[n++], g_items[k]);
        if (used++) swprintf(nm, 160, L"%ls (%ls)", pname, KIND_TITLE[kind]);
        else wcscpy(nm, pname);
        sanitize(nm, id, 64);
        save_package(L, id, nm, kind, list, n);
    }
    if (used == 1) {
        int kind = kinds[0];
        swprintf(msg, msgn, L"Installed %ls (%d %ls%ls).", pname, nitems, KIND_ITEM[kind], nitems == 1 ? L"" : L"s");
    } else
        swprintf(msg, msgn, L"Installed %ls (%d items).", pname, nitems);
    if (lib_game_running()) wcscat(msg, L" Restart the game to see it.");
    return 1;
}

/* ==== where it all starts ==== */

int lib_install(Lib *L, const wchar_t *path, AskKind ask, void *ctx, wchar_t *msg, int msgn) {
    Src s = {0};
    msg[0] = 0;
    L->twin[0] = 0;
    const wchar_t *fname = file_of(path);
    wcsncpy(s.name, fname, 127);
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return swprintf(msg, msgn, L"%ls was not found.", fname), 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        add_folder(&s, path, fname);
    } else if (!_wcsicmp(ext_of(path), L".zip")) {
        const char *err = "";
        if (zip_open(&s.zip, path, &err)) {
            wchar_t w[128];
            MultiByteToWideChar(CP_UTF8, 0, err, -1, w, 128);
            return swprintf(msg, msgn, L"Couldn't open %ls: %ls.", fname, w), 0;
        }
        s.is_zip = 1;
        for (int i = 0; i < s.zip.n; i++) {
            if (s.zip.e[i].dir) continue;
            wchar_t rel[MAX_PATH];
            MultiByteToWideChar(CP_UTF8, 0, s.zip.e[i].name, -1, rel, MAX_PATH);
            for (wchar_t *c = rel; *c; c++)
                if (*c == L'/') *c = L'\\';
            if (wcsstr(rel, L"..\\") || rel[0] == L'\\' || wcschr(rel, L':')) continue; /* unsafe paths */
            if (!_wcsnicmp(rel, L"__MACOSX\\", 9)) continue;
            src_add(&s, rel)->zi = i;
        }
    } else if (!_wcsicmp(ext_of(path), L".7z") || !_wcsicmp(ext_of(path), L".rar")) {
        return swprintf(msg, msgn, L"%ls is a %ls archive. Extract it first, then drag the folder here.", fname, ext_of(path) + 1), 0;
    } else {
        wcscpy(src_add(&s, fname)->file, path);
    }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) *(wchar_t *)ext_of(s.name) = 0;
    wchar_t pname[128];
    pretty_name(s.name, pname, 128);

    int r = 0;
    int bep = 0, dlls = 0, managed = 0, native = 0;
    for (int i = 0; i < s.n; i++) {
        if (seg_after(s.e[i].rel, L"BepInEx") >= 0) bep = 1;
        if (!_wcsicmp(ext_of(s.e[i].rel), L".dll")) {
            dlls++;
            size_t n;
            unsigned char *d = src_read(&s, i, &n);
            int m = d ? is_managed(d, n) : -1;
            free(d);
            if (m == 1) managed++;
            else if (m == 0) native++;
        }
    }
    if (!s.n) {
        swprintf(msg, msgn, L"%ls is empty.", fname);
    } else if ((bep || dlls) && !L->game[0]) {
        swprintf(msg, msgn, L"Find the game folder first, then install %ls again.", pname);
    } else if (bep) {
        r = install_bepinex_tree(L, &s, pname, msg, msgn);
    } else if (native) {
        r = install_dlls(L, &s, pname, 0, msg, msgn);
    } else if (managed) {
        r = install_dlls(L, &s, pname, 1, msg, msgn);
    } else {
        r = install_content(L, &s, pname, ask, ctx, msg, msgn);
        if (r == 0 && !msg[0])
            swprintf(msg, msgn, L"HoofMods couldn't tell what kind of mod %ls is. No files were changed.", pname);
        if (r < 0) r = 0;
    }
    if (s.is_zip) zip_close(&s.zip);
    free(s.e);
    return r;
}
