/* Controller input for the app (XInput: Xbox pads, Steam Deck, and PlayStation pads through Steam Input). */
#include <windows.h>
#include "pad.h"

typedef struct {
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
} XPad;
typedef struct {
    DWORD packet;
    XPad pad;
} XState;

static DWORD(WINAPI *g_get)(DWORD, XState *);
static int g_tried;

static void load(void) {
    g_tried = 1;
    const char *names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (int i = 0; i < 3 && !g_get; i++) {
        HMODULE h = LoadLibraryA(names[i]);
        if (h) g_get = (void *)GetProcAddress(h, "XInputGetState");
    }
}

int pad_read(PadState *out) {
    if (!g_tried) load();
    memset(out, 0, sizeof *out);
    if (!g_get) return 0;
    for (DWORD i = 0; i < 4; i++) {
        XState st;
        if (g_get(i, &st) != ERROR_SUCCESS) continue;
        out->buttons = st.pad.buttons;
        out->lx = st.pad.lx / 32767.f;
        out->ly = st.pad.ly / 32767.f;
        out->rx = st.pad.rx / 32767.f;
        out->ry = st.pad.ry / 32767.f;
        out->lt = st.pad.lt / 255.f;
        out->rt = st.pad.rt / 255.f;
        return 1;
    }
    return 0;
}
