// Characters the font can draw (see textfit.h)
#include "textfit.h"
#include <string.h>

static const uint8_t *seg;              // the cmap format 4 subtable
static int nseg;

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

void textfit_init(const uint8_t *ttf, size_t len)
{
    seg = NULL;
    nseg = 0;
    if (!ttf || len < 12) return;
    int ntab = u16(ttf + 4);
    const uint8_t *cmap = NULL;
    for (int i = 0; i < ntab && 12 + i * 16 + 16 <= (int)len; i++) {
        const uint8_t *r = ttf + 12 + i * 16;
        if (!memcmp(r, "cmap", 4) && u32(r + 8) + 4 <= len) { cmap = ttf + u32(r + 8); break; }
    }
    if (!cmap) return;
    int nenc = u16(cmap + 2);
    for (int i = 0; i < nenc; i++) {                     // Unicode BMP: (3,1) Windows or (0,x) Unicode
        const uint8_t *e = cmap + 4 + i * 8;
        int plat = u16(e), enc = u16(e + 2);
        const uint8_t *sub = cmap + u32(e + 4);
        if ((size_t)(sub - ttf) + 14 > len || u16(sub) != 4) continue;
        if ((plat == 3 && enc == 1) || plat == 0) {
            seg = sub;
            nseg = u16(sub + 6) / 2;
            return;
        }
    }
}

bool textfit_has(uint32_t cp)
{
    if (!seg) return true;                               // unknown font: keep everything
    if (cp > 0xFFFF) return false;                       // above the BMP: emoji and the like
    const uint8_t *end = seg + 14, *start = end + nseg * 2 + 2, *delta = start + nseg * 2, *range = delta + nseg * 2;
    for (int i = 0; i < nseg; i++) {
        if (cp > u16(end + i * 2)) continue;
        uint16_t s = u16(start + i * 2);
        if (cp < s) return false;
        uint16_t ro = u16(range + i * 2), g;
        if (!ro) g = (uint16_t)(cp + u16(delta + i * 2));
        else {
            g = u16(range + i * 2 + ro + (cp - s) * 2);
            if (g) g = (uint16_t)(g + u16(delta + i * 2));
        }
        return g != 0;
    }
    return false;
}

// One UTF-8 character at s: its code point, *len its bytes (1 for a broken sequence, taken as U+FFFD)
static uint32_t next_cp(const unsigned char *s, int *len)
{
    if (s[0] < 0x80) { *len = 1; return s[0]; }
    int n = s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : s[0] >= 0xC0 ? 2 : 0;
    uint32_t cp = n == 4 ? s[0] & 0x07 : n == 3 ? s[0] & 0x0F : s[0] & 0x1F;
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) n = 0;
        else cp = cp << 6 | (s[i] & 0x3F);
        if (!n) break;
    }
    if (!n) { *len = 1; return 0xFFFD; }
    *len = n;
    return cp;
}

size_t textfit(const char *in, char *out, size_t n)
{
    size_t o = 0;
    bool dropped = false;                                // a character was removed: collapse the gaps it leaves
    if (!n) return 0;
    for (const unsigned char *s = (const unsigned char *)in; *s;) {
        int len;
        uint32_t cp = next_cp(s, &len);
        s += len;
        if (!(cp < 0x80 || (cp != 0xFFFD && textfit_has(cp)))) { dropped = true; continue; }
        if (dropped && cp == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
        if (o + len >= n) break;                         // whole characters only
        memcpy(out + o, s - len, len);
        o += len;
    }
    if (dropped) while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
    return o;
}
