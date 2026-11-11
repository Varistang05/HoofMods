/* Thumbnails of tack/clothes pictures, decoded with Windows Imaging Component and cached. */
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <stdlib.h>
#include <wchar.h>
#include "preview.h"

#define MAX_THUMBS 256

static Thumb g_cache[MAX_THUMBS];
static int g_n;
static IWICImagingFactory *g_wic;

static Thumb *load(const wchar_t *path, int size) {
    if (!g_wic && FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&g_wic)))
        return NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *sc = NULL;
    IWICFormatConverter *cv = NULL;
    Thumb *t = NULL;
    UINT w, h;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(g_wic, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &frame))) goto done;
    if (FAILED(IWICBitmapFrameDecode_GetSize(frame, &w, &h)) || !w || !h) goto done;
    UINT tw = w >= h ? (UINT)size : (UINT)(size * (double)w / h), th = h >= w ? (UINT)size : (UINT)(size * (double)h / w);
    if (!tw) tw = 1;
    if (!th) th = 1;
    if (FAILED(IWICImagingFactory_CreateBitmapScaler(g_wic, &sc))) goto done;
    if (FAILED(IWICBitmapScaler_Initialize(sc, (IWICBitmapSource *)frame, tw, th, WICBitmapInterpolationModeFant))) goto done;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(g_wic, &cv))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)sc, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0,
                                              WICBitmapPaletteTypeCustom)))
        goto done;
    DWORD *px = malloc(tw * th * 4);
    if (!px) goto done;
    if (FAILED(IWICFormatConverter_CopyPixels(cv, NULL, tw * 4, tw * th * 4, (BYTE *)px))) {
        free(px);
        goto done;
    }
    t = malloc(sizeof *t);
    t->w = (int)tw;
    t->h = (int)th;
    t->px = px;
done:
    if (cv) IWICFormatConverter_Release(cv);
    if (sc) IWICBitmapScaler_Release(sc);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (dec) IWICBitmapDecoder_Release(dec);
    return t;
}

const Thumb *thumb_get(const wchar_t *path, int size) {
    for (int i = 0; i < g_n; i++)
        if (g_cache[i].size == size && !wcscmp(g_cache[i].path, path)) return g_cache[i].px ? &g_cache[i] : NULL;
    if (g_n == MAX_THUMBS) thumb_clear();
    Thumb *slot = &g_cache[g_n++];
    memset(slot, 0, sizeof *slot);
    wcsncpy(slot->path, path, MAX_PATH - 1);
    slot->size = size;
    Thumb *t = load(path, size); /* failures are cached too, so a broken picture isn't retried every frame */
    if (t) {
        slot->w = t->w;
        slot->h = t->h;
        slot->px = t->px;
        free(t);
    }
    return slot->px ? slot : NULL;
}

void thumb_clear(void) {
    for (int i = 0; i < g_n; i++) free(g_cache[i].px);
    g_n = 0;
}
