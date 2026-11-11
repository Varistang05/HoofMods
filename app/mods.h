#pragma once
#include <windows.h>

typedef enum { MK_DLL, MK_BEPINEX, MK_TACK, MK_CLOTHES, MK_LANGUAGE, MK_COUNT } ModKind;

typedef struct {
    ModKind kind;
    wchar_t id[MAX_PATH];  /* folder name (code mods), file/folder name (BepInEx) or file name w/o extension (tack etc) */
    wchar_t name[128];
    wchar_t info[256];     /* author / version / how the last start went */
    int enabled;
    int pkg;               /* which pack its in, or minus 1 if none */
    int hidden;            /* shown thru its pack row instead */
    int count;             /* pack rows: how many items */
    int pic_state;         /* 0 = not checked yet, 1 = has a pic, 2 = no pic */
    wchar_t pic[MAX_PATH];
} Mod;

/* a download installed w/ HoofMods, so a whole collection shows as one row */
typedef struct {
    wchar_t id[64];
    wchar_t name[128];
    ModKind kind;
    int nitems;
    wchar_t (*items)[MAX_PATH];
} Pkg;

typedef struct {
    wchar_t game[MAX_PATH];     /* game folder, empty if we couldnt find it */
    wchar_t content[MAX_PATH];  /* ...\LocalLow\ThogliStudios\Horses of Hoofprint Bay\Saves~\Mods */
    wchar_t data[MAX_PATH];     /* %LOCALAPPDATA%\HoofMods */
    wchar_t appid[16];
    int loader;                 /* 0 none, 1 ours, 2 someone elses hid.dll,
                                   3 an older version of ours (update ready) */
    int bepinex;
    int bep_on;                 /* BepInEx starts w/ the game (enabled in doorstop_config.ini) */
    Mod *mods;
    int n, cap;
    Pkg *pkgs;
    int npkgs;
    wchar_t twin[MAX_PATH];     /* set when an install finds the same mod in the game folder too (that rows id) */
} Lib;

/* DLL names other mods use to load straight from the game folder. a code mod row w/ an id starting
   w/ @ is one of those (like @version.dll = StickHorse installed the normal way) */
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
int lib_preview_path(Lib *L, int i, wchar_t *out); /* pic for a tack/clothes row, 0 if there isnt one */
/* tack/clothes only: how many saves use an item from row i, item and save get the first one found
   (like Blue Ascot Jumping Pad, Varis). only reads saves, never changes them */
int lib_saves_using(Lib *L, int i, wchar_t *item, int itemn, wchar_t *save, int saven);
/* copies the saves to %LOCALAPPDATA%\HoofMods\Save backups\<date time>, keeps the newest 10 */
int lib_backup_saves(Lib *L, wchar_t *where, int wheren);
int lib_install_loader(Lib *L, wchar_t *err, int errn);
int lib_game_running(void);
void lib_open_folder(Lib *L, ModKind k);
void lib_start_game(Lib *L);
int lib_start_without_mods(Lib *L); /* next start loads no code mods (the loader deletes the flag) */
int lib_set_bepinex(Lib *L, int on, wchar_t *err, int errn); /* turns BepInEx itself on/off (its mods stay where they are) */

/* install.c: path can be a .zip, a folder or one mod file. ask() gets called if we cant tell what it is
   (returns MK_TACK, MK_CLOTHES or minus 1 to cancel). returns 1 if it worked, msg always gets a sentence for the player */
typedef int (*AskKind)(void *ctx, const wchar_t *what);
int lib_install(Lib *L, const wchar_t *path, AskKind ask, void *ctx, wchar_t *msg, int msgn);

/* keep out list for the loader (see loader.c): add = this file has to stay gone even if Steam Cloud
   brings it back, add = 0 takes it off the list again */
void hm_keepout(Lib *L, const wchar_t *path, int add);
/* sets BepInEx's config the way this game needs (only if HoofMods installed that BepInEx) */
void hm_bepinex_config(Lib *L);

/* used in a few places */
void hm_mkdirs(const wchar_t *path);
void hm_content_dir(Lib *L, ModKind k, int on, wchar_t *out);
int hm_recycle(const wchar_t *const *paths, int n);
