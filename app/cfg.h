#pragma once
#include <windows.h>

#define CFG_MAX_OPTS 24

typedef struct {
    wchar_t section[64];
    wchar_t key[96];
    wchar_t value[256];
    wchar_t desc[256];
    wchar_t type[32];
    wchar_t def[96];
    wchar_t opts[CFG_MAX_OPTS][48];
    int nopts;
    int has_range;
    double lo, hi;
    int line;
} CfgEntry;

typedef struct {
    wchar_t path[MAX_PATH];
    wchar_t title[128];
    char **lines; /* UTF8, no line endings */
    int nlines;
    int crlf;
    CfgEntry *e;
    int n;
} Cfg;

/* finds BepInEx\config\*.cfg files, title comes from the "created by plugin" line, returns how many */
int cfg_list(const wchar_t *game, wchar_t (*paths)[MAX_PATH], wchar_t (*titles)[128], int max);
int cfg_load(Cfg *c, const wchar_t *path);
int cfg_set(Cfg *c, int i, const wchar_t *value); /* saves the file, 0 if it didnt work */
void cfg_free(Cfg *c);
int cfg_is_bool(const CfgEntry *e);
int cfg_is_int(const CfgEntry *e);
