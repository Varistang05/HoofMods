#pragma once
#include "mods.h"

/* packs logs + a summary into one zip for the mods author. out_dir NULL = the Desktop.
   gives back 1 + the zips path, or 0 + why it didnt work (simple words) */
int report_make(Lib *L, const wchar_t *out_dir, wchar_t *path, int pathn, wchar_t *err, int errn);
