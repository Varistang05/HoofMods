#pragma once
#include <windows.h>

#define WM_HM_UPDATE (WM_APP + 7) /* posted to the window when a newer version was found */

void update_start(HWND notify, const wchar_t *data_dir);
const wchar_t *update_latest(void); /* newer version text, or NULL */
int update_has_page(void);
void update_open_page(void);
