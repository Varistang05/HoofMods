/* checks if theres a newer HoofMods, once a day, only if release.h has a version file link */
#include <windows.h>
#include "release.h"
#if HM_CHECK_UPDATES
#include <winhttp.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <wchar.h>
#include "release.h"
#include "update.h"

static HWND g_notify;
static wchar_t g_ini[MAX_PATH];
static wchar_t g_latest[32];

/* 1 if a is newer than b (1.0.10 beats 1.0.9) */
static int newer(const wchar_t *a, const wchar_t *b) {
    for (;;) {
        long x = wcstol(a, (wchar_t **)&a, 10), y = wcstol(b, (wchar_t **)&b, 10);
        if (x != y) return x > y;
        if (*a != L'.' && *b != L'.') return 0;
        if (*a == L'.') a++;
        if (*b == L'.') b++;
    }
}

#if HM_CHECK_UPDATES /* internet stuff only gets built in when update checks are on */
static int fetch(const wchar_t *url, char *out, int outn) {
    URL_COMPONENTS u = {sizeof u};
    wchar_t host[256], path[1024];
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url, 0, 0, &u)) return 0;
    int got = 0;
    HINTERNET s = WinHttpOpen(L"HoofMods", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &len, NULL);
        DWORD n = 0;
        if (status == 200 && WinHttpReadData(r, out, outn - 1, &n)) got = (int)n;
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    out[got] = 0;
    return got > 0;
}

static DWORD WINAPI check_thread(void *unused) {
    (void)unused;
    char buf[64];
    if (!fetch(HM_VERSION_FILE_URL, buf, sizeof buf)) return 0;
    wchar_t v[32] = L"";
    int k = 0;
    for (char *c = buf; *c && k < 31 && (isdigit((unsigned char)*c) || *c == '.'); c++) v[k++] = (wchar_t)*c;
    v[k] = 0;
    if (!k) return 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t day[16];
    swprintf(day, 16, L"%04d%02d%02d", t.wYear, t.wMonth, t.wDay);
    WritePrivateProfileStringW(L"Update", L"LastCheck", day, g_ini);
    WritePrivateProfileStringW(L"Update", L"Latest", v, g_ini);
    if (newer(v, HM_VERSION_TEXT)) {
        wcscpy(g_latest, v);
        PostMessageW(g_notify, WM_HM_UPDATE, 0, 0);
    }
    return 0;
}
#endif

void update_start(HWND notify, const wchar_t *data_dir) {
    g_notify = notify;
    swprintf(g_ini, MAX_PATH, L"%ls\\app.ini", data_dir);
#if !HM_CHECK_UPDATES
    return; /* no internet code in this build */
#endif
    if (!HM_VERSION_FILE_URL[0]) return;
    /* if an earlier check already found a newer one, still show it */
    wchar_t last[32];
    GetPrivateProfileStringW(L"Update", L"Latest", L"", last, 32, g_ini);
    if (last[0] && newer(last, HM_VERSION_TEXT)) wcscpy(g_latest, last);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t day[16], checked[16];
    swprintf(day, 16, L"%04d%02d%02d", t.wYear, t.wMonth, t.wDay);
    GetPrivateProfileStringW(L"Update", L"LastCheck", L"", checked, 16, g_ini);
    if (wcscmp(day, checked)) {
#if HM_CHECK_UPDATES
        HANDLE th = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
        if (th) CloseHandle(th);
#endif
    }
}

const wchar_t *update_latest(void) { return g_latest[0] ? g_latest : NULL; }

int update_has_page(void) { return HM_NEXUS_URL[0] != 0; }

void update_open_page(void) {
    if (HM_NEXUS_URL[0]) ShellExecuteW(NULL, L"open", HM_NEXUS_URL, NULL, NULL, SW_SHOWNORMAL);
}
