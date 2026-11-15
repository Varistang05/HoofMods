#pragma once
#include <windows.h>

#define WM_HM_UPDATE (WM_APP + 7) /* sent to the window when theres a newer version */
#define WM_HM_WHATSNEW (WM_APP + 8) /* first start after an update: show whats new */

void update_start(HWND notify, const wchar_t *data_dir);
const wchar_t *update_latest(void); /* the newer version, or NULL */
int update_has_page(void);
int update_should_tell(void); /* 1 = show the "new version" popup now (only once per version) */
void update_open_page(void);
/* Update now: downloads the newest zip, checks its SHA256 against the version file, swaps HoofMods.exe + loader\hid.dll.
   1 = done (restart HoofMods), 0 = nothing changed and err says why in plain words */
int update_install(wchar_t *err, int errn);
