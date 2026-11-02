/***************************************************************************
 * Y1 Wheel edition - font forge (see wheel_fontforge.h).
 *
 * Output: Rockbox "RB12" font, 4-bit alpha (the anti-aliased format
 * convttf writes): header, glyph bits, offsets, widths. Every glyph is a
 * cell of width x line height; 0 = full ink ... 15 = transparent, two
 * pixels per byte, low nibble first, row by row.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "wheel_fontforge.h"

/* --- stb_truetype without libm (the Android build doesn't link it) --- */
static int forge_ifloor(double x) { int i = (int)x; return (x < (double)i) ? i - 1 : i; }
static int forge_iceil(double x)  { int i = (int)x; return (x > (double)i) ? i + 1 : i; }
static double forge_fabs(double x) { return x < 0 ? -x : x; }
static double forge_sqrt(double x)
{
    if (x <= 0)
        return 0;
    double r = x > 1 ? x : 1;
    for (int i = 0; i < 24; i++)
        r = 0.5 * (r + x / r);
    return r;
}
static double forge_unused2(double a, double b) { (void)a; (void)b; return 0; }
static double forge_unused1(double a) { (void)a; return 0; }

#define STBTT_ifloor(x)    forge_ifloor(x)
#define STBTT_iceil(x)     forge_iceil(x)
#define STBTT_sqrt(x)      forge_sqrt(x)
#define STBTT_pow(x,y)     forge_unused2(x,y)  /* SDF only */
#define STBTT_fmod(x,y)    forge_unused2(x,y)  /* SDF only */
#define STBTT_cos(x)       forge_unused1(x)    /* SDF only */
#define STBTT_acos(x)      forge_unused1(x)    /* SDF only */
#define STBTT_fabs(x)      forge_fabs(x)
#define STBTT_malloc(x,u)  ((void)(u), malloc(x))
#define STBTT_free(x,u)    ((void)(u), free(x))
#define STBTT_assert(x)    ((void)0)
#define STBTT_strlen(x)    strlen(x)
#define STBTT_memcpy       memcpy
#define STBTT_memset       memset
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static int iround(float x) { return forge_ifloor(x + 0.5f); }

/* ------------------------------ sources ------------------------------ */

struct face {
    bool ok, outline;
    /* outline */
    stbtt_fontinfo tt;
    float scale;
    /* bitmap (.fnt) */
    const unsigned char *bits, *offs, *widths;
    bool off32;
    int maxwidth, height, ascent, depth;
    unsigned first, count, defchar;
    long nbits;
    /* metrics in output pixels */
    float cap, xh, asc, desc;
};

static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static long rd32(const unsigned char *p)
{
    return (long)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static bool is_fnt(const struct forge_src *s)
{
    return s->size > 36 && !memcmp(s->data, "RB12", 4);
}

static bool open_bitmap(struct face *f, const struct forge_src *s)
{
    const unsigned char *d = s->data;
    f->maxwidth = (short)rd16(d + 4);
    f->height   = (short)rd16(d + 6);
    f->ascent   = (short)rd16(d + 8);
    f->depth    = (short)rd16(d + 10);
    f->first    = rd32(d + 12);
    f->defchar  = rd32(d + 16);
    f->count    = rd32(d + 20);
    f->nbits    = rd32(d + 24);
    long noff   = rd32(d + 28);
    long nwid   = rd32(d + 32);
    if (f->height <= 0 || f->height > 255 || f->count == 0 || f->nbits < 0)
        return false;
    long p = 36;
    f->bits = d + p;
    p += f->nbits;
    f->off32 = f->nbits >= 0xFFDB;
    f->offs = NULL;
    if (noff)
    {
        p = (p + (f->off32 ? 3 : 1)) & ~(long)(f->off32 ? 3 : 1);
        f->offs = d + p;
        p += noff * (f->off32 ? 4 : 2);
    }
    f->widths = nwid ? d + p : NULL;
    p += nwid;
    if (p > s->size)
        return false;
    f->outline = false;
    return true;
}

static long bm_offset(const struct face *f, unsigned idx)
{
    if (f->offs)
        return f->off32 ? rd32(f->offs + 4 * idx) : (long)rd16(f->offs + 2 * idx);
    long per = f->depth ? (f->maxwidth * f->height + 1) / 2
                        : f->maxwidth * ((f->height + 7) / 8);
    return per * idx;
}

static int bm_width(const struct face *f, unsigned idx)
{
    return f->widths ? f->widths[idx] : f->maxwidth;
}

static bool bm_has(const struct face *f, unsigned cp)
{
    if (cp < f->first || cp >= f->first + f->count)
        return false;
    if (cp == f->defchar)
        return true;
    unsigned idx = cp - f->first;
    if (f->defchar >= f->first && f->defchar < f->first + f->count)
    {
        unsigned di = f->defchar - f->first;
        /* convbdf/convttf point missing glyphs at the default one */
        if (bm_offset(f, idx) == bm_offset(f, di) && bm_width(f, idx) == bm_width(f, di))
            return false;
    }
    return bm_width(f, idx) > 0 || cp == ' ';
}

/* alpha (0..255) of a pixel of a bitmap glyph */
static int bm_pixel(const struct face *f, const unsigned char *g, int w, int x, int y)
{
    if (f->depth)
    {
        long n = (long)y * w + x;
        int v = (g[n >> 1] >> ((n & 1) * 4)) & 15;
        return (15 - v) * 17;
    }
    return (g[(y >> 3) * w + x] >> (y & 7)) & 1 ? 255 : 0;
}

static bool open_face(struct face *f, const struct forge_src *s)
{
    memset(f, 0, sizeof(*f));
    if (!s || !s->data || s->size < 12)
        return false;
    if (is_fnt(s))
        f->ok = open_bitmap(f, s);
    else
    {
        int off = stbtt_GetFontOffsetForIndex(s->data, 0);
        f->ok = off >= 0 && stbtt_InitFont(&f->tt, s->data, off);
        f->outline = true;
    }
    return f->ok;
}

static int outline_glyph(const struct face *f, unsigned cp)
{
    return f->outline ? stbtt_FindGlyphIndex(&f->tt, (int)cp) : 0;
}

static bool face_has(const struct face *f, unsigned cp)
{
    return f->outline ? outline_glyph(f, cp) != 0 : bm_has(f, cp);
}

/* top ink row of a bitmap glyph, or -1 */
static int bm_top(const struct face *f, unsigned cp)
{
    if (!bm_has(f, cp))
        return -1;
    unsigned idx = cp - f->first;
    int w = bm_width(f, idx);
    const unsigned char *g = f->bits + bm_offset(f, idx);
    for (int y = 0; y < f->height; y++)
        for (int x = 0; x < w; x++)
            if (bm_pixel(f, g, w, x, y) > 127)
                return y;
    return -1;
}

static void set_metrics(struct face *f, int px)
{
    if (f->outline)
    {
        f->scale = stbtt_ScaleForMappingEmToPixels(&f->tt, (float)px);
        int a, d, gap, x0, y0, x1, y1;
        stbtt_GetFontVMetrics(&f->tt, &a, &d, &gap);
        f->asc = a * f->scale;
        f->desc = -d * f->scale;
        f->cap = stbtt_GetCodepointBox(&f->tt, 'H', &x0, &y0, &x1, &y1) ? y1 * f->scale : f->asc * 0.72f;
        f->xh = stbtt_GetCodepointBox(&f->tt, 'x', &x0, &y0, &x1, &y1) ? y1 * f->scale : f->cap * 0.72f;
    }
    else
    {
        int t = bm_top(f, 'H');
        f->cap = t >= 0 ? (float)(f->ascent - t) : f->ascent * 0.8f;
        t = bm_top(f, 'x');
        f->xh = t >= 0 ? (float)(f->ascent - t) : f->cap * 0.72f;
        f->asc = (float)f->ascent;
        f->desc = (float)(f->height - f->ascent);
    }
}

/* --------------------------- glyph drawing --------------------------- */

struct cell {
    unsigned char *a;   /* alpha 0..255 */
    int w, h;
};

static void blit_max(struct cell *c, const unsigned char *src, int w, int h, int stride, int dx, int dy)
{
    for (int y = 0; y < h; y++)
    {
        int ty = dy + y;
        if (ty < 0 || ty >= c->h)
            continue;
        for (int x = 0; x < w; x++)
        {
            int tx = dx + x;
            if (tx < 0 || tx >= c->w)
                continue;
            unsigned char v = src[y * stride + x];
            if (v > c->a[ty * c->w + tx])
                c->a[ty * c->w + tx] = v;
        }
    }
}

static bool draw_outline(const struct face *f, float scale, unsigned cp, float ox, int baseline, struct cell *c)
{
    int g = outline_glyph(f, cp);
    if (!g)
        return false;
    int ix = forge_ifloor(ox);
    float sx = ox - ix;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&f->tt, g, scale, scale, sx, 0, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0)
        return true; /* blank glyph (space) */
    unsigned char *tmp = malloc((size_t)w * h);
    if (!tmp)
        return false;
    stbtt_MakeGlyphBitmapSubpixel(&f->tt, tmp, w, h, w, scale, scale, sx, 0, g);
    blit_max(c, tmp, w, h, w, ix + x0, baseline + y0);
    free(tmp);
    return true;
}

static void draw_bitmap(const struct face *f, unsigned cp, int ox, int baseline, struct cell *c)
{
    unsigned idx = cp - f->first;
    int w = bm_width(f, idx);
    const unsigned char *g = f->bits + bm_offset(f, idx);
    int dy = baseline - f->ascent;
    for (int y = 0; y < f->height; y++)
    {
        int ty = dy + y;
        if (ty < 0 || ty >= c->h)
            continue;
        for (int x = 0; x < w; x++)
        {
            int tx = ox + x;
            if (tx < 0 || tx >= c->w)
                continue;
            int v = bm_pixel(f, g, w, x, y);
            if (v > c->a[ty * c->w + tx])
                c->a[ty * c->w + tx] = v;
        }
    }
}

/* thick polyline with round joins and caps, 4x4 supersampled */
static void draw_polyline(struct cell *c, const float *pt, int n, float r)
{
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
        {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                {
                    float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                    for (int i = 0; i + 1 < n; i++)
                    {
                        float ax = pt[2 * i], ay = pt[2 * i + 1];
                        float bx = pt[2 * i + 2], by = pt[2 * i + 3];
                        float vx = bx - ax, vy = by - ay;
                        float t = ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy);
                        if (t < 0) t = 0;
                        if (t > 1) t = 1;
                        float dx = ax + t * vx - px, dy = ay + t * vy - py;
                        if (dx * dx + dy * dy <= r * r)
                        {
                            hits++;
                            break;
                        }
                    }
                }
            int v = hits * 255 / 16;
            if (v > c->a[y * c->w + x])
                c->a[y * c->w + x] = (unsigned char)v;
        }
}

/* ------------------------------- output ------------------------------ */

struct out {
    unsigned char *bits;
    long len, cap;
};

static bool out_cell(struct out *o, const struct cell *c, bool crisp, int contrast)
{
    long need = ((long)c->w * c->h + 1) / 2;
    if (o->len + need > o->cap)
    {
        long nc = o->cap ? o->cap * 2 : 65536;
        while (nc < o->len + need)
            nc *= 2;
        unsigned char *nb = realloc(o->bits, nc);
        if (!nb)
            return false;
        o->bits = nb;
        o->cap = nc;
    }
    unsigned char *dst = o->bits + o->len;
    memset(dst, 0, need);
    long n = (long)c->w * c->h;
    for (long i = 0; i < n; i++)
    {
        int a = c->a[i];
        if (crisp)
            a = a >= 110 ? 255 : 0;
        else if (contrast > 0)
            a += a * (255 - a) * contrast / (255 * 4);
        if (a > 255) a = 255;
        int v = 15 - (a * 15 + 127) / 255;
        dst[i >> 1] |= (unsigned char)(v << ((i & 1) * 4));
    }
    o->len += need;
    return true;
}

static void put16(unsigned char *p, unsigned v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
static void put32(unsigned char *p, unsigned long v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

/* code points worth storing (the rest map to '?') */
static bool wanted(unsigned cp, const struct forge_params *p)
{
    if (cp > p->last)
        return false;
    if (cp >= 0x20 && cp <= 0x7e) return true;
    if (cp >= 0xa0 && cp <= 0x24f) return true;
    if (cp >= 0x2c6 && cp <= 0x2dd) return true;
    if (cp >= 0x370 && cp <= 0x4ff) return true;
    if (cp >= 0x2010 && cp <= 0x205e) return true;
    if (cp >= 0x20a0 && cp <= 0x20c0) return true;
    if (cp >= 0x2100 && cp <= 0x2122) return true;
    if (cp >= 0x2190 && cp <= 0x2193) return true;
    if (cp == 0x2212 || cp == 0x2713) return true;
    return false;
}

#define FIRST 0x20

int forge_build(const struct forge_src *src, const struct forge_src *fallback,
                const struct forge_params *p, forge_write_fn write, void *ctx,
                struct forge_stats *st)
{
    struct face main, fb;
    struct forge_stats dummy;
    if (!st)
        st = &dummy;
    memset(st, 0, sizeof(*st));
    if (!open_face(&main, src))
        return -1;
    bool have_fb = fallback && open_face(&fb, fallback) && fb.outline;

    int px = p->px > 0 ? p->px : 16;
    set_metrics(&main, px);
    if (!main.outline)
        px = iround(main.cap / 0.72f); /* nominal em of a bitmap font */
    float fb_scale = 0;
    if (have_fb)
    {
        set_metrics(&fb, px);
        fb_scale = fb.scale * (fb.cap > 0 ? main.cap / fb.cap : 1);
    }

    /* line height and baseline: centre the capitals in the band */
    int band = p->band > 0 ? p->band : iround(main.asc + main.desc);
    if (band > 255) band = 255;
    int base = iround(band / 2.0f + main.cap / 2.0f);
    int d = forge_iceil(main.desc);
    if (base + d > band) base = band - d;
    if (base < forge_iceil(main.cap)) base = forge_iceil(main.cap);
    if (base > band) base = band;

    unsigned last = p->last ? p->last : 0x2122;
    if (p->extras == FORGE_EXTRA_ROW && last < 0x2713)
        last = 0x2713;
    unsigned count = last - FIRST + 1;
    struct forge_params prm = *p;
    prm.last = last;

    /* tabular figures: the widest digit */
    int tab_w = 0;
    if (p->tabular)
        for (unsigned cp = '0'; cp <= '9'; cp++)
        {
            int w;
            if (main.outline)
            {
                int adv, lsb;
                stbtt_GetCodepointHMetrics(&main.tt, (int)cp, &adv, &lsb);
                w = iround(adv * main.scale);
            }
            else
                w = bm_has(&main, cp) ? bm_width(&main, cp - main.first) : 0;
            if (w > tab_w) tab_w = w;
        }

    unsigned long *offs = calloc(count, sizeof(*offs));
    unsigned char *widths = calloc(count, 1);
    unsigned char *seen = calloc(count, 1);
    struct out o = { NULL, 0, 0 };
    int maxw = 1;
    int ret = -2;
    if (!offs || !widths || !seen)
        goto done;

    float em = (float)px;
    for (unsigned cp = FIRST; cp <= last; cp++)
    {
        if (!wanted(cp, &prm))
            continue;
        bool extra = p->extras == FORGE_EXTRA_ROW && (cp == 0x203a || cp == 0x2713);
        int from = 0; /* 1 main, 2 fallback */
        if (!extra)
        {
            if (face_has(&main, cp))
                from = 1;
            else if (have_fb && outline_glyph(&fb, cp))
                from = 2;
            else
            {
                if (cp != '?')
                    st->missing++;
                continue;
            }
        }

        /* cell width */
        float adv;
        if (extra)
            adv = cp == 0x203a ? em * 0.55f * 0.55f + 6 + 2 : em * 0.55f * 1.05f + 6;
        else if (from == 2 || main.outline)
        {
            const struct face *f = from == 2 ? &fb : &main;
            int a, lsb;
            stbtt_GetGlyphHMetrics(&f->tt, outline_glyph(f, cp), &a, &lsb);
            adv = a * (from == 2 ? fb_scale : main.scale);
        }
        else
            adv = (float)bm_width(&main, cp - main.first);
        bool digit = cp >= '0' && cp <= '9' && p->tabular && tab_w > 0;
        float body = digit ? (float)tab_w : adv;
        int w = iround(body) + p->tracking + p->embolden;
        if (cp == ' ' && w < 2) w = 2;
        if (w < 1) w = 1;
        if (w > 255) w = 255;
        float ox = p->tracking / 2.0f + (digit ? (tab_w - adv) / 2.0f : 0);

        struct cell c;
        c.w = w;
        c.h = band;
        c.a = calloc((size_t)w * band, 1);
        if (!c.a)
            goto done;
        if (extra)
        {
            float cy = base - main.xh / 2.0f;
            float size = em * 0.55f;
            float r = em * 0.105f / 2;
            if (r < 0.8f) r = 0.8f;
            if (cp == 0x203a)
            {
                float x0 = 4 + 0.5f, x1 = x0 + size * 0.5f;
                float pts[6] = { x0, cy - size * 0.5f, x1, cy, x0, cy + size * 0.5f };
                draw_polyline(&c, pts, 3, r);
            }
            else
            {
                float x0 = 3 + 0.5f;
                float pts[6] = { x0, cy, x0 + size * 0.36f, cy + size * 0.36f,
                                 x0 + size * 0.98f, cy - size * 0.42f };
                draw_polyline(&c, pts, 3, r);
            }
        }
        else if (from == 2)
            draw_outline(&fb, fb_scale, cp, ox, base, &c);
        else if (main.outline)
            draw_outline(&main, main.scale, cp, ox, base, &c);
        else
            draw_bitmap(&main, cp, iround(ox), base, &c);

        for (int e = 0; e < p->embolden; e++) /* smear right by one pixel */
            for (int y = 0; y < c.h; y++)
                for (int x = c.w - 1; x > 0; x--)
                {
                    unsigned char *q = &c.a[y * c.w + x];
                    if (q[-1] > *q) *q = q[-1];
                }

        offs[cp - FIRST] = o.len;
        widths[cp - FIRST] = (unsigned char)w;
        seen[cp - FIRST] = 1;
        if (w > maxw) maxw = w;
        bool ok = out_cell(&o, &c, p->crisp, p->contrast);
        free(c.a);
        if (!ok)
            goto done;
        if (from == 2) st->fallback++; else st->glyphs++;
    }

    /* everything else shows '?' (or a space if even that is missing) */
    unsigned def = seen['?' - FIRST] ? '?' : ' ';
    if (!seen[def - FIRST])
    {
        struct cell c = { calloc(band, 1), 1, band };
        if (!c.a)
            goto done;
        offs[def - FIRST] = o.len;
        widths[def - FIRST] = 1;
        seen[def - FIRST] = 1;
        bool ok = out_cell(&o, &c, false, 0);
        free(c.a);
        if (!ok)
            goto done;
    }
    for (unsigned i = 0; i < count; i++)
        if (!seen[i])
        {
            offs[i] = offs[def - FIRST];
            widths[i] = widths[def - FIRST];
        }

    /* write */
    ret = -3;
    {
        bool off32 = o.len >= 0xFFDB;
        unsigned char hdr[36];
        memcpy(hdr, "RB12", 4);
        put16(hdr + 4, maxw);
        put16(hdr + 6, band);
        put16(hdr + 8, base);
        put16(hdr + 10, 1);          /* 4-bit anti-aliased */
        put32(hdr + 12, FIRST);
        put32(hdr + 16, def);
        put32(hdr + 20, count);
        put32(hdr + 24, o.len);
        put32(hdr + 28, count);
        put32(hdr + 32, count);
        if (!write(ctx, hdr, sizeof(hdr)) || (o.len && !write(ctx, o.bits, o.len)))
            goto done;
        long total = 36 + o.len;
        static const unsigned char zero[4] = { 0, 0, 0, 0 };
        long pad = off32 ? ((o.len + 3) & ~3L) - o.len : (o.len & 1);
        if (pad && !write(ctx, zero, pad))
            goto done;
        total += pad;
        unsigned char buf[256];
        int n = 0;
        int esz = off32 ? 4 : 2;
        for (unsigned i = 0; i < count; i++)
        {
            if (off32) put32(buf + n, offs[i]); else put16(buf + n, (unsigned)offs[i]);
            n += esz;
            if (n == (int)sizeof(buf))
            {
                if (!write(ctx, buf, n))
                    goto done;
                n = 0;
            }
        }
        if (n && !write(ctx, buf, n))
            goto done;
        total += (long)count * esz;
        if (!write(ctx, widths, count))
            goto done;
        total += count;
        st->bytes = total;
        st->height = band;
        st->ascent = base;
        st->maxwidth = maxw;
        st->last = last;
        ret = 0;
    }

done:
    free(o.bits);
    free(offs);
    free(widths);
    free(seen);
    return ret;
}

/* -------------------------------- info ------------------------------- */

static void name_string(const stbtt_fontinfo *f, int id, char *out, size_t len)
{
    int n = 0;
    const char *s = stbtt_GetFontNameString(f, &n, STBTT_PLATFORM_ID_MICROSOFT,
                                            STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, id);
    size_t o = 0;
    if (s)
        for (int i = 0; i + 1 < n && o + 1 < len; i += 2)
        {
            unsigned ch = ((unsigned char)s[i] << 8) | (unsigned char)s[i + 1];
            out[o++] = ch < 0x80 ? (char)ch : '?';
        }
    else
    {   /* Macintosh Roman names */
        s = stbtt_GetFontNameString(f, &n, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN,
                                    STBTT_MAC_LANG_ENGLISH, id);
        for (int i = 0; s && i < n && o + 1 < len; i++)
            out[o++] = (s[i] & 0x80) ? '?' : s[i];
    }
    out[o] = '\0';
}

static const unsigned blocks[FB_COUNT][2] = {
    { 0x20, 0x7e }, { 0xa0, 0xff }, { 0x100, 0x17f },
    { 0x391, 0x3c9 }, { 0x410, 0x44f }, { 0x2010, 0x2027 },
};

bool forge_info(const struct forge_src *src, struct forge_info *info)
{
    struct face f;
    memset(info, 0, sizeof(*info));
    if (!open_face(&f, src))
        return false;
    info->outline = f.outline;
    if (f.outline)
    {
        name_string(&f.tt, 16, info->family, sizeof(info->family));
        if (!info->family[0])
            name_string(&f.tt, 1, info->family, sizeof(info->family));
        name_string(&f.tt, 17, info->style, sizeof(info->style));
        if (!info->style[0])
            name_string(&f.tt, 2, info->style, sizeof(info->style));
        stbtt_uint32 os2 = stbtt__find_table(f.tt.data, f.tt.fontstart, "OS/2");
        info->weight = os2 ? ttUSHORT(f.tt.data + os2 + 4) : 400;
        info->variable = stbtt__find_table(f.tt.data, f.tt.fontstart, "fvar") != 0;
        set_metrics(&f, 1000);
        info->cap1000 = iround(f.cap);
    }
    else
    {
        info->height = f.height;
        info->ascent = f.ascent;
        info->weight = 400;
        set_metrics(&f, 0);
        info->cap = iround(f.cap);
    }
    if (info->weight < 100 || info->weight > 1000)
        info->weight = 400;
    for (int b = 0; b < FB_COUNT; b++)
        for (unsigned cp = blocks[b][0]; cp <= blocks[b][1]; cp++)
        {
            if (b == FB_GREEK && cp == 0x3a2) /* unassigned */
                continue;
            info->cover_of[b]++;
            if (face_has(&f, cp))
                info->cover[b]++;
        }
    return true;
}

bool forge_has_glyph(const struct forge_src *src, unsigned cp)
{
    struct face f;
    return open_face(&f, src) && face_has(&f, cp);
}
