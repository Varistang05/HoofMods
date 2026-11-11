/* small zip reader, does stored + deflate files and checks the CRC */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "zip.h"

#define MAX_ZIP (1024u * 1024u * 1024u)

static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* ==== inflate ==== */

typedef struct {
    const unsigned char *in, *end;
    unsigned bitbuf, bitcnt;
    unsigned char *out;
    size_t pos, cap;
    int bad;
} Inf;

typedef struct {
    short count[16];
    short symbol[288];
} Huff;

static int bits(Inf *s, unsigned n) {
    while (s->bitcnt < n) {
        if (s->in >= s->end) {
            s->bad = 1;
            return 0;
        }
        s->bitbuf |= (unsigned)*s->in++ << s->bitcnt;
        s->bitcnt += 8;
    }
    int v = (int)(s->bitbuf & ((1u << n) - 1));
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

static int build(Huff *h, const unsigned char *len, int n) {
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int i = 1; i < 16; i++) {
        left <<= 1;
        left -= h->count[i];
        if (left < 0) return -1; /* too many codes, the data is broken */
    }
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++)
        if (len[i]) h->symbol[offs[len[i]]++] = (short)i;
    return 0;
}

static int decode(Inf *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits(s, 1);
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (s->bad) return -1;
    }
    s->bad = 1;
    return -1;
}

static const short LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(Inf *s, const Huff *lc, const Huff *dc) {
    for (;;) {
        int sym = decode(s, lc);
        if (s->bad || sym < 0) return -1;
        if (sym < 256) {
            if (s->pos >= s->cap) return -1;
            s->out[s->pos++] = (unsigned char)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            size_t len = LBASE[sym] + bits(s, LEXT[sym]);
            int ds = decode(s, dc);
            if (s->bad || ds < 0 || ds >= 30) return -1;
            size_t dist = DBASE[ds] + bits(s, DEXT[ds]);
            if (s->bad || dist > s->pos || s->pos + len > s->cap) return -1;
            for (size_t k = 0; k < len; k++, s->pos++) s->out[s->pos] = s->out[s->pos - dist];
        }
    }
}

static int inflate_block_stored(Inf *s) {
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->end - s->in < 4) return -1;
    unsigned len = rd16(s->in), nlen = rd16(s->in + 2);
    s->in += 4;
    if (len != (~nlen & 0xFFFF) || (size_t)(s->end - s->in) < len || s->pos + len > s->cap) return -1;
    memcpy(s->out + s->pos, s->in, len);
    s->in += len;
    s->pos += len;
    return 0;
}

static int inflate_block_fixed(Inf *s) {
    static Huff lc, dc;
    static int ready;
    if (!ready) {
        unsigned char l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&dc, l, 30);
        ready = 1;
    }
    return codes(s, &lc, &dc);
}

static int inflate_block_dynamic(Inf *s) {
    static const unsigned char ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    unsigned char len[320];
    Huff lc, dc;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->bad || nlen > 286 || ndist > 30) return -1;
    memset(len, 0, sizeof len);
    for (int i = 0; i < ncode; i++) len[ORDER[i]] = (unsigned char)bits(s, 3);
    if (build(&lc, len, 19)) return -1;
    int i = 0;
    while (i < nlen + ndist) {
        int sym = decode(s, &lc);
        if (s->bad || sym < 0) return -1;
        if (sym < 16) {
            len[i++] = (unsigned char)sym;
            continue;
        }
        int rep, val = 0;
        if (sym == 16) {
            if (!i) return -1;
            val = len[i - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) rep = 3 + bits(s, 3);
        else rep = 11 + bits(s, 7);
        if (i + rep > nlen + ndist) return -1;
        while (rep--) len[i++] = (unsigned char)val;
    }
    if (!len[256]) return -1;
    if (build(&lc, len, nlen) < 0 && nlen - lc.count[0] != 1) return -1;
    if (build(&dc, len + nlen, ndist) < 0 && ndist - dc.count[0] != 1) return -1;
    return codes(s, &lc, &dc);
}

static int inflate_raw(const unsigned char *in, size_t inlen, unsigned char *out, size_t outlen) {
    Inf s = {in, in + inlen, 0, 0, out, 0, outlen, 0};
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.bad) return -1;
        int r = type == 0 ? inflate_block_stored(&s) : type == 1 ? inflate_block_fixed(&s) : type == 2 ? inflate_block_dynamic(&s) : -1;
        if (r) return -1;
    } while (!last);
    return s.pos == outlen ? 0 : -1;
}

static unsigned crc32(const unsigned char *p, size_t n) {
    static unsigned t[256];
    if (!t[1])
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
    unsigned c = 0xFFFFFFFFu;
    while (n--) c = t[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

/* ==== the zip itself ==== */

int zip_open(Zip *z, const wchar_t *path, const char **err) {
    memset(z, 0, sizeof *z);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return *err = "could not open the file", -1;
    LARGE_INTEGER sz;
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart > MAX_ZIP || sz.QuadPart < 22) {
        CloseHandle(f);
        return *err = sz.QuadPart < 22 ? "the zip is empty or damaged" : "the zip is too large", -1;
    }
    z->len = (size_t)sz.QuadPart;
    z->buf = malloc(z->len);
    DWORD got = 0;
    BOOL ok = z->buf && ReadFile(f, z->buf, (DWORD)z->len, &got, NULL) && got == z->len;
    CloseHandle(f);
    if (!ok) return zip_close(z), *err = "could not read the file", -1;

    /* find the end record by searching back from the end (a zip comment can come after it) */
    long eocd = -1;
    for (long i = (long)z->len - 22; i >= 0 && i >= (long)z->len - 65557; i--)
        if (rd32(z->buf + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) return zip_close(z), *err = "this is not a zip file (7z and rar aren't supported yet)", -1;
    unsigned count = rd16(z->buf + eocd + 10);
    size_t cd = rd32(z->buf + eocd + 16);
    if (cd >= z->len) return zip_close(z), *err = "the zip is damaged", -1;
    z->e = calloc(count ? count : 1, sizeof(ZipEntry));
    size_t p = cd;
    for (unsigned i = 0; i < count; i++) {
        if (p + 46 > z->len || rd32(z->buf + p) != 0x02014b50) return zip_close(z), *err = "the zip is damaged", -1;
        const unsigned char *h = z->buf + p;
        unsigned flags = rd16(h + 8), nlen = rd16(h + 28), xlen = rd16(h + 30), clen = rd16(h + 32);
        if (p + 46 + nlen > z->len) return zip_close(z), *err = "the zip is damaged", -1;
        if (flags & 1) return zip_close(z), *err = "the zip is password protected", -1;
        ZipEntry *e = &z->e[z->n++];
        e->method = rd16(h + 10);
        e->crc = rd32(h + 16);
        e->csize = rd32(h + 20);
        e->usize = rd32(h + 24);
        size_t loc = rd32(h + 42);
        int k = nlen < sizeof e->name - 1 ? (int)nlen : (int)sizeof e->name - 1;
        memcpy(e->name, h + 46, k);
        e->name[k] = 0;
        for (char *c = e->name; *c; c++)
            if (*c == '\\') *c = '/';
        e->dir = k && e->name[k - 1] == '/';
        if (loc + 30 > z->len || rd32(z->buf + loc) != 0x04034b50) return zip_close(z), *err = "the zip is damaged", -1;
        e->data_off = loc + 30 + rd16(z->buf + loc + 26) + rd16(z->buf + loc + 28);
        if (e->data_off + e->csize > z->len) return zip_close(z), *err = "the zip is damaged", -1;
        p += 46 + nlen + xlen + clen;
    }
    return 0;
}

unsigned char *zip_read(Zip *z, int i, const char **err) {
    ZipEntry *e = &z->e[i];
    unsigned char *out = malloc(e->usize ? e->usize : 1);
    if (!out) return *err = "out of memory", NULL;
    const unsigned char *src = z->buf + e->data_off;
    int r;
    if (e->method == 0) r = e->csize == e->usize ? (memcpy(out, src, e->usize), 0) : -1;
    else if (e->method == 8) r = inflate_raw(src, e->csize, out, e->usize);
    else return free(out), *err = "the zip uses an unsupported compression method", NULL;
    if (r || crc32(out, e->usize) != e->crc) return free(out), *err = "the zip is damaged", NULL;
    return out;
}

void zip_close(Zip *z) {
    free(z->buf);
    free(z->e);
    memset(z, 0, sizeof *z);
}
