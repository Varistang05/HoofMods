/* HoofMods: Mod Manager for Horses of Hoofprint Bay, the desktop app */
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include "mods.h"
#include "preview.h"
#include "cfg.h"
#include "pad.h"
#include "update.h"

#define APP_TITLE L"HoofMods: Mod Manager for Horses of Hoofprint Bay"

/* ==== looks ==== */

typedef struct { const wchar_t *name; COLORREF paper, desk, ink, faded, red; } Theme;
static const Theme THEMES[] = {
    {L"Parchment", RGB(247, 238, 210), RGB(233, 220, 184), RGB(62, 40, 22), RGB(128, 102, 74), RGB(156, 44, 30)},
    {L"Night Stable", RGB(54, 45, 38), RGB(36, 30, 26), RGB(240, 226, 196), RGB(178, 158, 130), RGB(236, 118, 96)},
    {L"Meadow", RGB(240, 246, 226), RGB(214, 228, 190), RGB(38, 62, 30), RGB(98, 122, 82), RGB(164, 52, 30)},
    {L"Rose Ribbon", RGB(252, 238, 236), RGB(240, 214, 214), RGB(96, 36, 52), RGB(152, 98, 110), RGB(176, 30, 40)},
    {L"Ocean Bay", RGB(238, 245, 250), RGB(204, 222, 234), RGB(24, 52, 78), RGB(90, 118, 142), RGB(172, 52, 40)},
};
#define N_THEMES (int)(sizeof THEMES / sizeof THEMES[0])
#define THEME_CUSTOM N_THEMES
static const float ZOOMS[] = {1.f, 1.15f, 1.3f};
static const wchar_t *ZOOM_NAMES[] = {L"Normal", L"Large", L"Extra large"};

static COLORREF C_PAPER, C_DESK, C_INK, C_FADED, C_RED;
static int g_theme, g_zoom, g_texture = 1;
static COLORREF g_custom_ink = RGB(62, 40, 22), g_custom_paper = RGB(247, 238, 210), g_custom_red = RGB(156, 44, 30);

static COLORREF mix(COLORREF a, COLORREF b, float t) {
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static void apply_theme(void) {
    if (g_theme >= 0 && g_theme < N_THEMES) {
        const Theme *t = &THEMES[g_theme];
        C_PAPER = t->paper, C_DESK = t->desk, C_INK = t->ink, C_FADED = t->faded, C_RED = t->red;
    } else { /* custom: the desk and faded ink get worked out from the ink and paper you picked */
        g_theme = THEME_CUSTOM;
        C_PAPER = g_custom_paper;
        C_INK = g_custom_ink;
        C_RED = g_custom_red;
        C_DESK = mix(g_custom_paper, g_custom_ink, 0.08f);
        C_FADED = mix(g_custom_ink, g_custom_paper, 0.42f);
    }
}

enum { H_TOGGLE, H_ROW, H_REMOVE, H_FOLDER, H_INSTALL, H_FINDGAME, H_REFRESH, H_START, H_ADD,
       H_BACK, H_CFGLIST, H_CFGOPEN, H_CFG_TOGGLE, H_CFG_PREV, H_CFG_NEXT, H_CFG_EDIT,
       H_LOOK, H_THEME, H_ZOOM, H_TEXTURE, H_PICK, H_POP, H_UPDATE, H_GETBEP, H_NOMODS, H_BEPSW,
       H_WIN_MIN, H_WIN_MAX, H_WIN_CLOSE };
typedef struct { RECT r, full; int type, arg, in_list; } Hit; /* r = the part you can see, full = the whole thing (might be scrolled away) */

/* pages: 0 = mod list, 1 = BepInEx settings files, 2 = one settings file, 3 = appearance */
static int g_page;
#define MAX_CFGS 128
static wchar_t g_cfg_paths[MAX_CFGS][MAX_PATH], g_cfg_titles[MAX_CFGS][128];
static int g_ncfg;
static Cfg g_cfg;
static HWND g_edit;
static int g_edit_idx = -1;
static WNDPROC g_edit_proc;
static void load_cfg_list(void);

/* parchment popup drawn over the window (see popup()) */
static int g_pop_active, g_pop_result, g_pop_nb, g_pop_danger;
static int g_pop_start = 1; /* which button the controller starts on, set to 2 right b4 a careful question */
static wchar_t g_pop_title[128], g_pop_body[800], g_pop_btn[3][48];

static Lib g_lib;
static HWND g_wnd;
static float s = 1, g_dpi_s = 1; /* s = monitor scale x text size */
static HFONT fTitle, fSub, fHead, fBody, fSmall, fBtn;
static Hit g_hits[512];
static int g_nhits, g_sel = -1;
static int g_scroll, g_content_h, g_list_h;
static wchar_t g_msg[256];
static int g_msg_bad;
static int g_mx = -1, g_my = -1;

/* controller: a highlight jumps between stuff you can press */
static RECT g_list_rect;
static int g_pad_mode;                   /* 1 after using the controller, 0 after moving the mouse */
static int g_focus_type = -1, g_focus_arg; /* whats highlighted, kept between redraws */
static void paint_focus(void);

/* ==== drawing ==== */

static DWORD *g_px;
static int g_w, g_h;
static RECT g_clip;

static float sd_rr(float px, float py, float hw, float hh, float r) {
    float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void blend(DWORD *p, COLORREF c, float a) {
    if (a <= 0) return;
    int r = (*p >> 16) & 255, g = (*p >> 8) & 255, b = *p & 255;
    r += (int)((GetRValue(c) - r) * a);
    g += (int)((GetGValue(c) - g) * a);
    b += (int)((GetBValue(c) - b) * a);
    *p = (r << 16) | (g << 8) | b;
}

static unsigned hash(unsigned x, unsigned y) {
    unsigned h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void paint_desk(void) {
    for (int y = 0; y < g_h; y++)
        for (int x = 0; x < g_w; x++) {
            int n = g_texture ? (int)(hash(x, y) & 7) - 3 + (int)(hash(x / 7, y / 3) & 3) : 0;
            int r = GetRValue(C_DESK) + n, g = GetGValue(C_DESK) + n, b = GetBValue(C_DESK) + n;
            g_px[y * g_w + x] = (r << 16) | (g << 8) | b;
        }
}

/* inked paper shape w/ a slightly wobbly edge and a hard shadow, like the games UI */
static void shape(float x, float y, float w, float h, float rad, COLORREF fill, float fill_a, COLORREF ink, float inkw, float shadow) {
    float cx = x + w / 2, cy = y + h / 2, hw = w / 2, hh = h / 2;
    rad = fminf(rad, fminf(hw, hh));
    int x0 = (int)(x - 3), y0 = (int)(y - 3), x1 = (int)(x + w + shadow + 3), y1 = (int)(y + h + shadow + 3);
    if (x0 < g_clip.left) x0 = g_clip.left;
    if (y0 < g_clip.top) y0 = g_clip.top;
    if (x1 > g_clip.right) x1 = g_clip.right;
    if (y1 > g_clip.bottom) y1 = g_clip.bottom;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f - cx, fy = py + 0.5f - cy;
            float wob = (sinf(fx * 0.05f + fy * 0.013f) + sinf(fy * 0.07f - fx * 0.021f)) * 0.3f * s;
            float d = sd_rr(fx, fy, hw, hh, rad) + wob;
            DWORD *p = &g_px[py * g_w + px];
            if (shadow > 0) {
                float ds = sd_rr(fx - shadow * 0.5f, fy - shadow, hw, hh, rad) + wob;
                if (d > -1) blend(p, C_INK, 0.30f * clamp01(0.5f - ds) * clamp01(0.5f + d));
            }
            blend(p, fill, fill_a * clamp01(0.5f - d));
            if (inkw > 0) blend(p, ink, clamp01(0.5f - (fabsf(d + inkw * 0.5f) - inkw * 0.5f)));
        }
}

static void text(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, const wchar_t *t, UINT fmt) {
    RECT r = {x, y, x + w, y + h};
    if (r.top < g_clip.top || r.bottom > g_clip.bottom) {
        HRGN rg = CreateRectRgn(g_clip.left, g_clip.top, g_clip.right, g_clip.bottom);
        SelectClipRgn(dc, rg);
        DeleteObject(rg);
    }
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &r, fmt | DT_NOPREFIX | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectClipRgn(dc, NULL);
}

/* small framed pic of a tack/clothes item, faded when its turned off */
static void draw_thumb(int x, int y, int box, const wchar_t *path, int on) {
    shape(x, y, box, box, 7 * s, C_PAPER, 1, C_INK, 1.6f * s, 0);
    int inner = box - (int)(8 * s);
    const Thumb *t = thumb_get(path, inner);
    if (!t) return;
    int ox = x + (box - t->w) / 2, oy = y + (box - t->h) / 2;
    float fade = on ? 1.f : 0.4f;
    for (int j = 0; j < t->h; j++) {
        int py = oy + j;
        if (py < g_clip.top || py >= g_clip.bottom) continue;
        for (int i = 0; i < t->w; i++) {
            int px = ox + i;
            if (px < g_clip.left || px >= g_clip.right) continue;
            DWORD s32 = t->px[j * t->w + i];
            float a = ((s32 >> 24) & 255) / 255.f * fade;
            if (a <= 0) continue;
            DWORD *d = &g_px[py * g_w + px];
            /* premultiplied, so dst = src*fade + dst*(1 minus a) */
            int r = (int)(((s32 >> 16) & 255) * fade + ((*d >> 16) & 255) * (1 - a));
            int g = (int)(((s32 >> 8) & 255) * fade + ((*d >> 8) & 255) * (1 - a));
            int b = (int)((s32 & 255) * fade + (*d & 255) * (1 - a));
            *d = (DWORD)(r > 255 ? 255 : r) << 16 | (DWORD)(g > 255 ? 255 : g) << 8 | (DWORD)(b > 255 ? 255 : b);
        }
    }
}

static int hit_add(int x, int y, int w, int h, int type, int arg) {
    if (g_nhits >= 512) return -1;
    Hit *t = &g_hits[g_nhits];
    t->r = (RECT){x, y, x + w, y + h};
    t->full = t->r;
    t->in_list = g_clip.top == g_list_rect.top && g_clip.bottom == g_list_rect.bottom;
    IntersectRect(&t->r, &t->r, &g_clip);
    t->type = type;
    t->arg = arg;
    return g_nhits++;
}

static int hovered(int x, int y, int w, int h) { return g_mx >= x && g_mx < x + w && g_my >= y && g_my < y + h; }

static void button(HDC dc, int x, int y, int w, int h, const wchar_t *label, int type, int arg, int strong) {
    int hov = hovered(x, y, w, h) && g_my >= g_clip.top && g_my < g_clip.bottom;
    int lift = hov ? (int)(-1 * s) : 0;
    shape(x, y + lift, w, h, 9 * s, strong ? C_INK : C_PAPER, 1, C_INK, 2.2f * s, hov ? 4 * s : 3 * s);
    text(dc, fBtn, strong ? C_PAPER : C_INK, x, y + lift, w, h, label, DT_CENTER | DT_VCENTER);
    hit_add(x, y, w, h, type, arg);
}

static void toggle(int x, int y, int on) {
    float w = 54 * s, h = 28 * s;
    shape(x, y, w, h, h / 2, on ? C_INK : C_PAPER, 1, C_INK, 2.2f * s, 0);
    float k = h - 8 * s;
    float kx = on ? x + w - k - 4 * s : x + 4 * s;
    shape(kx, y + 4 * s, k, k, k / 2, on ? C_PAPER : C_INK, 1, C_INK, on ? 0 : 0, 0);
}

/* ==== layout ==== */

static void make_fonts(void) {
    HFONT *all[] = {&fTitle, &fSub, &fHead, &fBody, &fSmall, &fBtn};
    for (int i = 0; i < 6; i++)
        if (*all[i]) DeleteObject(*all[i]);
#define F(px, wt, face) CreateFontW(-(int)((px) * s), 0, 0, 0, wt, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face)
    fTitle = F(36, FW_HEAVY, L"Segoe UI Black");
    fSub = F(17, FW_SEMIBOLD, L"Segoe UI");
    fHead = F(21, FW_BOLD, L"Segoe UI");
    fBody = F(18, FW_SEMIBOLD, L"Segoe UI");
    fSmall = F(14, FW_NORMAL, L"Segoe UI");
    fBtn = F(16, FW_BOLD, L"Segoe UI");
#undef F
}

static void set_scale(void) {
    s = g_dpi_s * ZOOMS[g_zoom];
    make_fonts();
}

/* looks get saved in %LOCALAPPDATA%\HoofMods\app.ini [Look] */
static void look_ini(wchar_t *out) { swprintf(out, MAX_PATH, L"%ls\\app.ini", g_lib.data); }

static void load_look(void) {
    wchar_t ini[MAX_PATH];
    look_ini(ini);
    g_theme = GetPrivateProfileIntW(L"Look", L"Theme", 0, ini);
    g_zoom = GetPrivateProfileIntW(L"Look", L"TextSize", 0, ini);
    g_texture = GetPrivateProfileIntW(L"Look", L"Texture", 1, ini);
    g_custom_ink = GetPrivateProfileIntW(L"Look", L"Ink", g_custom_ink, ini);
    g_custom_paper = GetPrivateProfileIntW(L"Look", L"Paper", g_custom_paper, ini);
    g_custom_red = GetPrivateProfileIntW(L"Look", L"Highlight", g_custom_red, ini);
    if (g_zoom < 0 || g_zoom > 2) g_zoom = 0;
    if (g_theme < 0 || g_theme > THEME_CUSTOM) g_theme = 0;
    apply_theme();
}

static void save_look(void) {
    wchar_t ini[MAX_PATH], v[16];
    look_ini(ini);
    swprintf(v, 16, L"%d", g_theme);
    WritePrivateProfileStringW(L"Look", L"Theme", v, ini);
    swprintf(v, 16, L"%d", g_zoom);
    WritePrivateProfileStringW(L"Look", L"TextSize", v, ini);
    swprintf(v, 16, L"%d", g_texture);
    WritePrivateProfileStringW(L"Look", L"Texture", v, ini);
    swprintf(v, 16, L"%lu", (unsigned long)g_custom_ink);
    WritePrivateProfileStringW(L"Look", L"Ink", v, ini);
    swprintf(v, 16, L"%lu", (unsigned long)g_custom_paper);
    WritePrivateProfileStringW(L"Look", L"Paper", v, ini);
    swprintf(v, 16, L"%lu", (unsigned long)g_custom_red);
    WritePrivateProfileStringW(L"Look", L"Highlight", v, ini);
}

static int kind_count(ModKind k) {
    int c = 0;
    for (int i = 0; i < g_lib.n; i++) c += g_lib.mods[i].kind == k && !g_lib.mods[i].hidden;
    return c;
}

static int kind_shown(ModKind k) { return k != MK_BEPINEX || g_lib.bepinex || kind_count(k); }

static const wchar_t *EMPTY_HINT[MK_COUNT] = {
    L"No code mods yet. Drag a mod's .zip onto this window to install it.",
    L"No BepInEx plugins installed.",
    L"No custom tack yet. Drag a tack download (.zip) onto this window to install it.",
    L"No custom clothes yet.",
    L"No custom language files yet.",
};

static void section_head(HDC dc, int px, int *y, int pw, const wchar_t *title) {
    int headh = (int)(42 * s);
    text(dc, fHead, C_INK, px, *y, pw, headh, title, DT_LEFT | DT_VCENTER);
    shape(px, *y + headh - 3 * s, pw, 2 * s, 1 * s, C_INK, 0.55f, 0, 0, 0);
    *y += headh;
}

static void swatch(int x, int y, int w, int h, COLORREF c, COLORREF ink) { shape(x, y, w, h, 5 * s, c, 1, ink, 1.4f * s, 0); }

static int paint_look(HDC dc, int y, int px, int pw) {
    int rowh = (int)(58 * s);
    section_head(dc, px, &y, pw, L"Theme");
    y += (int)(10 * s);
    int cw = (int)(156 * s), ch = (int)(92 * s), gap = (int)(14 * s);
    int per = (pw + gap) / (cw + gap);
    if (per < 1) per = 1;
    for (int i = 0; i <= N_THEMES; i++) {
        COLORREF paper, desk, ink, faded, red;
        const wchar_t *name;
        if (i < N_THEMES) paper = THEMES[i].paper, desk = THEMES[i].desk, ink = THEMES[i].ink, faded = THEMES[i].faded, red = THEMES[i].red, name = THEMES[i].name;
        else paper = g_custom_paper, ink = g_custom_ink, red = g_custom_red, desk = mix(paper, ink, 0.08f), faded = mix(ink, paper, 0.42f), name = L"Custom";
        int x = px + (i % per) * (cw + gap), yy = y + (i / per) * (ch + gap);
        int sel = i == g_theme, hov = hovered(x, yy, cw, ch) && g_my >= g_clip.top && g_my < g_clip.bottom;
        shape(x, yy, cw, ch, 10 * s, desk, 1, sel ? C_INK : (hov ? C_FADED : 0), sel ? 3.f * s : 1.5f * s, sel || hov ? 3 * s : 0);
        shape(x + 10 * s, yy + 10 * s, cw - 20 * s, ch - 46 * s, 7 * s, paper, 1, ink, 1.8f * s, 0);
        swatch(x + (int)(18 * s), yy + (int)(20 * s), (int)(18 * s), (int)(18 * s), ink, ink);
        swatch(x + (int)(42 * s), yy + (int)(20 * s), (int)(18 * s), (int)(18 * s), faded, ink);
        swatch(x + (int)(66 * s), yy + (int)(20 * s), (int)(18 * s), (int)(18 * s), red, ink);
        text(dc, fBtn, ink, x + (int)(10 * s), yy + ch - (int)(34 * s), cw - (int)(20 * s), (int)(28 * s), name, DT_LEFT | DT_VCENTER);
        if (sel) text(dc, fBtn, ink, x + (int)(10 * s), yy + ch - (int)(34 * s), cw - (int)(20 * s), (int)(28 * s), L"\x2713", DT_RIGHT | DT_VCENTER);
        hit_add(x, yy, cw, ch, H_THEME, i);
    }
    y += ((N_THEMES + per) / per) * (ch + gap) + (int)(8 * s);

    section_head(dc, px, &y, pw, L"Custom colours");
    static const wchar_t *labels[3] = {L"Ink", L"Paper", L"Highlight"};
    static const wchar_t *hints[3] = {L"Text, borders and switches", L"Panels and buttons", L"Warnings and the Remove link"};
    COLORREF cols[3] = {g_custom_ink, g_custom_paper, g_custom_red};
    for (int i = 0; i < 3; i++) {
        text(dc, fBody, C_INK, px + (int)(6 * s), y + (int)(6 * s), pw - (int)(200 * s), (int)(24 * s), labels[i], DT_LEFT | DT_VCENTER);
        text(dc, fSmall, C_FADED, px + (int)(6 * s), y + (int)(31 * s), pw - (int)(200 * s), (int)(20 * s), hints[i], DT_LEFT | DT_VCENTER);
        int bw = (int)(150 * s), bh = (int)(32 * s), bx = px + pw - bw, by = y + (rowh - bh) / 2;
        int hov = hovered(bx, by, bw, bh);
        shape(bx, by, bw, bh, 8 * s, C_PAPER, 1, hov ? C_INK : C_FADED, 1.8f * s, 0);
        swatch(bx + (int)(8 * s), by + (int)(6 * s), (int)(36 * s), bh - (int)(12 * s), cols[i], C_INK);
        text(dc, fSmall, C_INK, bx + (int)(50 * s), by, bw - (int)(56 * s), bh, L"Change\x2026", DT_LEFT | DT_VCENTER);
        hit_add(bx, by, bw, bh, H_PICK, i);
        y += rowh;
    }
    y += (int)(8 * s);

    section_head(dc, px, &y, pw, L"Text and paper");
    {
        text(dc, fBody, C_INK, px + (int)(6 * s), y + (int)(6 * s), pw - (int)(260 * s), (int)(24 * s), L"Text size", DT_LEFT | DT_VCENTER);
        text(dc, fSmall, C_FADED, px + (int)(6 * s), y + (int)(31 * s), pw - (int)(260 * s), (int)(20 * s), L"Makes everything bigger, handy on a Steam Deck or a TV", DT_LEFT | DT_VCENTER);
        int cw2 = (int)(220 * s), cx = px + pw - cw2, chh = (int)(32 * s), cy = y + (rowh - chh) / 2, aw = (int)(34 * s);
        shape(cx, cy, cw2, chh, 8 * s, C_PAPER, 1, C_INK, 1.8f * s, 0);
        text(dc, fHead, hovered(cx, cy, aw, chh) ? C_INK : C_FADED, cx, cy, aw, chh, L"\x2039", DT_CENTER | DT_VCENTER);
        text(dc, fHead, hovered(cx + cw2 - aw, cy, aw, chh) ? C_INK : C_FADED, cx + cw2 - aw, cy, aw, chh, L"\x203A", DT_CENTER | DT_VCENTER);
        text(dc, fBody, C_INK, cx + aw, cy, cw2 - 2 * aw, chh, ZOOM_NAMES[g_zoom], DT_CENTER | DT_VCENTER);
        hit_add(cx, cy, aw, chh, H_ZOOM, -1);
        hit_add(cx + cw2 - aw, cy, aw, chh, H_ZOOM, 1);
        y += rowh;
    }
    {
        text(dc, fBody, C_INK, px + (int)(6 * s), y + (int)(6 * s), pw - (int)(200 * s), (int)(24 * s), L"Paper texture", DT_LEFT | DT_VCENTER);
        text(dc, fSmall, C_FADED, px + (int)(6 * s), y + (int)(31 * s), pw - (int)(200 * s), (int)(20 * s), L"A light grain on the background", DT_LEFT | DT_VCENTER);
        int tx = px + pw - (int)(60 * s), ty = y + (rowh - (int)(28 * s)) / 2;
        text(dc, fSmall, C_INK, tx - (int)(60 * s), ty, (int)(52 * s), (int)(28 * s), g_texture ? L"On" : L"Off", DT_RIGHT | DT_VCENTER);
        toggle(tx, ty, g_texture);
        hit_add(tx - (int)(70 * s), y, (int)(130 * s), rowh, H_TEXTURE, 0);
        y += rowh;
    }
    return y;
}

/* the settings and appearance pages, gives back the y under the last row */
static int paint_settings(HDC dc, int y, int px, int pw) {
    int rowh = (int)(58 * s), headh = (int)(42 * s);
    if (g_page == 3) return paint_look(dc, y, px, pw);
    if (g_page == 1) {
        if (!g_ncfg) {
            text(dc, fSmall, C_FADED, px + (int)(4 * s), y, pw, (int)(34 * s), L"None of your BepInEx mods have settings yet. Start the game once so they create them.",
                 DT_LEFT | DT_VCENTER);
            return y + (int)(40 * s);
        }
        for (int i = 0; i < g_ncfg; i++) {
            int hov = hovered(px, y, pw, rowh) && g_my >= g_clip.top && g_my < g_clip.bottom;
            if (hov) shape(px - 6 * s, y + 3 * s, pw + 12 * s, rowh - 6 * s, 8 * s, C_INK, 0.05f, 0, 0, 0);
            text(dc, fBody, C_INK, px + (int)(6 * s), y + (int)(7 * s), pw - (int)(120 * s), (int)(24 * s), g_cfg_titles[i], DT_LEFT | DT_VCENTER);
            const wchar_t *fn = wcsrchr(g_cfg_paths[i], L'\\');
            text(dc, fSmall, C_FADED, px + (int)(6 * s), y + (int)(31 * s), pw - (int)(120 * s), (int)(20 * s), fn + 1, DT_LEFT | DT_VCENTER);
            text(dc, fSmall, hov ? C_INK : C_FADED, px + pw - (int)(110 * s), y, (int)(104 * s), rowh, L"Change \x2192", DT_RIGHT | DT_VCENTER);
            hit_add(px, y, pw, rowh, H_CFGOPEN, i);
            y += rowh;
        }
        return y;
    }
    const wchar_t *section = NULL;
    for (int i = 0; i < g_cfg.n; i++) {
        CfgEntry *e = &g_cfg.e[i];
        if (!section || wcscmp(section, e->section)) {
            section = e->section;
            if (i) y += (int)(8 * s);
            text(dc, fHead, C_INK, px, y, pw, headh, section[0] ? section : L"Settings", DT_LEFT | DT_VCENTER);
            shape(px, y + headh - 3 * s, pw, 2 * s, 1 * s, C_INK, 0.55f, 0, 0, 0);
            y += headh;
        }
        int cw = (int)(220 * s), cx = px + pw - cw, ch = (int)(32 * s), cy = y + (rowh - ch) / 2;
        int tw = cx - px - (int)(20 * s);
        text(dc, fBody, C_INK, px + (int)(6 * s), y + (int)(6 * s), tw, (int)(24 * s), e->key, DT_LEFT | DT_VCENTER);
        wchar_t d[300];
        if (e->def[0] && _wcsicmp(e->def, e->value)) swprintf(d, 300, L"%ls (default: %ls)", e->desc, e->def);
        else wcscpy(d, e->desc);
        text(dc, fSmall, C_FADED, px + (int)(6 * s), y + (int)(31 * s), tw, (int)(20 * s), d, DT_LEFT | DT_VCENTER);
        if (cfg_is_bool(e)) {
            int on = !_wcsicmp(e->value, L"true");
            int tx = px + pw - (int)(60 * s), ty = y + (rowh - (int)(28 * s)) / 2;
            text(dc, fSmall, C_INK, tx - (int)(60 * s), ty, (int)(52 * s), (int)(28 * s), on ? L"On" : L"Off", DT_RIGHT | DT_VCENTER);
            toggle(tx, ty, on);
            hit_add(tx - (int)(70 * s), y, (int)(130 * s), rowh, H_CFG_TOGGLE, i);
        } else if (e->nopts || e->has_range) {
            int aw = (int)(34 * s);
            shape(cx, cy, cw, ch, 8 * s, C_PAPER, 1, C_INK, 1.8f * s, 0);
            int lh = hovered(cx, cy, aw, ch), rh = hovered(cx + cw - aw, cy, aw, ch);
            text(dc, fHead, lh ? C_INK : C_FADED, cx, cy, aw, ch, e->nopts ? L"\x2039" : L"\x2212", DT_CENTER | DT_VCENTER);
            text(dc, fHead, rh ? C_INK : C_FADED, cx + cw - aw, cy, aw, ch, e->nopts ? L"\x203A" : L"+", DT_CENTER | DT_VCENTER);
            text(dc, fBody, C_INK, cx + aw, cy, cw - 2 * aw, ch, e->value, DT_CENTER | DT_VCENTER);
            hit_add(cx, cy, aw, ch, H_CFG_PREV, i);
            hit_add(cx + cw - aw, cy, aw, ch, H_CFG_NEXT, i);
            if (!e->nopts) hit_add(cx + aw, cy, cw - 2 * aw, ch, H_CFG_EDIT, i);
        } else {
            int hov = hovered(cx, cy, cw, ch);
            shape(cx, cy, cw, ch, 8 * s, C_PAPER, 1, hov ? C_INK : C_FADED, 1.8f * s, 0);
            if (i != g_edit_idx)
                text(dc, fBody, C_INK, cx + (int)(10 * s), cy, cw - (int)(20 * s), ch, e->value[0] ? e->value : L"(empty)", DT_LEFT | DT_VCENTER);
            hit_add(cx, cy, cw, ch, H_CFG_EDIT, i);
        }
        y += rowh;
    }
    return y;
}

/* ==== our own title bar ==== */

#define HEADER_H ((int)(90 * s)) /* the header is the title bar too */
static int hit_at(int x, int y);

static int frame_px(void) {
    UINT dpi = (UINT)(g_dpi_s * 96 + 0.5f);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    int(WINAPI * metric)(int, UINT) = (void *)GetProcAddress(u, "GetSystemMetricsForDpi");
    if (metric) return metric(SM_CXFRAME, dpi) + metric(SM_CXPADDEDBORDER, dpi);
    return GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
}

/* minimize / maximize / close drawn in ink, plus a thin ink edge around the window */
static void paint_window_frame(HDC dc) {
    (void)dc;
    RECT keep = g_clip;
    g_clip = (RECT){0, 0, g_w, g_h};
    int bw = (int)(44 * s), bh = (int)(32 * s), x0 = g_w - 3 * bw;
    for (int b = 0; b < 3; b++) {
        int x = x0 + b * bw;
        int hov = hovered(x, 0, bw, bh);
        int close = b == 2;
        if (hov) shape(x, 0, bw, bh, 0, close ? C_RED : C_INK, close ? 1.f : 0.10f, 0, 0, 0);
        COLORREF ink = hov && close ? C_PAPER : C_INK;
        float cx = x + bw / 2.f, cy = bh / 2.f, r = 5.5f * s, lw = 1.6f * s;
        if (b == 0) shape(cx - r, cy, 2 * r, lw, lw / 2, ink, 1, 0, 0, 0);
        else if (b == 1) {
            if (IsZoomed(g_wnd)) { /* restore: two squares on top of each other */
                shape(cx - r + 2.5f * s, cy - r - 1 * s, 2 * r - 3 * s, 2 * r - 3 * s, 1.5f * s, C_PAPER, 0, ink, lw, 0);
                shape(cx - r, cy - r + 2 * s, 2 * r - 3 * s, 2 * r - 3 * s, 1.5f * s, hov ? C_INK : C_DESK, hov ? 0 : 1, ink, lw, 0);
            } else
                shape(cx - r, cy - r, 2 * r, 2 * r, 1.5f * s, C_PAPER, 0, ink, lw, 0);
        } else { /* close: an X made of short strokes */
            for (int k = -5; k <= 5; k++) {
                float d = k * r / 5.f;
                shape(cx + d - lw / 2, cy + d - lw / 2, lw, lw, lw / 2, ink, 1, 0, 0, 0);
                shape(cx + d - lw / 2, cy - d - lw / 2, lw, lw, lw / 2, ink, 1, 0, 0, 0);
            }
        }
        hit_add(x, 0, bw, bh, H_WIN_MIN + b, 0);
    }
    if (!IsZoomed(g_wnd)) { /* thin ink edge so the window still looks finished w/o the Windows frame */
        float e = 1.5f * s;
        shape(0, 0, g_w, e, 0, C_INK, 0.6f, 0, 0, 0);
        shape(0, g_h - e, g_w, e, 0, C_INK, 0.6f, 0, 0, 0);
        shape(0, 0, e, g_h, 0, C_INK, 0.6f, 0, 0, 0);
        shape(g_w - e, 0, e, g_h, 0, C_INK, 0.6f, 0, 0, 0);
    }
    g_clip = keep;
}

/* edges resize, the header moves the window, buttons and links in it still click */
static LRESULT title_hit_test(HWND w, LPARAM lp) {
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(w, &pt);
    RECT r;
    GetClientRect(w, &r);
    if (!IsZoomed(w)) {
        int b = (int)(6 * s);
        int left = pt.x < b, right = pt.x >= r.right - b, top = pt.y < b, bottom = pt.y >= r.bottom - b;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    if (pt.y < HEADER_H && !g_pop_active && hit_at(pt.x, pt.y) < 0) return HTCAPTION;
    if (pt.y < HEADER_H && g_pop_active) { /* a popup is open, only the window buttons work up here */
        int h = hit_at(pt.x, pt.y);
        if (h < 0 || g_hits[h].type < H_WIN_MIN) return HTCAPTION;
    }
    return HTCLIENT;
}

/* the popup card: the page behind gets dimmed and only the popups buttons click */
static void paint_popup(HDC dc) {
    g_clip = (RECT){0, 0, g_w, g_h};
    for (int i = 0; i < g_w * g_h; i++) blend(&g_px[i], C_INK, 0.45f);
    g_nhits = 0;
    int cw = (int)(520 * s);
    if (cw > g_w - (int)(40 * s)) cw = g_w - (int)(40 * s);
    int pad = (int)(26 * s);
    RECT body = {0, 0, cw - 2 * pad, 0};
    SelectObject(dc, fBody);
    DrawTextW(dc, g_pop_body, -1, &body, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    int bh = (int)(40 * s), th = (int)(34 * s);
    int ch = pad + th + (int)(10 * s) + (body.bottom - body.top) + (int)(24 * s) + bh + pad;
    int cx = (g_w - cw) / 2, cy = (g_h - ch) / 2;
    shape(cx, cy, cw, ch, 14 * s, C_PAPER, 1, C_INK, 2.6f * s, 5 * s);
    text(dc, fHead, g_pop_danger ? C_RED : C_INK, cx + pad, cy + pad, cw - 2 * pad, th, g_pop_title, DT_LEFT | DT_VCENTER);
    RECT br = {cx + pad, cy + pad + th + (int)(10 * s), cx + cw - pad, cy + pad + th + (int)(10 * s) + (body.bottom - body.top)};
    SelectObject(dc, fBody); /* text() left the title font selected */
    SetTextColor(dc, C_INK);
    DrawTextW(dc, g_pop_body, -1, &br, DT_WORDBREAK | DT_NOPREFIX);
    /* buttons go right to left, the first one is the main one */
    int bx = cx + cw - pad, by = cy + ch - pad - bh;
    for (int b = 0; b < g_pop_nb; b++) {
        SIZE sz;
        SelectObject(dc, fBtn);
        GetTextExtentPoint32W(dc, g_pop_btn[b], (int)wcslen(g_pop_btn[b]), &sz);
        int bw = sz.cx + (int)(40 * s);
        if (bw < (int)(110 * s)) bw = (int)(110 * s);
        bx -= bw;
        button(dc, bx, by, bw, bh, g_pop_btn[b], H_POP, b + 1, b == 0);
        bx -= (int)(12 * s);
    }
}

static void paint(HDC wdc) {
    RECT cr;
    GetClientRect(g_wnd, &cr);
    g_w = cr.right;
    g_h = cr.bottom;
    if (g_w <= 0 || g_h <= 0) return;
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), g_w, -g_h, 1, 32, BI_RGB}};
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, (void **)&g_px, NULL, 0);
    HGDIOBJ oldb = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    g_nhits = 0;
    g_clip = cr;

    paint_desk();
    int m = (int)(22 * s);

    /* header */
    text(dc, fTitle, C_INK, m, (int)(12 * s), g_w - 2 * m, (int)(50 * s), L"HoofMods", DT_LEFT | DT_VCENTER);
    text(dc, fSub, C_FADED, m + (int)(2 * s), (int)(58 * s), g_w - 2 * m, (int)(24 * s), L"Mod Manager for Horses of Hoofprint Bay", DT_LEFT | DT_VCENTER);
    if (g_page != 3) {
        int lw = (int)(120 * s), lh = (int)(30 * s), lx = g_w - m - lw, ly2 = (int)(46 * s);
        int hov = hovered(lx, ly2, lw, lh);
        text(dc, fSmall, hov ? C_INK : C_FADED, lx, ly2, lw, lh, L"\x270E Appearance", DT_RIGHT | DT_VCENTER);
        hit_add(lx, ly2, lw, lh, H_LOOK, 0);
    }
    if (update_latest()) { /* theres a newer HoofMods */
        wchar_t up[96];
        int page = update_has_page();
        swprintf(up, 96, page ? L"HoofMods %ls is out - get it on Nexus \x2192" : L"HoofMods %ls is out", update_latest());
        int uw = (int)(300 * s), uh = (int)(26 * s), ux = g_w - m - (int)(132 * s) - uw, uy = (int)(48 * s);
        int hov = page && hovered(ux, uy, uw, uh);
        text(dc, fBtn, hov ? C_INK : C_RED, ux, uy, uw, uh, up, DT_RIGHT | DT_VCENTER);
        if (page) hit_add(ux, uy, uw, uh, H_UPDATE, 0);
    }

    /* status card (on settings pages its the back button and page title) */
    int sy = (int)(94 * s), sh = (int)(62 * s);
    shape(m, sy, g_w - 2 * m, sh, 12 * s, C_PAPER, 1, C_INK, 2.4f * s, 3 * s);
    const wchar_t *l1, *l2;
    wchar_t buf[300];
    int bw = (int)(170 * s), bh = (int)(36 * s), bx = g_w - m - bw - (int)(14 * s), by = sy + (sh - bh) / 2;
    COLORREF c1 = C_INK;
    if (g_page) {
        int bbw = (int)(96 * s);
        button(dc, m + (int)(14 * s), by, bbw, bh, L"\x2039 Back", H_BACK, 0, 0);
        l1 = g_page == 1 ? L"BepInEx mod settings" : g_page == 2 ? g_cfg.title : L"Appearance";
        l2 = g_page == 1   ? L"Pick a mod to change its settings."
             : g_page == 2 ? L"Changes are saved straight away. Restart the game to use them."
                           : L"Make HoofMods look the way you like. Changes are saved straight away.";
        int tx = m + (int)(14 * s) + bbw + (int)(16 * s);
        text(dc, fHead, C_INK, tx, sy + (int)(8 * s), g_w - m - tx - (int)(14 * s), (int)(26 * s), l1, DT_LEFT | DT_VCENTER);
        text(dc, fSmall, C_FADED, tx, sy + (int)(34 * s), g_w - m - tx - (int)(14 * s), (int)(20 * s), l2, DT_LEFT | DT_VCENTER);
    } else if (!g_lib.game[0]) {
        l1 = L"Horses of Hoofprint Bay was not found";
        l2 = L"Point HoofMods to the game folder to manage code mods.";
        c1 = C_RED;
        button(dc, bx, by, bw, bh, L"Find game folder", H_FINDGAME, 0, 1);
    } else if (g_lib.loader == 1) {
        l1 = L"Ready - the HoofMods loader is installed";
        swprintf(buf, 300, L"%ls", g_lib.game);
        l2 = buf;
        /* one start w/ no code mods, like to check if a mod is causing problems */
        int nw = (int)(170 * s), nx = g_w - m - nw - (int)(18 * s), ny = sy + (sh - (int)(26 * s)) / 2;
        int hov = hovered(nx, ny, nw, (int)(26 * s));
        text(dc, fSmall, hov ? C_INK : C_FADED, nx, ny, nw, (int)(26 * s), L"Start without mods \x2192", DT_RIGHT | DT_VCENTER);
        hit_add(nx, ny, nw, (int)(26 * s), H_NOMODS, 0);
    } else {
        c1 = C_RED;
        if (g_lib.loader == 2) {
            l1 = L"Another mod is using hid.dll in the game folder";
            l2 = L"HoofMods won't replace it. Remove that mod to install the HoofMods loader.";
        } else if (g_lib.loader == 3) {
            l1 = L"An update for the HoofMods loader is ready";
            l2 = L"Close the game, then press Update loader.";
            button(dc, bx, by, bw, bh, L"Update loader", H_INSTALL, 0, 1);
        } else {
            l1 = L"The HoofMods loader is not installed yet";
            l2 = L"Code mods need it. Tack, clothes and language mods work without it.";
            button(dc, bx, by, bw, bh, L"Install loader", H_INSTALL, 0, 1);
        }
    }
    if (!g_page) {
        text(dc, fHead, c1, m + (int)(18 * s), sy + (int)(8 * s), bx - m - (int)(30 * s), (int)(26 * s), l1, DT_LEFT | DT_VCENTER);
        text(dc, fSmall, C_FADED, m + (int)(18 * s), sy + (int)(34 * s), bx - m - (int)(30 * s), (int)(20 * s), l2, DT_LEFT | DT_VCENTER);
    }

    /* mod list */
    int ly = sy + sh + (int)(16 * s), lb = g_h - (int)(74 * s);
    shape(m, ly, g_w - 2 * m, lb - ly, 12 * s, C_PAPER, 1, C_INK, 2.4f * s, 3 * s);
    RECT inner = {m + (int)(4 * s), ly + (int)(5 * s), g_w - m - (int)(4 * s), lb - (int)(5 * s)};
    g_clip = inner;
    g_list_rect = inner;
    g_list_h = inner.bottom - inner.top;
    int y = inner.top + (int)(8 * s) - g_scroll;
    int px = m + (int)(18 * s), pw = g_w - 2 * m - (int)(36 * s);
    int rowh = (int)(54 * s), headh = (int)(42 * s);
    if (g_page) y = paint_settings(dc, y, px, pw);
    else for (int k = 0; k < MK_COUNT; k++) {
        if (!kind_shown(k)) continue;
        int cnt = kind_count(k);
        swprintf(buf, 300, cnt ? L"%ls  (%d)" : L"%ls", KIND_TITLE[k], cnt);
        text(dc, fHead, C_INK, px, y, pw, headh, buf, DT_LEFT | DT_VCENTER);
        if (k >= MK_TACK || g_lib.game[0]) {
            int fw = (int)(110 * s);
            int fx = px + pw - fw, fy = y + (headh - (int)(26 * s)) / 2;
            int hov = hovered(fx, fy, fw, (int)(26 * s));
            text(dc, fSmall, hov ? C_INK : C_FADED, fx, fy, fw, (int)(26 * s), L"Open folder \x2192", DT_RIGHT | DT_VCENTER);
            hit_add(fx, fy, fw, (int)(26 * s), H_FOLDER, k);
            if (k == MK_BEPINEX && g_ncfg) {
                int sw = (int)(90 * s), sx = fx - sw - (int)(14 * s);
                hov = hovered(sx, fy, sw, (int)(26 * s));
                text(dc, fSmall, hov ? C_INK : C_FADED, sx, fy, sw, (int)(26 * s), L"Settings \x2192", DT_RIGHT | DT_VCENTER);
                hit_add(sx, fy, sw, (int)(26 * s), H_CFGLIST, 0);
            }
        }
        /* ink line under the heading */
        shape(px, y + headh - 3 * s, pw, 2 * s, 1 * s, C_INK, 0.55f, 0, 0, 0);
        y += headh;
        if (k == MK_BEPINEX && !g_lib.bepinex) { /* BepInEx mods cant run w/o BepInEx */
            int lh = (int)(24 * s);
            text(dc, fBody, C_RED, px + (int)(4 * s), y + (int)(6 * s), pw, lh, L"BepInEx isn't installed, so these mods can't run yet.", DT_LEFT | DT_VCENTER);
            text(dc, fSmall, C_FADED, px + (int)(4 * s), y + (int)(6 * s) + lh, pw - (int)(150 * s), (int)(20 * s),
                 L"Press Get BepInEx to download the right one, then drag that zip onto HoofMods.", DT_LEFT | DT_VCENTER);
            int gw = (int)(140 * s), gx = px + pw - gw, gy = y + (int)(6 * s) + lh;
            int hov = hovered(gx, gy, gw, (int)(20 * s));
            text(dc, fSmall, hov ? C_INK : C_RED, gx, gy, gw, (int)(20 * s), L"Get BepInEx \x2192", DT_RIGHT | DT_VCENTER);
            hit_add(gx, gy - (int)(4 * s), gw, (int)(28 * s), H_GETBEP, 0);
            y += (int)(62 * s);
        }
        if (k == MK_BEPINEX && g_lib.bepinex) { /* BepInEx itself */
            int on = g_lib.bep_on;
            COLORREF nc = on ? C_INK : C_FADED;
            if (hovered(px, y, pw, rowh) && g_my >= g_clip.top && g_my < g_clip.bottom)
                shape(px - 6 * s, y + 3 * s, pw + 12 * s, rowh - 6 * s, 8 * s, C_INK, 0.04f, 0, 0, 0);
            int tx0 = px + (int)(6 * s), tw = pw - (int)(230 * s);
            text(dc, fBody, nc, tx0, y + (int)(6 * s), tw, (int)(24 * s), L"BepInEx", DT_LEFT | DT_VCENTER);
            text(dc, fSmall, C_FADED, tx0, y + (int)(29 * s), tw, (int)(20 * s),
                 on ? L"Starts with the game, with its log window" : L"Turned off - the game starts without BepInEx and its mods",
                 DT_LEFT | DT_VCENTER);
            int tx = px + pw - (int)(60 * s), ty = y + (rowh - (int)(28 * s)) / 2;
            text(dc, fSmall, nc, tx - (int)(60 * s), ty, (int)(52 * s), (int)(28 * s), on ? L"On" : L"Off", DT_RIGHT | DT_VCENTER);
            toggle(tx, ty, on);
            hit_add(tx - (int)(70 * s), y, (int)(130 * s), rowh, H_BEPSW, 0);
            y += rowh;
        }
        if (!cnt) {
            text(dc, fSmall, C_FADED, px + (int)(4 * s), y, pw, (int)(34 * s), EMPTY_HINT[k], DT_LEFT | DT_VCENTER);
            y += (int)(40 * s);
            continue;
        }
        for (int i = 0; i < g_lib.n; i++) {
            Mod *md = &g_lib.mods[i];
            if (md->kind != k || md->hidden) continue;
            if (i == g_sel) shape(px - 6 * s, y + 3 * s, pw + 12 * s, rowh - 6 * s, 8 * s, C_INK, 0.07f, C_INK, 1.6f * s, 0);
            else if (hovered(px, y, pw, rowh) && g_my >= g_clip.top && g_my < g_clip.bottom)
                shape(px - 6 * s, y + 3 * s, pw + 12 * s, rowh - 6 * s, 8 * s, C_INK, 0.04f, 0, 0, 0);
            COLORREF nc = md->enabled ? C_INK : C_FADED;
            int tx0 = px + (int)(6 * s);
            if (k == MK_TACK || k == MK_CLOTHES) {
                wchar_t pic[MAX_PATH];
                int box = (int)(42 * s);
                if (lib_preview_path(&g_lib, i, pic)) draw_thumb(tx0, y + (rowh - box) / 2, box, pic, md->enabled);
                else shape(tx0, y + (rowh - box) / 2, box, box, 7 * s, C_PAPER, 1, C_FADED, 1.2f * s, 0);
                tx0 += box + (int)(12 * s);
            }
            int tw = pw - (int)(230 * s) - (tx0 - px - (int)(6 * s));
            text(dc, fBody, nc, tx0, y + (int)(6 * s), tw, (int)(24 * s), md->name, DT_LEFT | DT_VCENTER);
            COLORREF ic = wcsstr(md->info, L"Failed") || wcsstr(md->info, L"twice") ? C_RED : C_FADED;
            const wchar_t *info = md->info[0]     ? md->info
                                  : md->enabled   ? L""
                                  : k == MK_DLL   ? L"Turned off - won't start with the game"
                                                  : L"Turned off - stored safely by HoofMods";
            text(dc, fSmall, ic, tx0, y + (int)(29 * s), tw, (int)(20 * s), info, DT_LEFT | DT_VCENTER);
            int tx = px + pw - (int)(60 * s), ty = y + (rowh - (int)(28 * s)) / 2;
            text(dc, fSmall, nc, tx - (int)(60 * s), ty, (int)(52 * s), (int)(28 * s), md->enabled ? L"On" : L"Off", DT_RIGHT | DT_VCENTER);
            toggle(tx, ty, md->enabled);
            int rw = (int)(70 * s), rx = tx - (int)(150 * s);
            int rhov = hovered(rx, ty, rw, (int)(28 * s)) && g_my >= g_clip.top && g_my < g_clip.bottom;
            if (rhov || i == g_sel || hovered(px, y, pw, rowh))
                text(dc, fSmall, rhov ? C_RED : C_FADED, rx, ty, rw, (int)(28 * s), L"Remove", DT_CENTER | DT_VCENTER);
            hit_add(px, y, rx - px, rowh, H_ROW, i);
            hit_add(rx, ty, rw, (int)(28 * s), H_REMOVE, i);
            hit_add(tx - (int)(70 * s), y, (int)(130 * s), rowh, H_TOGGLE, i);
            y += rowh;
        }
        y += (int)(10 * s);
    }
    g_content_h = y + g_scroll - inner.top;
    g_clip = cr;

    /* footer */
    int fy = g_h - (int)(58 * s), fbw = (int)(130 * s), fbh = (int)(38 * s);
    button(dc, g_w - m - fbw, fy, fbw, fbh, L"Start game", H_START, 0, 1);
    button(dc, g_w - m - 2 * fbw - (int)(12 * s), fy, fbw, fbh, L"Install mod\x2026", H_ADD, 0, 0);
    button(dc, g_w - m - 3 * fbw - (int)(24 * s), fy, fbw, fbh, L"Refresh", H_REFRESH, 0, 0);
    int mw = g_w - 2 * m - 3 * fbw - (int)(42 * s);
    if (g_msg[0]) {
        RECT r = {m + (int)(4 * s), fy - (int)(6 * s), m + (int)(4 * s) + mw, fy + fbh + (int)(6 * s)};
        SelectObject(dc, fSmall);
        SetTextColor(dc, g_msg_bad ? C_RED : C_INK);
        RECT calc = r;
        DrawTextW(dc, g_msg, -1, &calc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        int th = calc.bottom - calc.top;
        if (th < r.bottom - r.top) r.top += (r.bottom - r.top - th) / 2;
        DrawTextW(dc, g_msg, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    } else
        text(dc, fSmall, C_FADED, m + (int)(4 * s), fy, mw, fbh,
             g_pad_mode ? L"A choose \x00B7 B back \x00B7 X remove \x00B7 LT/RT scroll" : L"Drag a mod's .zip here to install it.", DT_LEFT | DT_VCENTER);

    if (g_pop_active) paint_popup(dc);
    paint_window_frame(dc);
    if (g_pad_mode) paint_focus();
    BitBlt(wdc, 0, 0, g_w, g_h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldb);
    DeleteObject(bmp);
    DeleteDC(dc);
}

/* ==== doing stuff ==== */

static void say(int bad, const wchar_t *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vswprintf(g_msg, 256, fmt, a);
    va_end(a);
    g_msg_bad = bad;
    SetTimer(g_wnd, 1, 7000, NULL);
}

/* shows a parchment popup and waits for an answer. b1 is the main button. returns 1, 2 or 3 for
   the button pressed, 0 for Esc or closing. danger makes the title a warning colour */
static int popup(int danger, const wchar_t *title, const wchar_t *body, const wchar_t *b1, const wchar_t *b2, const wchar_t *b3) {
    if (g_edit) SetFocus(g_wnd);
    wcsncpy(g_pop_title, title, 127);
    wcsncpy(g_pop_body, body, 799);
    const wchar_t *bs[3] = {b1, b2, b3};
    g_pop_nb = 0;
    for (int i = 0; i < 3; i++)
        if (bs[i]) wcsncpy(g_pop_btn[g_pop_nb++], bs[i], 47);
    g_pop_danger = danger;
    g_pop_result = 0;
    g_pop_active = 1;
    int keep_type = g_focus_type, keep_arg = g_focus_arg; /* controller highlight moves to the popup and back after */
    g_focus_type = H_POP;
    g_focus_arg = g_pop_start;
    g_pop_start = 1;
    InvalidateRect(g_wnd, NULL, FALSE);
    MSG m;
    while (g_pop_active) {
        int r = GetMessageW(&m, NULL, 0, 0);
        if (r <= 0) { /* the app is closing, pass the quit on to the main loop */
            g_pop_active = 0;
            g_pop_result = 0;
            PostQuitMessage(0);
            break;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_focus_type = keep_type;
    g_focus_arg = keep_arg;
    InvalidateRect(g_wnd, NULL, FALSE);
    return g_pop_result;
}

static void popup_close(int result) {
    g_pop_result = result;
    g_pop_active = 0;
}

static void clamp_scroll(void) {
    int maxs = g_content_h - g_list_h + (int)(8 * s);
    if (g_scroll > maxs) g_scroll = maxs;
    if (g_scroll < 0) g_scroll = 0;
}

static void refresh(void) {
    wchar_t keep[MAX_PATH] = L"";
    ModKind kk = 0;
    if (g_sel >= 0 && g_sel < g_lib.n) wcscpy(keep, g_lib.mods[g_sel].id), kk = g_lib.mods[g_sel].kind;
    thumb_clear(); /* pics might have changed on disk */
    lib_scan(&g_lib);
    load_cfg_list();
    g_sel = -1;
    for (int i = 0; i < g_lib.n && keep[0]; i++)
        if (g_lib.mods[i].kind == kk && !wcscmp(g_lib.mods[i].id, keep)) g_sel = i;
    InvalidateRect(g_wnd, NULL, FALSE);
}

/* b4 tack/clothes leaves the game: if a save uses it, warn and back up the saves first
   returns 0 if the player changed their mind (or the backup didnt work) */
static int check_in_use(int i, const wchar_t *action, int *backed_up) {
    *backed_up = 0;
    wchar_t item[128], save[128], body[800], btn[64], where[MAX_PATH];
    int n = lib_saves_using(&g_lib, i, item, 128, save, 128);
    if (!n) return 1;
    /* tested in game: a missing item comes off the horse, and goes back where it was once its files are back */
    int removing = !wcscmp(action, L"remove");
    swprintf(body, 800,
             L"A horse in your save \x201C%ls\x201D%ls is wearing %ls.\n\nThat's fine: the game takes it off the horse while it's %ls, "
             L"and puts it back where it was %ls. HoofMods backs up your saves first anyway, just to be safe.",
             save, n > 1 ? L" (and other saves)" : L"", item, removing ? L"gone" : L"off",
             removing ? L"if you install it again later" : L"when you turn it back on");
    swprintf(btn, 64, L"Back up and %ls", action);
    g_pop_start = 2;
    if (popup(0, L"This item is in use", body, btn, L"Keep it", NULL) != 1) return 0;
    if (!lib_backup_saves(&g_lib, where, MAX_PATH)) {
        popup(1, L"Nothing was changed", L"HoofMods couldn't back up your saves, so it left everything as it was.", L"OK", NULL, NULL);
        return 0;
    }
    *backed_up = 1;
    return 1;
}

static void do_toggle(int i) {
    if (i < 0 || i >= g_lib.n) return;
    Mod *m = &g_lib.mods[i];
    wchar_t err[256], name[128];
    wcscpy(name, m->name);
    int on = !m->enabled, backed = 0;
    if (!on && !check_in_use(i, L"turn off", &backed)) return;
    if (!lib_set_enabled(&g_lib, i, on, err, 256)) {
        say(1, L"%ls", err);
    } else {
        say(0, L"%ls turned %ls.%ls%ls", name, on ? L"on" : L"off", backed ? L" Your saves were backed up first." : L"",
            lib_game_running() ? L" Restart the game to see the change." : L"");
    }
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void do_install(void) {
    wchar_t err[256];
    int update = g_lib.loader == 3;
    if (lib_install_loader(&g_lib, err, 256))
        say(0, update ? L"Loader updated." : L"Loader installed. Your code mods will start with the game.");
    else say(1, L"%ls", err);
    refresh();
}

static void do_find_game(void) {
    BROWSEINFOW bi = {.hwndOwner = g_wnd, .lpszTitle = L"Select the Horses of Hoofprint Bay game folder",
                      .ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE};
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return;
    wchar_t p[MAX_PATH];
    if (SHGetPathFromIDListW(id, p)) {
        if (lib_set_game(&g_lib, p)) say(0, L"Game found.");
        else say(1, L"That folder doesn't contain Horses of Hoofprint Bay.exe.");
    }
    CoTaskMemFree(id);
    refresh();
}

static void do_remove(int i) {
    if (i < 0 || i >= g_lib.n) return;
    Mod *m = &g_lib.mods[i];
    wchar_t q[400], err[256], name[128], title[160];
    wcscpy(name, m->name);
    swprintf(title, 160, L"Remove %ls?", name);
    if (m->count > 1)
        swprintf(q, 400, L"This removes all %d %lss in it. The files go to the Recycle Bin, so you can get them back.", m->count, KIND_ITEM[m->kind]);
    else
        swprintf(q, 400, L"The files go to the Recycle Bin, so you can get them back.");
    g_pop_start = 2;
    if (popup(0, title, q, L"Remove", L"Cancel", NULL) != 1) return;
    int backed = 0;
    if (!check_in_use(i, L"remove", &backed)) return;
    if (lib_uninstall(&g_lib, i, err, 256)) say(0, L"%ls removed.%ls", name, backed ? L" Your saves were backed up first." : L"");
    else say(1, L"%ls", err);
    g_sel = -1;
    refresh();
}

/* only asked when we cant tell from the files if its tack or clothes */
static int ask_kind(void *ctx, const wchar_t *what) {
    (void)ctx;
    wchar_t q[400];
    swprintf(q, 400, L"HoofMods couldn't tell from the files in \x201C%ls\x201D.\n\nTack is for horses (saddles, bridles, pads, boots). Clothes are for your rider.", what);
    int r = popup(0, L"Is this tack or clothes?", q, L"Tack", L"Clothes", L"Cancel");
    return r == 1 ? MK_TACK : r == 2 ? MK_CLOTHES : -1;
}

static int is_loose_file(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    const wchar_t *x = wcsrchr(p, L'.');
    return !(x && (!_wcsicmp(x, L".zip") || !_wcsicmp(x, L".7z") || !_wcsicmp(x, L".rar")));
}

static const wchar_t *file_part(const wchar_t *p) {
    const wchar_t *f = wcsrchr(p, L'\\');
    return f ? f + 1 : p;
}

/* after installing a code mod thats also loading from the game folder (StickHorse as version.dll)
   offer to turn the game folder copy off, otherwise both would react to every button */
static void offer_twin(wchar_t *msg, int msgn) {
    if (!g_lib.twin[0]) return;
    wchar_t twin[MAX_PATH];
    wcscpy(twin, g_lib.twin);
    g_lib.twin[0] = 0;
    lib_scan(&g_lib);
    for (int i = 0; i < g_lib.n; i++) {
        Mod *m = &g_lib.mods[i];
        if (m->kind != MK_DLL || wcscmp(m->id, twin) || !m->enabled) continue;
        wchar_t body[600], name[128], err[256];
        wcscpy(name, m->name);
        swprintf(body, 600,
                 L"%ls is also installed straight in the game folder (%ls). With both, it would run twice and every "
                 L"button would count twice.\n\nTurn the game-folder copy off? You can turn it back on any time.",
                 name, twin + 1);
        g_pop_start = 1;
        if (popup(1, L"Installed twice", body, L"Turn it off", L"Keep both", NULL) == 1) {
            if (lib_set_enabled(&g_lib, i, 0, err, 256)) wcsncat(msg, L" The game-folder copy is off now.", msgn - wcslen(msg) - 1);
            else swprintf(msg, msgn, L"%ls", err);
        }
        return;
    }
}

/* a few loose files dropped together (a .json w/ its .png) count as one download, they get copied
   to a temp folder named after the first file, installed, then cleaned up */
static void install_paths(wchar_t **paths, int n) {
    wchar_t msg[300] = L"";
    int ok = 0, fail = 0, first_loose = -1;
    for (int i = 0; i < n; i++) {
        if (is_loose_file(paths[i])) {
            if (first_loose < 0) first_loose = i;
            continue;
        }
        if (lib_install(&g_lib, paths[i], ask_kind, NULL, msg, 300)) ok++, offer_twin(msg, 300);
        else fail++;
    }
    if (first_loose >= 0) {
        wchar_t tmp[MAX_PATH], top[MAX_PATH + 2] = {0}, dir[MAX_PATH], base[MAX_PATH], to[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        wcscpy(base, file_part(paths[first_loose]));
        wchar_t *d = wcsrchr(base, L'.');
        if (d) *d = 0;
        swprintf(top, MAX_PATH, L"%lsHoofMods drop %lu", tmp, GetTickCount());
        swprintf(dir, MAX_PATH, L"%ls\\%ls", top, base);
        hm_mkdirs(dir);
        for (int i = 0; i < n; i++)
            if (is_loose_file(paths[i])) {
                swprintf(to, MAX_PATH, L"%ls\\%ls", dir, file_part(paths[i]));
                CopyFileW(paths[i], to, FALSE);
            }
        if (lib_install(&g_lib, dir, ask_kind, NULL, msg, 300)) ok++, offer_twin(msg, 300);
        else fail++;
        SHFILEOPSTRUCTW op = {.wFunc = FO_DELETE, .pFrom = top, .fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI};
        SHFileOperationW(&op); /* top ends w/ two nulls */
    }
    if (fail) { /* problems get the full text in a popup bc the bottom bar is too small for them */
        wchar_t title[64];
        if (ok) swprintf(title, 64, L"Installed %d, %d didn't work", ok, fail);
        else wcscpy(title, L"That didn't install");
        refresh();
        popup(1, title, msg, L"OK", NULL, NULL);
        say(1, L"%ls", title);
        return;
    }
    if (ok == 1) say(0, L"%ls", msg);
    else say(0, L"Installed %d mods.", ok);
    refresh();
}

static void do_add(void) {
    static wchar_t buf[8192];
    buf[0] = 0;
    OPENFILENAMEW of = {sizeof of};
    of.hwndOwner = g_wnd;
    of.lpstrFilter = L"Mod downloads (*.zip, *.dll, *.json, *.png)\0*.zip;*.dll;*.json;*.png\0All files\0*.*\0";
    of.lpstrFile = buf;
    of.nMaxFile = 8192;
    of.lpstrTitle = L"Install mods";
    of.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&of)) return;
    static wchar_t full[64][MAX_PATH];
    wchar_t *list[64];
    int n = 0;
    wchar_t *p = buf + wcslen(buf) + 1;
    if (!*p) {
        wcscpy(full[n], buf);
        list[n] = full[n];
        n++;
    } else
        for (; *p && n < 64; p += wcslen(p) + 1) {
            swprintf(full[n], MAX_PATH, L"%ls\\%ls", buf, p);
            list[n] = full[n];
            n++;
        }
    install_paths(list, n);
}

static void on_drop(HDROP d) {
    static wchar_t full[64][MAX_PATH];
    wchar_t *list[64];
    int n = DragQueryFileW(d, 0xFFFFFFFF, NULL, 0);
    if (n > 64) n = 64;
    for (int i = 0; i < n; i++) {
        DragQueryFileW(d, i, full[i], MAX_PATH);
        list[i] = full[i];
    }
    DragFinish(d);
    SetForegroundWindow(g_wnd);
    install_paths(list, n);
}

/* ==== settings pages ==== */

static void go_page(int p) {
    if (g_edit) SetFocus(g_wnd);
    if (p < 2 && g_page == 2) cfg_free(&g_cfg);
    g_page = p;
    g_scroll = 0;
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void cfg_save(int i, const wchar_t *v) {
    wchar_t key[96];
    wcscpy(key, g_cfg.e[i].key);
    if (!cfg_set(&g_cfg, i, v)) say(1, L"Couldn't save %ls. The settings file may be read-only.", key);
    else say(0, lib_game_running() ? L"%ls saved. Restart the game to use it." : L"%ls saved.", key);
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void fmt_num(const CfgEntry *e, double x, wchar_t *out) {
    if (cfg_is_int(e)) {
        swprintf(out, 64, L"%.0f", x);
        return;
    }
    swprintf(out, 64, L"%.4f", x);
    wchar_t *d = wcschr(out, L'.');
    if (d) {
        wchar_t *z = out + wcslen(out) - 1;
        while (z > d && *z == L'0') *z-- = 0;
        if (z == d) *z = 0;
    }
}

static void cfg_step(int i, int dir) {
    CfgEntry *e = &g_cfg.e[i];
    wchar_t v[256];
    if (e->nopts) {
        int k = 0;
        while (k < e->nopts && _wcsicmp(e->opts[k], e->value)) k++;
        k = k == e->nopts ? 0 : (k + dir + e->nopts) % e->nopts;
        wcscpy(v, e->opts[k]);
    } else {
        double x = wcstod(e->value, NULL), step = 1;
        if (!cfg_is_int(e)) { /* a round step of about a twentieth of the range */
            double raw = (e->hi - e->lo) / 20, p = pow(10, floor(log10(raw > 0 ? raw : 1)));
            step = raw >= 5 * p ? 5 * p : raw >= 2 * p ? 2 * p : p;
        }
        x += dir * step;
        if (x < e->lo) x = e->lo;
        if (x > e->hi) x = e->hi;
        fmt_num(e, x, v);
    }
    cfg_save(i, v);
}

#define WM_EDIT_END (WM_APP + 5)

static void edit_end(int commit) {
    if (!g_edit) return;
    HWND ed = g_edit;
    int i = g_edit_idx;
    g_edit = NULL;
    g_edit_idx = -1;
    wchar_t v[256];
    GetWindowTextW(ed, v, 256);
    DestroyWindow(ed);
    if (commit && i >= 0 && i < g_cfg.n && wcscmp(v, g_cfg.e[i].value)) {
        CfgEntry *e = &g_cfg.e[i];
        if (e->has_range) {
            wchar_t *end;
            double x = wcstod(v, &end);
            if (end == v) {
                say(1, L"%ls needs a number between %g and %g.", e->key, e->lo, e->hi);
                InvalidateRect(g_wnd, NULL, FALSE);
                return;
            }
            if (x < e->lo) x = e->lo;
            if (x > e->hi) x = e->hi;
            fmt_num(e, x, v);
        }
        cfg_save(i, v);
    }
    InvalidateRect(g_wnd, NULL, FALSE);
}

static LRESULT CALLBACK edit_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        PostMessageW(g_wnd, WM_EDIT_END, wp == VK_RETURN, 0);
        return 0;
    }
    if (m == WM_CHAR && (wp == L'\r' || wp == 27)) return 0;
    if (m == WM_KILLFOCUS) PostMessageW(g_wnd, WM_EDIT_END, 1, 0);
    return CallWindowProcW(g_edit_proc, w, m, wp, lp);
}

static void edit_begin(int h) {
    edit_end(1);
    RECT r = g_hits[h].r;
    int i = g_hits[h].arg;
    int pad = (int)(8 * s), eh = (int)(24 * s);
    g_edit = CreateWindowExW(0, L"EDIT", g_cfg.e[i].value, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | (g_cfg.e[i].has_range ? ES_CENTER : 0),
                             r.left + pad, r.top + (r.bottom - r.top - eh) / 2, r.right - r.left - 2 * pad, eh, g_wnd, NULL, NULL, NULL);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)fBody, TRUE);
    g_edit_proc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    g_edit_idx = i;
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void load_cfg_list(void) { g_ncfg = g_lib.game[0] ? cfg_list(g_lib.game, g_cfg_paths, g_cfg_titles, MAX_CFGS) : 0; }

static void activate(int h) {
    if (h < 0) return;
    Hit *t = &g_hits[h];
    if (g_pop_active) {
        if (t->type == H_POP) popup_close(t->arg);
        else if (t->type == H_WIN_MIN) ShowWindow(g_wnd, SW_MINIMIZE);
        else if (t->type == H_WIN_MAX) ShowWindow(g_wnd, IsZoomed(g_wnd) ? SW_RESTORE : SW_MAXIMIZE);
        else if (t->type == H_WIN_CLOSE) popup_close(0), PostMessageW(g_wnd, WM_CLOSE, 0, 0);
        return;
    }
    switch (t->type) {
    case H_BACK: go_page(g_page == 3 ? 0 : g_page - 1); break;
    case H_LOOK: go_page(3); break;
    case H_UPDATE: update_open_page(); break;
    case H_GETBEP: ShellExecuteW(NULL, L"open", L"https://builds.bepinex.dev/projects/bepinex_be/793/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.793%2Bdbc82f0.zip", NULL, NULL, SW_SHOWNORMAL); break;
    case H_WIN_MIN: ShowWindow(g_wnd, SW_MINIMIZE); break;
    case H_WIN_MAX: ShowWindow(g_wnd, IsZoomed(g_wnd) ? SW_RESTORE : SW_MAXIMIZE); break;
    case H_WIN_CLOSE: PostMessageW(g_wnd, WM_CLOSE, 0, 0); break;
    case H_THEME:
        g_theme = t->arg;
        apply_theme();
        save_look();
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    case H_ZOOM: {
        int z = g_zoom + t->arg;
        if (z >= 0 && z <= 2) {
            g_zoom = z;
            set_scale();
            save_look();
            clamp_scroll();
            InvalidateRect(g_wnd, NULL, FALSE);
        }
        break;
    }
    case H_TEXTURE:
        g_texture = !g_texture;
        save_look();
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    case H_PICK: {
        static COLORREF custom[16];
        COLORREF *target = t->arg == 0 ? &g_custom_ink : t->arg == 1 ? &g_custom_paper : &g_custom_red;
        if (g_theme != THEME_CUSTOM) { /* start from the current theme so small changes are easy */
            g_custom_ink = C_INK, g_custom_paper = C_PAPER, g_custom_red = C_RED;
        }
        CHOOSECOLORW cc = {sizeof cc};
        cc.hwndOwner = g_wnd;
        cc.rgbResult = *target;
        cc.lpCustColors = custom;
        cc.Flags = CC_RGBINIT | CC_FULLOPEN;
        if (ChooseColorW(&cc)) {
            *target = cc.rgbResult;
            g_theme = THEME_CUSTOM;
            apply_theme();
            save_look();
        }
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    }
    case H_CFGLIST: load_cfg_list(); go_page(1); break;
    case H_CFGOPEN:
        if (cfg_load(&g_cfg, g_cfg_paths[t->arg])) go_page(2);
        else say(1, L"Couldn't open that settings file.");
        break;
    case H_CFG_TOGGLE: cfg_save(t->arg, _wcsicmp(g_cfg.e[t->arg].value, L"true") ? L"true" : L"false"); break;
    case H_CFG_PREV: cfg_step(t->arg, -1); break;
    case H_CFG_NEXT: cfg_step(t->arg, 1); break;
    case H_CFG_EDIT: edit_begin(h); break;
    case H_TOGGLE: g_sel = t->arg; do_toggle(t->arg); break;
    case H_REMOVE: do_remove(t->arg); break;
    case H_ADD: do_add(); break;
    case H_ROW: g_sel = t->arg; InvalidateRect(g_wnd, NULL, FALSE); break;
    case H_FOLDER: lib_open_folder(&g_lib, t->arg); break;
    case H_INSTALL: do_install(); break;
    case H_FINDGAME: do_find_game(); break;
    case H_REFRESH: refresh(); say(0, L"List refreshed."); break;
    case H_START:
        if (!g_lib.game[0]) say(1, L"Find the game folder first.");
        else if (lib_game_running()) say(0, L"The game is already running.");
        else lib_start_game(&g_lib), say(0, L"Starting the game\x2026");
        break;
    case H_BEPSW: {
        wchar_t err[200];
        if (!lib_set_bepinex(&g_lib, !g_lib.bep_on, err, 200)) say(1, L"%ls", err);
        else say(0, g_lib.bep_on ? L"BepInEx turned on. It starts with the game again." : L"BepInEx turned off. The game starts without it.");
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    }
    case H_NOMODS:
        if (lib_game_running()) say(0, L"The game is already running. Close it first.");
        else if (!lib_start_without_mods(&g_lib)) say(1, L"Could not start without mods.");
        else say(0, L"Starting the game without code mods, just this once.");
        break;
    }
}

static int hit_at(int x, int y) {
    for (int i = g_nhits - 1; i >= 0; i--)
        if (x >= g_hits[i].r.left && x < g_hits[i].r.right && y >= g_hits[i].r.top && y < g_hits[i].r.bottom) return i;
    return -1;
}

/* ==== controller ==== */

static int focusable(const Hit *h) { return h->type != H_ROW && h->type < H_WIN_MIN && h->full.right > h->full.left; }

static int focus_index(void) {
    for (int i = 0; i < g_nhits; i++)
        if (g_hits[i].type == g_focus_type && g_hits[i].arg == g_focus_arg && focusable(&g_hits[i])) return i;
    return -1;
}

/* where the highlight starts: the popups main button, else the first switch, else anything */
static int focus_default(void) {
    int best = -1;
    for (int i = 0; i < g_nhits; i++) {
        const Hit *h = &g_hits[i];
        if (!focusable(h) || h->r.bottom <= h->r.top) continue;
        if (h->type == H_POP && h->arg == 1) return i;
        if (best < 0 || (h->type == H_TOGGLE && g_hits[best].type != H_TOGGLE) || (h->in_list && !g_hits[best].in_list)) best = i;
    }
    return best;
}

static void paint_focus(void) {
    int i = focus_index();
    if (i < 0) {
        i = focus_default();
        if (i < 0) return;
        g_focus_type = g_hits[i].type;
        g_focus_arg = g_hits[i].arg;
    }
    RECT r = g_hits[i].r;
    if (r.bottom <= r.top) return;
    RECT keep = g_clip;
    if (g_hits[i].in_list) g_clip = g_list_rect;
    else g_clip = (RECT){0, 0, g_w, g_h};
    float o = 5 * s;
    shape(r.left - o, r.top - o, r.right - r.left + 2 * o, r.bottom - r.top + 2 * o, 10 * s, C_INK, 0, C_INK, 3 * s, 0);
    g_clip = keep;
}

/* keeps a highlighted list item in the part of the list you can see */
static void scroll_into_view(const Hit *h) {
    if (!h->in_list) return;
    int margin = (int)(10 * s);
    if (h->full.top < g_list_rect.top + margin) g_scroll -= g_list_rect.top + margin - h->full.top;
    else if (h->full.bottom > g_list_rect.bottom - margin) g_scroll += h->full.bottom - (g_list_rect.bottom - margin);
    clamp_scroll();
}

/* moves the highlight to the closest thing you can press in that direction */
static void nav(int dx, int dy) {
    int cur = focus_index();
    if (cur < 0) cur = focus_default();
    if (cur < 0) return;
    RECT c = g_hits[cur].full;
    float cx = (c.left + c.right) / 2.f, cy = (c.top + c.bottom) / 2.f;
    int best = -1;
    float best_score = 1e30f;
    for (int i = 0; i < g_nhits; i++) {
        if (i == cur || !focusable(&g_hits[i])) continue;
        if (g_pop_active && g_hits[i].type != H_POP) continue;
        RECT r = g_hits[i].full;
        if (!g_hits[i].in_list && r.bottom <= r.top) continue;
        float x = (r.left + r.right) / 2.f, y = (r.top + r.bottom) / 2.f;
        float along = dx ? (x - cx) * dx : (y - cy) * dy;   /* how far in the direction we want */
        float across = dx ? fabsf(y - cy) : fabsf(x - cx); /* how far off to the side */
        if (along <= 4 * s) continue;
        /* rows: try to stay in the same column, side moves: try to stay on the same line */
        float score = along + across * (dx ? 3.f : 1.2f);
        if (score < best_score) best_score = score, best = i;
    }
    if (best < 0) return;
    g_focus_type = g_hits[best].type;
    g_focus_arg = g_hits[best].arg;
    scroll_into_view(&g_hits[best]);
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void pad_press_a(void) {
    int i = focus_index();
    if (i < 0) i = focus_default();
    if (i >= 0) {
        if (g_hits[i].type == H_TOGGLE) g_sel = g_hits[i].arg;
        activate(i);
    }
}

static void pad_press_b(void) {
    if (g_pop_active) popup_close(0);
    else if (g_edit) SetFocus(g_wnd);
    else if (g_page) go_page(g_page == 3 ? 0 : g_page - 1);
}

static void pad_press_x(void) { /* remove the highlighted mod */
    int i = focus_index();
    if (g_pop_active || g_page || i < 0) return;
    if (g_hits[i].type == H_TOGGLE || g_hits[i].type == H_REMOVE) do_remove(g_hits[i].arg);
}

static void pad_tick(void) {
    static unsigned short prev;
    static DWORD next_repeat;
    static int held_dir = -1;
    PadState p;
    if (GetForegroundWindow() != g_wnd || !pad_read(&p)) { /* only while HoofMods is the window youre using */
        prev = 0;
        held_dir = -1;
        return;
    }
    /* dpad and left stick share one direction, hold to repeat */
    int dir = -1;
    if (p.buttons & PAD_UP || p.ly > 0.55f) dir = 0;
    else if (p.buttons & PAD_DOWN || p.ly < -0.55f) dir = 1;
    else if (p.buttons & PAD_LEFT || p.lx < -0.55f) dir = 2;
    else if (p.buttons & PAD_RIGHT || p.lx > 0.55f) dir = 3;
    DWORD now = GetTickCount();
    if (dir >= 0 && (dir != held_dir || now >= next_repeat)) {
        next_repeat = now + (dir != held_dir ? 380 : 110);
        if (!g_pad_mode) g_pad_mode = 1; /* first touch just shows the highlight */
        else nav(dir == 2 ? -1 : dir == 3 ? 1 : 0, dir == 0 ? -1 : dir == 1 ? 1 : 0);
        g_mx = g_my = -1;
        InvalidateRect(g_wnd, NULL, FALSE);
    }
    held_dir = dir;
    unsigned short down = p.buttons & ~prev;
    prev = p.buttons;
    if (down & (PAD_A | PAD_B | PAD_X)) {
        int was = g_pad_mode;
        g_pad_mode = 1;
        g_mx = g_my = -1;
        InvalidateRect(g_wnd, NULL, FALSE);
        if (was || g_pop_active) { /* first press after using the mouse just shows the highlight */
            if (down & PAD_A) pad_press_a();
            else if (down & PAD_B) pad_press_b();
            else if (down & PAD_X) pad_press_x();
        }
    }
    /* scrolling: triggers or right stick */
    float sc = p.rt - p.lt;
    if (fabsf(p.ry) > 0.25f) sc -= p.ry;
    if (fabsf(sc) > 0.15f && !g_pop_active) {
        g_scroll += (int)(sc * 18 * s);
        clamp_scroll();
        InvalidateRect(g_wnd, NULL, FALSE);
    }
}

static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        paint(dc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_NCCALCSIZE:
        if (wp) { /* no Windows title bar, the parchment header is the title bar */
            if (IsZoomed(w)) { /* a maximized window hangs over the screen edge by its frame */
                NCCALCSIZE_PARAMS *pr = (NCCALCSIZE_PARAMS *)lp;
                int f = frame_px();
                pr->rgrc[0].left += f;
                pr->rgrc[0].top += f;
                pr->rgrc[0].right -= f;
                pr->rgrc[0].bottom -= f;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: return title_hit_test(w, lp);
    case WM_CREATE: {
        MARGINS mg = {0, 0, 1, 0}; /* keeps the Windows shadow w/o the frame */
        DwmExtendFrameIntoClientArea(w, &mg);
        SetWindowPos(w, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DROPFILES:
        if (g_pop_active) DragFinish((HDROP)wp);
        else on_drop((HDROP)wp);
        return 0;
    case WM_SIZE: edit_end(1); clamp_scroll(); InvalidateRect(w, NULL, FALSE); return 0;
    case WM_EDIT_END: edit_end((int)wp); return 0;
    case WM_HM_UPDATE: InvalidateRect(w, NULL, FALSE); return 0;
    case WM_CTLCOLOREDIT: {
        static HBRUSH br;
        static COLORREF br_col = (COLORREF)-1;
        if (br_col != C_PAPER) {
            if (br) DeleteObject(br);
            br = CreateSolidBrush(C_PAPER);
            br_col = C_PAPER;
        }
        SetTextColor((HDC)wp, C_INK);
        SetBkColor((HDC)wp, C_PAPER);
        return (LRESULT)br;
    }
    case WM_LBUTTONDOWN: SetFocus(w); return 0; /* clicking away from a text box saves it */
    case WM_DPICHANGED: {
        g_dpi_s = HIWORD(wp) / 96.f;
        set_scale();
        RECT *r = (RECT *)lp;
        SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = (LONG)(640 * s);
        mm->ptMinTrackSize.y = (LONG)(460 * s);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (GET_X_LPARAM(lp) == g_mx && GET_Y_LPARAM(lp) == g_my) return 0; /* not a real move */
        g_pad_mode = 0;
        g_mx = GET_X_LPARAM(lp);
        g_my = GET_Y_LPARAM(lp);
        int h = hit_at(g_mx, g_my);
        TRACKMOUSEEVENT tm = {sizeof tm, TME_LEAVE, w, 0};
        TrackMouseEvent(&tm);
        SetCursor(LoadCursorW(NULL, h >= 0 && g_hits[h].type != H_ROW ? IDC_HAND : IDC_ARROW));
        InvalidateRect(w, NULL, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE: g_mx = g_my = -1; InvalidateRect(w, NULL, FALSE); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) return 1;
        break;
    case WM_LBUTTONUP: activate(hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))); return 0;
    case WM_MOUSEWHEEL:
        if (g_pop_active) return 0;
        if (g_edit) SetFocus(w);
        g_scroll -= GET_WHEEL_DELTA_WPARAM(wp) * (int)(60 * s) / WHEEL_DELTA;
        clamp_scroll();
        InvalidateRect(w, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        /* keyboard moves the same highlight as the controller */
        if (wp == VK_UP || wp == VK_DOWN || wp == VK_LEFT || wp == VK_RIGHT) {
            g_mx = g_my = -1;
            if (!g_pad_mode) g_pad_mode = 1;
            else nav(wp == VK_LEFT ? -1 : wp == VK_RIGHT ? 1 : 0, wp == VK_UP ? -1 : wp == VK_DOWN ? 1 : 0);
            InvalidateRect(w, NULL, FALSE);
            return 0;
        }
        if (wp == VK_RETURN || wp == VK_SPACE) {
            if (g_pad_mode) pad_press_a();
            else if (g_pop_active && wp == VK_RETURN) popup_close(1); /* Enter = main button */
            return 0;
        }
        if (wp == VK_ESCAPE) {
            pad_press_b();
            return 0;
        }
        if (g_pop_active) return 0;
        if (wp == VK_DELETE) pad_press_x();
        switch (wp) {
        case VK_F5: refresh(); break;
        case VK_PRIOR: g_scroll -= g_list_h; clamp_scroll(); InvalidateRect(w, NULL, FALSE); break;
        case VK_NEXT: g_scroll += g_list_h; clamp_scroll(); InvalidateRect(w, NULL, FALSE); break;
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && !g_pop_active) refresh(); /* pick up files that changed while the window was in the background */
        return 0;
    case WM_TIMER:
        if (wp == 2) {
            pad_tick();
            return 0;
        }
        KillTimer(w, 1);
        g_msg[0] = 0;
        InvalidateRect(w, NULL, FALSE);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev;
    (void)cmd;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    BOOL(WINAPI * set_ctx)(HANDLE) = (void *)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (set_ctx) set_ctx((HANDLE)-4); /* per monitor v2 */
    else SetProcessDPIAware();

    lib_init(&g_lib);
    lib_find_game(&g_lib);
    lib_scan(&g_lib);

    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"HoofModsApp";
    RegisterClassExW(&wc);

    HDC sdc = GetDC(NULL);
    g_dpi_s = GetDeviceCaps(sdc, LOGPIXELSX) / 96.f;
    ReleaseDC(NULL, sdc);
    load_look();
    set_scale();
    g_wnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            (int)(860 * s), (int)(680 * s), NULL, NULL, inst, NULL);
    UINT (WINAPI *get_dpi)(HWND) = (void *)GetProcAddress(u, "GetDpiForWindow");
    if (get_dpi) {
        float ns = get_dpi(g_wnd) / 96.f;
        if (ns != g_dpi_s) {
            g_dpi_s = ns;
            set_scale();
            SetWindowPos(g_wnd, NULL, 0, 0, (int)(860 * s), (int)(680 * s), SWP_NOMOVE | SWP_NOZORDER);
        }
    }
    DragAcceptFiles(g_wnd, TRUE);
    SetTimer(g_wnd, 2, 16, NULL); /* controller */
    update_start(g_wnd, g_lib.data);
    ShowWindow(g_wnd, show);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
