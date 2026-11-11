#pragma once
#include <windows.h>

typedef struct {
    wchar_t path[MAX_PATH];
    int size;
    int w, h;
    DWORD *px; /* premultiplied BGRA, NULL if the picture couldn't be read */
} Thumb;

const Thumb *thumb_get(const wchar_t *path, int size);
void thumb_clear(void);
