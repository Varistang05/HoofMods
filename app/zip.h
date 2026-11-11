#pragma once
#include <stddef.h>

typedef struct {
    char name[520];          /* UTF-8, '/' separators */
    int dir;
    unsigned method, crc;
    size_t csize, usize, data_off;
} ZipEntry;

typedef struct {
    unsigned char *buf;
    size_t len;
    ZipEntry *e;
    int n;
} Zip;

/* 0 on success; err gets a short reason otherwise. */
int zip_open(Zip *z, const wchar_t *path, const char **err);
/* Returns a malloc'd copy of the entry's contents (usize bytes) or NULL. */
unsigned char *zip_read(Zip *z, int i, const char **err);
void zip_close(Zip *z);
