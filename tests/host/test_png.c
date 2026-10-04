// png_rows.c: PNGs built here (miniz's deflate, the same library as the ROM's inflate) decoded row by row and compared
// pixel by pixel: grey, RGB, palette (+ tRNS), grey+alpha, RGBA; the five row filters; data split over two IDAT
// chunks; a chunk length that wraps around (it read past the buffer on 32-bit before v1.12.0).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "miniz.h"
#include "png_rows.h"

#define W 7
#define H 5

static uint8_t want[H][W][4];           // expected RGBA
static uint8_t got[H][W][4];
static int rows_seen;

static bool on_row(unsigned y, const uint8_t *rgba, unsigned w, void *user)
{
    if (y < H && w == W) memcpy(got[y], rgba, W * 4);
    rows_seen++;
    return true;
}

static uint8_t *put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; return p + 4; }

static uint8_t *chunk(uint8_t *p, const char *type, const uint8_t *data, uint32_t n)
{
    p = put32(p, n);
    memcpy(p, type, 4);
    if (n) memcpy(p + 4, data, n);
    p += 4 + n;
    return put32(p, 0);                  // CRC: png_rows doesn't check it (the TLS layer does integrity)
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Raw samples per pixel for colour type ct, from the expected RGBA (and the palette index for ct 3)
static int bpp_of(int ct) { return ct == 6 ? 4 : ct == 2 ? 3 : ct == 4 ? 2 : 1; }

static size_t build(uint8_t *png, int ct, int filter_mode, bool split)
{
    int bpp = bpp_of(ct), rb = W * bpp;
    uint8_t raw[H][W * 4], fil[H * (1 + W * 4)];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *o = &raw[y][x * bpp], *e = want[y][x];
            uint8_t r = (uint8_t)(x * 37 + y * 11), g = (uint8_t)(y * 53 + 7), b = (uint8_t)(x * y * 19), a = (uint8_t)(255 - x * 30);
            int idx = (x + y) % 4;
            static const uint8_t pal[4][4] = { {10, 20, 30, 255}, {200, 0, 0, 128}, {0, 255, 0, 0}, {1, 2, 3, 255} };
            switch (ct) {
            case 0: o[0] = r; e[0] = e[1] = e[2] = r; e[3] = 255; break;
            case 2: o[0] = r; o[1] = g; o[2] = b; e[0] = r; e[1] = g; e[2] = b; e[3] = 255; break;
            case 3: o[0] = idx; memcpy(e, pal[idx], 4); break;
            case 4: o[0] = r; o[1] = a; e[0] = e[1] = e[2] = r; e[3] = a; break;
            case 6: o[0] = r; o[1] = g; o[2] = b; o[3] = a; e[0] = r; e[1] = g; e[2] = b; e[3] = a; break;
            }
        }
    size_t k = 0;
    for (int y = 0; y < H; y++) {
        int f = filter_mode < 5 ? filter_mode : y % 5;     // 5: every row a different filter
        fil[k++] = f;
        for (int i = 0; i < rb; i++) {
            int cur = raw[y][i], left = i >= bpp ? raw[y][i - bpp] : 0, up = y ? raw[y - 1][i] : 0,
                ul = y && i >= bpp ? raw[y - 1][i - bpp] : 0;
            int v = f == 0 ? cur : f == 1 ? cur - left : f == 2 ? cur - up : f == 3 ? cur - ((left + up) >> 1)
                  : cur - paeth(left, up, ul);
            fil[k++] = (uint8_t)v;
        }
    }
    mz_ulong zn = compressBound(k);
    uint8_t *z = malloc(zn);
    compress(z, &zn, fil, k);
    uint8_t *p = png;
    memcpy(p, "\x89PNG\r\n\x1a\n", 8);
    p += 8;
    uint8_t ihdr[13] = {0};
    put32(ihdr, W);
    put32(ihdr + 4, H);
    ihdr[8] = 8;
    ihdr[9] = ct;
    p = chunk(p, "IHDR", ihdr, 13);
    if (ct == 3) {
        static const uint8_t plte[12] = { 10, 20, 30, 200, 0, 0, 0, 255, 0, 1, 2, 3 }, trns[3] = { 255, 128, 0 };
        p = chunk(p, "PLTE", plte, 12);
        p = chunk(p, "tRNS", trns, 3);
    }
    if (split) { p = chunk(p, "IDAT", z, zn / 2); p = chunk(p, "IDAT", z + zn / 2, zn - zn / 2); }
    else p = chunk(p, "IDAT", z, zn);
    p = chunk(p, "IEND", NULL, 0);
    free(z);
    return p - png;
}

static bool decode(const uint8_t *png, size_t n)
{
    memset(got, 0xEE, sizeof(got));
    rows_seen = 0;
    unsigned w = 0, h = 0;
    bool ok = png_rows(png, n, on_row, NULL, &w, &h);
    return ok && w == W && h == H && rows_seen == H;
}

int main(void)
{
    static uint8_t png[8192];
    static const int types[] = { 0, 2, 3, 4, 6 };
    for (int t = 0; t < 5; t++)
        for (int f = 0; f <= 5; f++) {
            size_t n = build(png, types[t], f, f == 5);
            CHECK(decode(png, n), "colour type %d, filter %d: not decoded", types[t], f);
            CHECK(!memcmp(want, got, sizeof(want)), "colour type %d, filter %d: pixels differ", types[t], f);
        }
    // A chunk length that wraps p + 12 + n around: rejected, nothing read past the buffer (ASan watches)
    size_t n = build(png, 2, 0, false);
    uint8_t *bad = malloc(n);
    memcpy(bad, png, n);
    put32(bad + 8 + 25, 0xFFFFFFF8u);                    // the chunk after IHDR (IDAT)
    CHECK(!decode(bad, n), "a wrapped chunk length was accepted");
    free(bad);
    // Not a PNG, too short, 16-bit depth: refused
    CHECK(!png_rows((const uint8_t *)"not a png at all, really not one", 33, on_row, NULL, NULL, NULL), "not a PNG");
    n = build(png, 2, 0, false);
    png[8 + 8 + 8] = 16;
    CHECK(!decode(png, n), "16-bit depth accepted");
    return check_done("png");
}
