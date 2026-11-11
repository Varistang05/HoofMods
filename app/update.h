#pragma once
#include <windows.h>

#define WM_HM_UPDATE (WM_APP + 7) /* sent to the window when theres a newer version */

void update_start(HWND notify, const wchar_t *data_dir);
const wchar_t *update_latest(void); /* the newer version, or NULL */
int update_has_page(void);
void update_open_page(void);
