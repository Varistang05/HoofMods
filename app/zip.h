#pragma once
#include <stddef.h>

typedef struct {
    char name[520];          /* UTF8, folders split by / */
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

/* 0 if it worked, otherwise err says why */
int zip_open(Zip *z, const wchar_t *path, const char **err);
/* gives back a malloc'd copy of the file (usize bytes) or NULL */
unsigned char *zip_read(Zip *z, int i, const char **err);
void zip_close(Zip *z);
