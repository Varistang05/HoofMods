#pragma once
#include <windows.h>

typedef enum { MK_DLL, MK_BEPINEX, MK_TACK, MK_CLOTHES, MK_LANGUAGE, MK_COUNT } ModKind;

typedef struct {
    ModKind kind;
    wchar_t id[MAX_PATH];  /* folder name (DLL), file/folder name (BepInEx) or base file name (content) */
    wchar_t name[128];
    wchar_t info[256];     /* author / version / last launch result */
    int enabled;
    int pkg;               /* index into Lib.pkgs, or -1 */
    int hidden;            /* item shown through its package row instead */
    int count;             /* package rows: number of items */
    int pic_state;         /* 0 not looked up yet, 1 pic found, 2 no picture */
    wchar_t pic[MAX_PATH];
} Mod;

/* A download installed through HoofMods; remembered so a collection shows as one row. */
typedef struct {
    wchar_t id[64];
    wchar_t name[128];
    ModKind kind;
    int nitems;
    wchar_t (*items)[MAX_PATH];
} Pkg;

typedef struct {
    wchar_t game[MAX_PATH];     /* game folder, "" if not found */
    wchar_t content[MAX_PATH];  /* ...\LocalLow\ThogliStudios\Horses of Hoofprint Bay\Saves~\Mods */
    wchar_t data[MAX_PATH];     /* %LOCALAPPDATA%\HoofMods */
    wchar_t appid[16];
    int loader;                 /* 0 none, 1 HoofMods loader (hid.dll), 2 a hid.dll from something else,
                                   3 an older HoofMods loader (update available) */
    int bepinex;
    int bep_on;                 /* BepInEx starts with the game (doorstop_config.ini enabled) */
    Mod *mods;
    int n, cap;
    Pkg *pkgs;
    int npkgs;
    wchar_t twin[MAX_PATH];     /* set by an install when the same mod is also in the game folder (its row id) */
} Lib;

/* DLL names other mods use to load themselves straight from the game folder. A code mod row whose
   id starts with '@' is such a file (e.g. "@version.dll" = StickHorse installed the normal way). */
extern const wchar_t *PROXY_NAMES[];
int hm_is_proxy_name(const wchar_t *file);

extern const wchar_t *KIND_TITLE[MK_COUNT];
extern const wchar_t *KIND_ITEM[MK_COUNT];

void lib_init(Lib *L);
int lib_find_game(Lib *L);
int lib_set_game(Lib *L, const wchar_t *dir);
void lib_scan(Lib *L);
int lib_set_enabled(Lib *L, int i, int on, wchar_t *err, int errn);
int lib_uninstall(Lib *L, int i, wchar_t *err, int errn);
int lib_preview_path(Lib *L, int i, wchar_t *out); /* picture for a tack/clothes row; 0 if none */
/* Tack/clothes only: how many of the game's saves use an item from row i; item and save get the
   first match (e.g. "Blue Ascot Jumping Pad", "Varis"). Saves are only read, never changed. */
int lib_saves_using(Lib *L, int i, wchar_t *item, int itemn, wchar_t *save, int saven);
/* Copies the game's saves to %LOCALAPPDATA%\HoofMods\Save backups\<date time>; keeps the newest 10. */
int lib_backup_saves(Lib *L, wchar_t *where, int wheren);
int lib_install_loader(Lib *L, wchar_t *err, int errn);
int lib_game_running(void);
void lib_open_folder(Lib *L, ModKind k);
void lib_start_game(Lib *L);
int lib_start_without_mods(Lib *L); /* next launch loads no code mods (the loader removes the flag) */
int lib_set_bepinex(Lib *L, int on, wchar_t *err, int errn); /* BepInEx itself on/off (its plugins stay) */

/* install.c: path is a .zip, a folder or a single mod file. ask() is called when the kind
   can't be worked out (returns MK_TACK, MK_CLOTHES or -1 to cancel). Returns 1 on success;
   msg gets a sentence for the user either way. */
typedef int (*AskKind)(void *ctx, const wchar_t *what);
int lib_install(Lib *L, const wchar_t *path, AskKind ask, void *ctx, wchar_t *msg, int msgn);

/* Keep-out list for the loader (see loader.c): add = this game-folder file must stay gone even if
   Steam Cloud puts it back; add = 0 takes it off the list (turned on / installed again). */
void hm_keepout(Lib *L, const wchar_t *path, int add);
/* Sets BepInEx's config the way this game needs it (only for a BepInEx that HoofMods installed). */
void hm_bepinex_config(Lib *L);

/* shared helpers */
void hm_mkdirs(const wchar_t *path);
void hm_content_dir(Lib *L, ModKind k, int on, wchar_t *out);
int hm_recycle(const wchar_t *const *paths, int n);
