/***************************************************************************
 * Y1 Wheel edition - colour palettes (see wheel_palette.h).
 *
 * Original palettes for the Wheel edition. Each one is a full set of
 * colours for the Wheel skins plus a selection gradient; the generated
 * theme is wps/Wheel-Pal.sbs / .wps (with the typography fonts too).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "action.h"
#include "settings.h"
#include "screens.h"
#include "viewport.h"
#include "splash.h"
#include "misc.h"
#include "font.h"
#include "string-extra.h"
#include "wheel_palette.h"
#include "wheel_screens.h"
#include "wheel_typo.h"
#include "wheel_gel.h"
#include "wheel_art.h"
#include "bmp.h"
#include "jpeg_load.h"
#include "audio.h"
#include "metadata.h"
#include "button.h"
#include "skin_engine/skin_engine.h"
#include "file.h"
#include "dir.h"
#include "rbpaths.h"

/* template colours, in enum wheel_pal_role order */
static const char * const tmpl[PR_COUNT] = {
    "f7f7f9", "141415", "3a3a3e", "141416", "6e6e73", "8e8e93", "3a3a3f", "ffffff",
    "f2f2f5", "d1d1d6", "c8c8ce", "cfcfd6", "fffffe", "dcdce1", "d6d6dc", "e6e6ea",
    "d4d4da",
};

struct pal_entry {
    struct wheel_palette p;
    unsigned swatch[2];        /* picker thumbnail gradient */
    unsigned dots[5];
    int ndots;
};

static const struct pal_entry pals[] = {
    { { "Wheel", LANG_WHEEL_PAL_DESC_0, false,
        { 0xf7f7f9, 0x141416, 0x3a3a3f, 0x141416, 0x6e6e73, 0x8e8e93, 0x3a3a3f, 0xffffff,
          0xf2f2f5, 0xd1d1d6, 0xc8c8ce, 0xcfcfd6, 0xffffff, 0xdcdce1, 0xd6d6dc, 0xe6e6ea,
          0xd4d4da }, 0x4a97fa, 0x1c63d8, 0xffffff },
      { 0x4a97fa, 0x1c63d8 }, { 0xffffff, 0xf2f2f5, 0x4a97fa, 0x141416 }, 4 },
    /* deep garnet bar, lilac-grey details, café crème pages */
    { { "Garnet Espresso", LANG_WHEEL_PAL_DESC_1, false,
        { 0x5c2239, 0xffffff, 0xf1dfe5, 0x2b1a20, 0x6f5660, 0x9a8790, 0x5c2239, 0xf8f2f1,
          0xefe5e4, 0xe0d1d4, 0x4a1a2d, 0xe6d7dc, 0xffffff, 0xe6d9de, 0xe6d9de, 0xeadfe0,
          0xdccdd1 }, 0x7e3452, 0x4f1b31, 0xffffff },
      { 0x2d1420, 0x7e3452 }, { 0x2b1a20, 0x7e3452, 0x9c8a9a, 0xefe5e4, 0x6b4a3e }, 5 },
    /* hot pink bar, peach selection, a touch of lilac */
    { { "Petal Pop", LANG_WHEEL_PAL_DESC_2, false,
        { 0xea4a7e, 0xffffff, 0xffe3ec, 0x3a1424, 0x8a5a6c, 0xa77f95, 0xb18cf0, 0xfff7f8,
          0xfdecf0, 0xf4d3dc, 0xd73c6e, 0xf9dbe5, 0xffffff, 0xf8dbe4, 0xf8dbe4, 0xf7e3e9,
          0xeecdd7 }, 0xf5895d, 0xe8417b, 0xffffff },
      { 0xec4c84, 0xf58a5f }, { 0xea4a7e, 0xf5895d, 0xbba4f2, 0xfff7f8 }, 4 },
    /* pine green bar, oat pages, golden honey selection */
    { { "Pine Nectar", LANG_WHEEL_PAL_DESC_3, false,
        { 0x1f3d2b, 0xf4f1e2, 0xdde6d6, 0x1d2a20, 0x5d6b5c, 0x8b9686, 0x2f5a3c, 0xf7f6ee,
          0xeef0e3, 0xd8ddc9, 0x15301f, 0xdde4d1, 0xffffff, 0xe1e5d4, 0xe1e5d4, 0xe7e9dd,
          0xd6dccb }, 0xe1aa3c, 0xb07a28, 0xffffff },
      { 0x1f3d2b, 0xe1aa3c }, { 0x8aa486, 0x1f3d2b, 0xa8783a, 0xe1aa3c }, 4 },
    /* after dark: ink violet with a lilac glow */
    { { "Midnight Iris", LANG_WHEEL_PAL_DESC_4, true,
        { 0x1a1824, 0xece8f5, 0xcfc8de, 0xece8f5, 0xa69fb8, 0x7f7892, 0xcfc8de, 0x222030,
          0x1d1b29, 0x302b40, 0x2e2a3e, 0x322d44, 0x8c84a3, 0x3a3550, 0x3a3550, 0x141219,
          0x1a1824 }, 0x5b4b8c, 0x9d89d9, 0xffffff },
      { 0x1a1824, 0x9d89d9 }, { 0x1a1824, 0x322d44, 0x8c84a3, 0xece8f5 }, 4 },
    /* warm cream, coral to tomato selection, sky-blue icons */
    { { "Citrus Sunset", LANG_WHEEL_PAL_DESC_5, false,
        { 0xfff7ec, 0x7c2414, 0xc63c2c, 0x3b1a10, 0x8a5a44, 0xb48a72, 0x3a9cc5, 0xfffaf2,
          0xfff0dd, 0xf3d6bb, 0xf4c9a8, 0xfde1c4, 0xffffff, 0xfbe1c8, 0xfbe1c8, 0xf8e6d2,
          0xefd4ba }, 0xf9a85c, 0xe0443a, 0xffffff },
      { 0xfcd27a, 0xe0443a }, { 0xfff3d8, 0xfcd27a, 0xf47b4f, 0xd0302e, 0x4fb3d8 }, 5 },
    /* blush bar, rosewood selection, vanilla pages, sage & dusty blue */
    { { "Rose Chai", LANG_WHEEL_PAL_DESC_6, false,
        { 0xf5d3dc, 0x5e2431, 0x8e3f4e, 0x3a1f24, 0x8a6168, 0x8e9cb0, 0xa8505f, 0xfffaf0,
          0xfbefe5, 0xecd2cf, 0xe9bcc6, 0xf3dbe0, 0xffffff, 0xefdcdc, 0xefdcdc, 0xf3e7e2,
          0xe8d4d2 }, 0xd98c9c, 0xa24b5c, 0xffffff },
      { 0xf2b8c8, 0x7e3442 }, { 0xfdf2dc, 0xf2b8c8, 0xa24b5c, 0xa9ba7a, 0x9aadc6 }, 5 },
};
#define N_FIXED ((int)ARRAYLEN(pals))
/* after the fixed ones: the colours of the cover, and the user's own */
#define PAL_COVER N_FIXED
#define PAL_MINE  (N_FIXED + 1)
#define N_PALS    (N_FIXED + 2)

/* the classic Wheel colours as schemes for the glossy skins: the template
 * itself and its dark variant (utils/wheel/make_dark.py); the selection
 * comes from the accent setting, so the page is not tinted */
static const struct wheel_palette classic[2] = {
    { "Wheel", LANG_WHEEL_PAL_DESC_0, false,
      { 0xf7f7f9, 0x141415, 0x3a3a3e, 0x141416, 0x6e6e73, 0x8e8e93, 0x3a3a3f, 0xffffff,
        0xf2f2f5, 0xd1d1d6, 0xc8c8ce, 0xcfcfd6, 0xfffffe, 0xdcdce1, 0xd6d6dc, 0xe6e6ea,
        0xd4d4da }, 0xffffff, 0xffffff, 0xffffff },
    { "Wheel", LANG_WHEEL_PAL_DESC_0, true,
      { 0x1c1c1f, 0xf2f2f5, 0xe5e5ea, 0xf2f2f5, 0xa1a1a8, 0x8e8e93, 0xe5e5ea, 0x0f0f11,
        0x18181b, 0x2c2c2f, 0x38383c, 0x2c2c31, 0x8e8e93, 0x3a3a3f, 0x3a3a3f, 0x050506,
        0x1f1f22 }, 0x0f0f11, 0x0f0f11, 0xffffff },
};

const struct wheel_palette *wheel_palette_classic(bool dark)
{
    return &classic[dark ? 1 : 0];
}

/* ------------------------- colour helpers (0xRRGGBB) ------------------------- */
#define BL(a, b, p) wheel_rgb_blend((a), (b), (p))

static int luma(unsigned c)
{
    return (((c >> 16) & 0xff) * 30 + ((c >> 8) & 0xff) * 59 + (c & 0xff) * 11) / 100;
}

/* h 0..359, s and v 0..100 */
static void to_hsv(unsigned c, int *h, int *s, int *v)
{
    int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    int mx = MAX(r, MAX(g, b)), mn = MIN(r, MIN(g, b)), d = mx - mn;
    *v = mx * 100 / 255;
    *s = mx ? d * 100 / mx : 0;
    if (!d)
        *h = 0;
    else if (mx == r)
        *h = (60 * (g - b) / d + 360) % 360;
    else if (mx == g)
        *h = 60 * (b - r) / d + 120;
    else
        *h = 60 * (r - g) / d + 240;
}

static unsigned from_hsv(int h, int s, int v)
{
    h = ((h % 360) + 360) % 360;
    s = MAX(0, MIN(100, s));
    v = MAX(0, MIN(100, v));
    int c = v * 255 / 100, m = c * (100 - s) / 100, x = h % 60;
    int up = m + (c - m) * x / 60, down = c - (c - m) * x / 60;
    int r, g, b;
    switch (h / 60)
    {
        case 0:  r = c; g = up; b = m; break;
        case 1:  r = down; g = c; b = m; break;
        case 2:  r = m; g = c; b = up; break;
        case 3:  r = m; g = down; b = c; break;
        case 4:  r = up; g = m; b = c; break;
        default: r = c; g = m; b = down; break;
    }
    return (unsigned)r << 16 | g << 8 | b;
}

void wheel_rgb_to_hsv(unsigned c, int *h, int *s, int *v) { to_hsv(c, h, s, v); }
unsigned wheel_hsv_to_rgb(int h, int s, int v) { return from_hsv(h, s, v); }

/* a whole palette from three colours: top bar, page and accent */
void wheel_palette_derive(struct wheel_palette *p, unsigned bar, unsigned bg, unsigned accent)
{
    const unsigned white = 0xffffff, black = 0, ink = 0x161419;
    bool dark = luma(bg) < 110;
    p->dark = dark;
    unsigned text = dark ? BL(0xf4f4f6, accent, 6) : BL(ink, accent, 12);
    unsigned bartext = luma(bar) < 150 ? BL(white, bar, 6) : BL(ink, bar, 20);
    p->c[PR_BAR] = bar;
    p->c[PR_BARTEXT] = bartext;
    p->c[PR_BARICONS] = BL(bartext, bar, 18);
    p->c[PR_TEXT] = text;
    p->c[PR_TEXT2] = BL(text, bg, 38);
    p->c[PR_TEXT3] = BL(text, bg, 52);
    p->c[PR_ICONS] = BL(text, accent, 40);
    p->c[PR_BG] = bg;
    p->c[PR_PANE] = BL(bg, accent, dark ? 7 : 5);
    p->c[PR_DIVIDER] = BL(bg, text, 14);
    p->c[PR_HAIRLINE] = luma(bar) < 150 ? BL(bar, black, 25) : BL(bar, ink, 20);
    p->c[PR_TILE] = BL(bg, accent, dark ? 22 : 16);
    p->c[PR_NOTE] = dark ? BL(p->c[PR_TILE], white, 45) : white;
    p->c[PR_TRACK] = BL(bg, text, 12);
    p->c[PR_TRACK2] = p->c[PR_TRACK];
    p->c[PR_SHADOW] = BL(bg, black, dark ? 45 : 6);
    p->c[PR_SHADOW2] = BL(bg, black, dark ? 28 : 13);
    /* the accent is also used for text (the artist): keep it readable */
    int ah, as, av;
    to_hsv(accent, &ah, &as, &av);
    if (!dark && luma(accent) > 150)
        accent = from_hsv(ah, as, av * 140 / MAX(1, luma(accent)) );
    if (dark && luma(accent) < 110)
        accent = from_hsv(ah, as, MIN(100, av + 25));
    p->sel_start = BL(accent, white, 22);
    p->sel_end = BL(accent, black, 10);
    p->sel_text = luma(p->sel_end) > 170 ? BL(ink, accent, 15) : white;
}

/* ------------------------- the two palettes that change ------------------------- */
static struct wheel_palette dyn[2];   /* 0: from the cover, 1: mine */
static bool cover_ready;
static char cover_path[MAX_PATH];

static bool cover_dark_now(void)
{
    if (global_settings.wheel_appearance == 1)
        return true;
    if (global_settings.wheel_appearance == 2)
        return wheel_is_night();
    return false;
}

/* the palette of a cover colour */
static void cover_from_seed(struct wheel_palette *p, unsigned seed, bool dark)
{
    int h, s, v;
    to_hsv(seed, &h, &s, &v);
    if (s < 12)
    {   /* black, white or grey covers: a quiet slate */
        h = 220;
        s = 12;
    }
    unsigned accent = from_hsv(h, MAX(45, MIN(80, s)), dark ? 82 : 62);
    unsigned bar = from_hsv(h, MAX(25, MIN(65, s * 7 / 10)), dark ? 17 : 30);
    unsigned bg = dark ? from_hsv(h, 25, 11) : from_hsv(h, 6, 99);
    wheel_palette_derive(p, bar, bg, accent);
}

/* the most colourful hue of a picture (falls back to its average) */
static unsigned dominant(const fb_data *px, int w, int h)
{
    long wsum[12] = { 0 }, rs[12] = { 0 }, gs[12] = { 0 }, bs[12] = { 0 };
    long ar = 0, ag = 0, ab = 0, n = 0;
    int step = MAX(1, w / 32);
    for (int y = 0; y < h; y += step)
        for (int x = 0; x < w; x += step)
        {
            unsigned c = FB_UNPACK_SCALAR_LCD(px[y * w + x]);
            int r = RGB_UNPACK_RED(c), g = RGB_UNPACK_GREEN(c), b = RGB_UNPACK_BLUE(c);
            ar += r; ag += g; ab += b; n++;
            int hh, ss, vv;
            to_hsv((unsigned)r << 16 | g << 8 | b, &hh, &ss, &vv);
            if (ss < 18 || vv < 15)
                continue;
            long wt = ss * vv;
            int bin = hh / 30;
            wsum[bin] += wt;
            rs[bin] += r * wt; gs[bin] += g * wt; bs[bin] += b * wt;
        }
    int best = 0;
    for (int i = 1; i < 12; i++)
        if (wsum[i] > wsum[best])
            best = i;
    if (n && wsum[best] > n * 300)
        return (unsigned)(rs[best] / wsum[best]) << 16 |
               (gs[best] / wsum[best]) << 8 | (bs[best] / wsum[best]);
    return n ? (unsigned)(ar / n) << 16 | (ag / n) << 8 | (ab / n) : 0x8a8f99;
}

static void mine_update(void)
{
    wheel_palette_derive(&dyn[1], global_settings.wheel_custom_bar & 0xffffff,
                         global_settings.wheel_custom_bg & 0xffffff,
                         global_settings.wheel_custom_accent & 0xffffff);
}

static void cover_init(void)
{
    if (!cover_ready)
    {
        cover_from_seed(&dyn[0], global_settings.wheel_cover_seed & 0xffffff, cover_dark_now());
        cover_ready = true;
    }
}

static struct wheel_palette *dynamic(int i)
{
    if (i == PAL_COVER)
    {
        cover_init();
        dyn[0].name = str(LANG_WHEEL_PAL_COVER);
        dyn[0].desc_id = LANG_WHEEL_PAL_COVER_DESC;
        return &dyn[0];
    }
    mine_update();
    dyn[1].name = str(LANG_WHEEL_PAL_MINE);
    dyn[1].desc_id = LANG_WHEEL_PAL_MINE_DESC;
    return &dyn[1];
}

bool wheel_palette_is_dynamic(void)
{
    return global_settings.wheel_palette == PAL_COVER;
}

/* the scheme being written by the skin generator (NULL: the active one) */
static const struct wheel_palette *gen_pal;

void wheel_palette_use(const struct wheel_palette *p)
{
    gen_pal = p;
}

int wheel_palette_count(void) { return N_PALS; }
int wheel_palette_cover_index(void) { return PAL_COVER; }
int wheel_palette_mine_index(void) { return PAL_MINE; }

const struct wheel_palette *wheel_palette_get(int i)
{
    if (i >= N_FIXED && i < N_PALS)
        return dynamic(i);
    return i >= 0 && i < N_FIXED ? &pals[i].p : NULL;
}

const struct wheel_palette *wheel_palette_active(void)
{
    int i = global_settings.wheel_palette;
    return i > 0 && i < N_PALS ? wheel_palette_get(i) : NULL;
}

/* a word of a skin line -> what the palette being generated puts there:
 * a colour for the fixed palettes; for the cover's, a colour role that is
 * set while the skin is shown ("wrNN", see wheel_gel.h) */
const char *wheel_palette_map_word(const char *w, int len)
{
    static char out[8];
    const struct wheel_palette *p = gen_pal ? gen_pal : wheel_palette_active();
    if (!p)
        return NULL;
    bool roles = p == &dyn[0];
    if (len == 6)
        for (int r = 0; r < PR_COUNT; r++)
            if (!strncasecmp(w, tmpl[r], 6))
            {
                if (roles)
                    snprintf(out, sizeof(out), "wr%02d", r);
                else
                    snprintf(out, sizeof(out), "%06x", p->c[r]);
                return out;
            }
    if (roles)
    {
        static const struct { const char *name; int role; } names[] = {
            { "accent", WHEEL_ROLE_ACCENT }, { "accent2", WHEEL_ROLE_ACCENT2 },
            { "accenttext", WHEEL_ROLE_ACCENTTX },
        };
        for (unsigned i = 0; i < ARRAYLEN(names); i++)
            if ((int)strlen(names[i].name) == len && !strncmp(w, names[i].name, len))
            {
                snprintf(out, sizeof(out), "wr%02d", names[i].role);
                return out;
            }
    }
    return NULL;
}

const char *wheel_palette_map(const char *hex6)
{
    return wheel_palette_map_word(hex6, 6);
}

static unsigned rgb(unsigned c)
{
    return LCD_RGBPACK((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
}

void wheel_palette_bar(unsigned *bg, unsigned *fg)
{
    const struct wheel_palette *p = wheel_palette_active();
    if (p)
    {
        *bg = rgb(p->c[PR_BAR]);
        *fg = rgb(p->c[PR_BARTEXT]);
    }
    else if (strstr((char *)global_settings.sbs_file, "Dark"))
    {
        *bg = rgb(0x1c1c1f);
        *fg = rgb(0xf2f2f5);
    }
    else
    {
        *bg = rgb(0xf7f7f9);
        *fg = rgb(0x141416);
    }
}

/* the colour roles of the skins that follow the cover */
static void set_roles(const struct wheel_palette *p)
{
    for (int r = 0; r < PR_COUNT; r++)
        wheel_role_set(r, rgb(p->c[r]));
    wheel_role_set(WHEEL_ROLE_ACCENT, rgb(p->sel_end));
    wheel_role_set(WHEEL_ROLE_ACCENT2, rgb(p->sel_start));
    wheel_role_set(WHEEL_ROLE_ACCENTTX, rgb(p->sel_text));
}

/* the colours Rockbox uses for lists and screens drawn in C */
bool wheel_palette_patch_colours(void)
{
    const struct wheel_palette *p = wheel_palette_active();
    if (!p)
        return false;
    if (p == &dyn[0])
        set_roles(p);
    bool changed = false;
    int *s[5] = { &global_settings.fg_color, &global_settings.bg_color,
                  &global_settings.lss_color, &global_settings.lse_color,
                  &global_settings.lst_color };
    unsigned v[5] = { p->c[PR_TEXT], p->c[PR_BG], p->sel_start, p->sel_end, p->sel_text };
    for (int i = 0; i < 5; i++)
        if (*s[i] != (int)rgb(v[i]))
        {
            *s[i] = rgb(v[i]);
            changed = true;
        }
    return changed;
}

/* ------------------------- the user's background image ------------------------- */
/* kept small and blurred (or at screen size when sharp), as 0xRRGGBB */
static struct {
    char path[MAX_PATH];
    bool blur, ok;
    int w, h;
    unsigned *px;
} wall;

#define PROBE_SIZE (96 * 1024)

static bool load_picture(const char *path, struct bitmap *bm, void **mem)
{
    const char *ext = strrchr(path, '.');
    bool jpeg = ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg"));
    int fmt = FORMAT_NATIVE | FORMAT_RESIZE;
    void *probe = malloc(PROBE_SIZE);
    if (!probe)
        return false;
    bm->data = probe;
    int need = jpeg ? read_jpeg_file(path, bm, PROBE_SIZE, fmt | FORMAT_RETURN_SIZE, NULL)
                    : read_bmp_file(path, bm, PROBE_SIZE, fmt | FORMAT_RETURN_SIZE, NULL);
    free(probe);
    if (need <= 0)
        return false;
    need += 64 * 1024;
    *mem = malloc(need);
    if (!*mem)
        return false;
    bm->data = *mem;
    int got = jpeg ? read_jpeg_file(path, bm, need, fmt, NULL)
                   : read_bmp_file(path, bm, need, fmt, NULL);
    return got > 0;
}

/* the picture cropped to fill tw x th (keeping its proportions) */
static unsigned *load_fill(const char *path, int tw, int th)
{
    struct bitmap bm;
    memset(&bm, 0, sizeof(bm));
    const char *ext = strrchr(path, '.');
    bool jpeg = ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg"));
    /* its own size first */
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    close(fd);
    /* (the JPEG decoder keeps its state in the buffer even for this) */
    void *probe = malloc(PROBE_SIZE);
    if (!probe)
        return NULL;
    bm.data = probe;
    int rs = jpeg ? read_jpeg_file(path, &bm, PROBE_SIZE, FORMAT_NATIVE | FORMAT_RETURN_SIZE, NULL)
              : read_bmp_file(path, &bm, PROBE_SIZE, FORMAT_NATIVE | FORMAT_RETURN_SIZE, NULL);
    free(probe);
    if (rs <= 0 ||
        bm.width <= 0 || bm.height <= 0)
        return NULL;
    int iw = bm.width, ih = bm.height;
    /* scale so it covers tw x th */
    int fw = tw, fh = th;
    if ((long)iw * th > (long)ih * tw)
        fw = (int)((long)iw * th / ih);
    else
        fh = (int)((long)ih * tw / iw);
    fw = MAX(fw, tw);
    fh = MAX(fh, th);
    bm.width = fw;
    bm.height = fh;
    void *mem = NULL;
    unsigned *out = NULL;
    if (load_picture(path, &bm, &mem) && bm.width >= tw && bm.height >= th)
    {
        out = malloc(tw * th * sizeof(unsigned));
        if (out)
        {
            fb_data *px = (fb_data *)bm.data;
            int ox = (bm.width - tw) / 2, oy = (bm.height - th) / 2;
            for (int y = 0; y < th; y++)
                for (int x = 0; x < tw; x++)
                {
                    unsigned c = FB_UNPACK_SCALAR_LCD(px[(y + oy) * bm.width + x + ox]);
                    out[y * tw + x] = (unsigned)RGB_UNPACK_RED(c) << 16 |
                                      RGB_UNPACK_GREEN(c) << 8 | RGB_UNPACK_BLUE(c);
                }
        }
    }
    free(mem);
    return out;
}

/* box blur, a few passes, channel by channel */
static void blur(unsigned *px, int w, int h, int r, int passes)
{
    unsigned *tmp = malloc(MAX(w, h) * sizeof(unsigned));
    if (!tmp)
        return;
    while (passes--)
    {
        for (int dir = 0; dir < 2; dir++)
        {
            int lines = dir ? w : h, len = dir ? h : w;
            for (int l = 0; l < lines; l++)
            {
                for (int i = 0; i < len; i++)
                {
                    int sr = 0, sg = 0, sb = 0, n = 0;
                    for (int k = -r; k <= r; k++)
                    {
                        int j = MAX(0, MIN(len - 1, i + k));
                        unsigned c = dir ? px[j * w + l] : px[l * w + j];
                        sr += c >> 16; sg += (c >> 8) & 0xff; sb += c & 0xff; n++;
                    }
                    tmp[i] = (unsigned)(sr / n) << 16 | (sg / n) << 8 | (sb / n);
                }
                for (int i = 0; i < len; i++)
                {
                    if (dir) px[i * w + l] = tmp[i];
                    else     px[l * w + i] = tmp[i];
                }
            }
        }
    }
    free(tmp);
}

static bool wall_prepare(void)
{
    const char *path = global_settings.wheel_wallpaper;
    if (!path[0])
        return false;
    if (wall.ok && !strcmp(wall.path, path) && wall.blur == global_settings.wheel_wall_blur)
        return true;
    free(wall.px);
    wall.px = NULL;
    wall.ok = false;
    strlcpy(wall.path, path, sizeof(wall.path));
    wall.blur = global_settings.wheel_wall_blur;
    wall.w = wall.blur ? LCD_WIDTH / 4 : LCD_WIDTH;
    wall.h = wall.blur ? LCD_HEIGHT / 4 : LCD_HEIGHT;
    wall.px = load_fill(path, wall.w, wall.h);
    if (wall.px && wall.blur)
        blur(wall.px, wall.w, wall.h, 2, 3);
    wall.ok = wall.px != NULL;
    return wall.ok;
}

bool wheel_wallpaper_check(const char *path)
{
    char old[MAX_PATH];
    strlcpy(old, global_settings.wheel_wallpaper, sizeof(old));
    strlcpy(global_settings.wheel_wallpaper, path, sizeof(global_settings.wheel_wallpaper));
    bool ok = wall_prepare();
    if (!ok)
        strlcpy(global_settings.wheel_wallpaper, old, sizeof(global_settings.wheel_wallpaper));
    return ok;
}

/* bilinear sample of the cached picture at a screen position */
static unsigned wall_at(int x, int y)
{
    if (wall.w == LCD_WIDTH && wall.h == LCD_HEIGHT)
        return wall.px[y * wall.w + x];
    int fx = x * (wall.w - 1) * 256 / (LCD_WIDTH - 1);
    int fy = y * (wall.h - 1) * 256 / (LCD_HEIGHT - 1);
    int x0 = fx >> 8, y0 = fy >> 8, ax = fx & 255, ay = fy & 255;
    int x1 = MIN(x0 + 1, wall.w - 1), y1 = MIN(y0 + 1, wall.h - 1);
    unsigned c00 = wall.px[y0 * wall.w + x0], c01 = wall.px[y0 * wall.w + x1];
    unsigned c10 = wall.px[y1 * wall.w + x0], c11 = wall.px[y1 * wall.w + x1];
    unsigned top = BL(c00, c01, ax * 100 / 256), bot = BL(c10, c11, ax * 100 / 256);
    return BL(top, bot, ay * 100 / 256);
}

/* ------------------------- backdrops ------------------------- */
#define BAR_H 38

static int isqrt(int v);

/* the vinyl record of the "Vinyl" Now Playing layout */
static unsigned vinyl_at(int x, int y, unsigned under)
{
    int dx = x - VINYL_CX, dy = y - VINYL_CY;
    int r2 = dx * dx + dy * dy;
    if (r2 > (VINYL_R + 1) * (VINYL_R + 1))
        return under;
    int r = isqrt(r2);
    unsigned c = 0x141416;
    if (r > VINYL_LABEL_R + 8 && r < VINYL_R - 4 && (r & 3) == 0)
        c = 0x1d1d20;                         /* grooves */
    if (r >= VINYL_R - 1)
        c = 0x2c2c30;                         /* rim */
    /* light glancing off it along a diagonal */
    int band = dx - dy < 0 ? dy - dx : dx - dy;
    int width = VINYL_R * 35 / 100;
    if (band < width && r > VINYL_LABEL_R)
        c = BL(c, 0xffffff, (width - band) * 16 * r / (width * VINYL_R));
    if (r >= VINYL_R)                         /* soft edge */
        c = BL(c, under, 50);
    return c;
}

/* one line of a backdrop (0xRRGGBB per pixel) */
static void backdrop_line(const struct wheel_palette *p, bool aero, bool vinyl, bool pic,
                          int y, unsigned *out)
{
    unsigned bar = p->c[PR_BAR], bg = p->c[PR_BG], c;
    if (y < BAR_H - 1)
        c = aero ? wheel_rgb_gel(bar, bar, y, BAR_H - 1,
                                 wheel_rgb_strength_for(bar, p->c[PR_BARTEXT]))
                 : bar;
    else if (y == BAR_H - 1)
        c = p->c[PR_HAIRLINE];
    else
    {
        /* (dark pages stay flat: so close to black the steps of a 16-bit
         * screen would show as bands) */
        int t = (y - BAR_H) * 100 / (LCD_HEIGHT - BAR_H);
        c = p->dark || !aero ? bg :
            BL(BL(bg, 0xffffff, 45), BL(bg, p->sel_end, 7), t);
        if (aero && y - BAR_H < 6) /* the bar floats a little above the page */
            c = BL(c, 0, (6 - (y - BAR_H)) * (p->dark ? 7 : 2));
    }
    static const int show[3] = { 22, 36, 52 };
    int k = show[MAX(0, MIN(2, global_settings.wheel_wall_strength))];
    for (int x = 0; x < LCD_WIDTH; x++)
    {
        unsigned px = c;
        if (pic && y != BAR_H - 1)
            px = BL(c, wall_at(x, y), y < BAR_H ? 16 : k);  /* glass bar over it */
        if (vinyl && y >= BAR_H)
            px = vinyl_at(x, y, px);
        out[x] = px;
    }
}

static unsigned fnv(unsigned h, unsigned v)
{
    return (h ^ v) * 16777619u;
}

static unsigned backdrop_hash(const struct wheel_palette *p, bool aero, bool vinyl, bool pic)
{
    unsigned h = 2166136261u;
    for (int r = 0; r < PR_COUNT; r++)
        h = fnv(h, p->c[r]);
    h = fnv(h, p->sel_end);
    h = fnv(h, aero | vinyl << 1 | pic << 2 | p->dark << 3);
    h = fnv(h, wheel_gloss_pct());
    h = fnv(h, LCD_WIDTH << 16 | LCD_HEIGHT);
    h = fnv(h, 4); /* look version */
    if (pic)
    {
        for (const char *c = wall.path; *c; c++)
            h = fnv(h, (unsigned char)*c);
        h = fnv(h, wall.blur | global_settings.wheel_wall_strength << 1);
    }
    return h;
}

static void put_le(unsigned char *b, unsigned v, int n)
{
    for (int i = 0; i < n; i++)
        b[i] = (v >> (8 * i)) & 0xff;
}

/* writes wps/<skin>/<aero|vinyl>-<hash>.bmp for the scheme being generated
 * (once) and removes the older ones; name gets the file name */
bool wheel_palette_backdrop(const char *skin, bool aero, bool vinyl, char *name, size_t len)
{
    const struct wheel_palette *p = gen_pal ? gen_pal : wheel_palette_active();
    if (!p)
        return false;
    bool pic = wall_prepare();
    const char *prefix = vinyl ? "vinyl-" : "aero-";
    snprintf(name, len, "%s%08x.bmp", prefix, backdrop_hash(p, aero, vinyl, pic));

    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, sizeof(dir), WPS_DIR "/%s", skin);
    mkdir(dir);
    DIR *dp = opendir(dir);
    if (dp)
    {
        struct dirent *e;
        while ((e = readdir(dp)))
            if (!strncmp((char *)e->d_name, prefix, strlen(prefix)) &&
                strcmp((char *)e->d_name, name))
            {
                snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
                remove(path);
            }
        closedir(dp);
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int fd = open(path, O_RDONLY);
    if (fd >= 0)
    {
        close(fd);
        return true;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    const int stride = (LCD_WIDTH * 3 + 3) & ~3;
    unsigned char hdr[54] = { 'B', 'M' };
    put_le(hdr + 2, 54 + stride * LCD_HEIGHT, 4);
    put_le(hdr + 10, 54, 4);
    put_le(hdr + 14, 40, 4);
    put_le(hdr + 18, LCD_WIDTH, 4);
    put_le(hdr + 22, LCD_HEIGHT, 4);  /* bottom-up */
    put_le(hdr + 26, 1, 2);
    put_le(hdr + 28, 24, 2);
    bool ok = write(fd, hdr, sizeof(hdr)) == sizeof(hdr);
    static unsigned char row[(LCD_WIDTH * 3 + 3) & ~3];
    static unsigned line[LCD_WIDTH];
    for (int y = LCD_HEIGHT - 1; y >= 0 && ok; y--)
    {
        backdrop_line(p, aero, vinyl, pic, y, line);
        for (int x = 0; x < LCD_WIDTH; x++)
        {
            row[x * 3] = line[x] & 0xff;
            row[x * 3 + 1] = (line[x] >> 8) & 0xff;
            row[x * 3 + 2] = (line[x] >> 16) & 0xff;
        }
        ok = write(fd, row, stride) == stride;
    }
    close(fd);
    if (!ok)
        remove(path);
    return ok;
}

/* ------------------------- the palette that follows the cover ------------------------- */
WHEEL_ART_INIT(cover_art, 64);

/* the loaded backdrops of the cover palette's skins redrawn in its current
 * colours, if they aren't already */
static bool refresh_backdrops(const struct wheel_palette *p)
{
    bool done = false;
    int iter = 0;
    const char *name;
    fb_data *buf;
    static unsigned line[LCD_WIDTH];
    bool pic = global_settings.wheel_wallpaper[0] && wall_prepare();
    while ((buf = skin_backdrop_next("/Wheel-Pal/", &iter, &name)))
    {
        bool vinyl = strstr(name, "/vinyl-") != NULL;
        bool aero = global_settings.wheel_aero;
        /* already in these colours? (a pixel of the bar and of the page) */
        static const int probe_y[2] = { 10, LCD_HEIGHT - 20 };
        bool same = true;
        for (int i = 0; i < 2 && same; i++)
        {
            backdrop_line(p, aero, vinyl, pic, probe_y[i], line);
            int x = LCD_WIDTH - 3;
            same = buf[probe_y[i] * LCD_WIDTH + x] == FB_SCALARPACK(rgb(line[x]));
        }
        if (same)
            continue;
        for (int y = 0; y < LCD_HEIGHT; y++)
        {
            backdrop_line(p, aero, vinyl, pic, y, line);
            fb_data *dst = buf + y * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; x++)
                dst[x] = FB_SCALARPACK(rgb(line[x]));
        }
        done = true;
    }
    return done;
}

bool wheel_palette_poll(void)
{
    if (global_settings.wheel_palette != PAL_COVER ||
        strcmp((char *)global_settings.sbs_file, "Wheel-Pal"))
        return false;
    bool changed = false;
    struct mp3entry *id3 = (audio_status() & AUDIO_STATUS_PLAY) ? audio_current_track() : NULL;
    const char *path = id3 ? id3->path : "";
    bool dark = cover_dark_now();
    if (!cover_ready || strcmp(path, cover_path) || dark != dyn[0].dark)
    {
        strlcpy(cover_path, path, sizeof(cover_path));
        unsigned seed = global_settings.wheel_cover_seed & 0xffffff;
        if (path[0] && wheel_art_load(&cover_art, path))
            seed = dominant((fb_data *)cover_art.bm.data, cover_art.bm.width,
                            cover_art.bm.height);
        global_settings.wheel_cover_seed = seed;
        cover_from_seed(&dyn[0], seed, dark);
        cover_ready = true;
        wheel_palette_patch_colours();
        changed = true;
    }
    else
        set_roles(&dyn[0]);
    if (refresh_backdrops(&dyn[0]))
        changed = true;
    if (changed)
    {
        if (get_current_activity() == ACTIVITY_WPS)
        {   /* (a full update only redraws the viewports) */
            struct viewport *vp = screens[SCREEN_MAIN].set_viewport(NULL);
            screens[SCREEN_MAIN].clear_viewport();
            screens[SCREEN_MAIN].set_viewport(vp);
        }
        skin_request_full_update(CUSTOM_STATUSBAR);
        skin_request_full_update(WPS);
        button_queue_post(BUTTON_REDRAW, 0);
    }
    return changed;
}

void wheel_palette_set(int i)
{
    if (i < 0 || i >= N_PALS)
        return;
    splash(0, ID2P(LANG_WAIT));
    global_settings.wheel_palette = i;
    if (i > 0 && !typo_generate_palette())
    {
        global_settings.wheel_palette = 0;
        splash(HZ * 2, ID2P(LANG_WHEEL_TYPO_FAILED));
    }
    settings_save();
    wheel_appearance_apply(true);
}

/* ------------------------------- picker ------------------------------- */

static unsigned mixc(unsigned a, unsigned b, int pct)   /* LCD colours */
{
    return LCD_RGBPACK((RGB_UNPACK_RED(a) * pct + RGB_UNPACK_RED(b) * (100 - pct)) / 100,
                       (RGB_UNPACK_GREEN(a) * pct + RGB_UNPACK_GREEN(b) * (100 - pct)) / 100,
                       (RGB_UNPACK_BLUE(a) * pct + RGB_UNPACK_BLUE(b) * (100 - pct)) / 100);
}

static int isqrt(int v)
{
    if (v <= 0)
        return 0;
    unsigned x = v, r = 0, bit = 1u << 30;
    while (bit > x)
        bit >>= 2;
    while (bit)
    {
        if (x >= r + bit)
        {
            x -= r + bit;
            r = (r >> 1) + bit;
        }
        else
            r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* horizontal span of a rounded rectangle's row (corner radius r) */
static int round_inset(int y, int h, int r)
{
    int dy = y < r ? r - 1 - y : (y >= h - r ? y - (h - r) : -1);
    if (dy < 0)
        return 0;
    return r - isqrt(r * r - dy * dy);
}

/* rounded rectangle, vertical gradient top -> bottom */
static void round_rect(struct screen *d, int x, int y, int w, int h, int r,
                       unsigned top, unsigned bottom, bool gloss)
{
    for (int k = 0; k < h; k++)
    {
        int in = round_inset(k, h, r);
        unsigned c = mixc(bottom, top, h > 1 ? k * 100 / (h - 1) : 0);
        if (gloss) /* the glossy finish: a gel in the gradient's colours */
            c = wheel_gel_row(c, c, k, h, 80);
        d->set_foreground(c);
        d->hline(x + in, x + w - 1 - in, y + k);
    }
}

static void disc(struct screen *d, int cx, int cy, int r, unsigned col)
{
    d->set_foreground(col);
    for (int dy = -r; dy <= r; dy++)
    {
        int w = isqrt(r * r - dy * dy);
        d->hline(cx - w, cx + w, cy + dy);
    }
}

#define ROW_H 76

/* text cut with an ellipsis to fit maxw pixels */
static void put_fit(struct screen *d, int x, int y, int maxw, int font, const char *text)
{
    char buf[128];
    int w, h;
    strlcpy(buf, text, sizeof(buf));
    font_getstringsize((unsigned char *)buf, &w, &h, font);
    if (w > maxw)
    {
        size_t n = strlen(buf);
        while (n > 0)
        {
            do n--; while (n > 0 && (buf[n] & 0xc0) == 0x80); /* whole UTF-8 chars */
            strcpy(buf + n, "…");
            font_getstringsize((unsigned char *)buf, &w, &h, font);
            if (w <= maxw)
                break;
        }
    }
    d->putsxy(x, y, (unsigned char *)buf);
}

static void draw_picker(int sel, int top)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    unsigned fg = global_settings.fg_color, bg = global_settings.bg_color;
    unsigned barfg;
    bool aero = global_settings.wheel_aero;
    d->set_drawmode(DRMODE_SOLID);
    d->set_background(bg);
    d->clear_viewport();
    wheel_top_bar(d, LCD_WIDTH, 38, &barfg);
    d->set_drawmode(DRMODE_FG);
    vp.font = typo_font(TU_DISPLAY_MENU);
    d->set_foreground(barfg);
    d->putsxy(16, 2, (unsigned char *)str(LANG_WHEEL_PALETTE_PICK));

    int cur = global_settings.wheel_palette;
    for (int i = top, y = 44; i < N_PALS && y < LCD_HEIGHT; i++, y += ROW_H)
    {
        struct pal_entry dynamic_entry;
        const struct pal_entry *e = &pals[MIN(i, N_FIXED - 1)];
        if (i >= N_FIXED)
        {   /* the cover's and "My palette": thumbnails from their colours */
            const struct wheel_palette *p = wheel_palette_get(i);
            dynamic_entry.p = *p;
            dynamic_entry.swatch[0] = p->c[PR_BAR];
            dynamic_entry.swatch[1] = p->sel_start;
            dynamic_entry.dots[0] = p->c[PR_BG];
            dynamic_entry.dots[1] = p->sel_end;
            dynamic_entry.dots[2] = p->c[PR_TEXT];
            dynamic_entry.ndots = 3;
            e = &dynamic_entry;
        }
        d->set_drawmode(DRMODE_SOLID);
        if (i == sel)
        {   /* card with an accent outline */
            round_rect(d, 8, y, LCD_WIDTH - 16, ROW_H - 6, 14,
                       global_settings.lse_color, global_settings.lse_color, false);
            round_rect(d, 10, y + 2, LCD_WIDTH - 20, ROW_H - 10, 12,
                       mixc(global_settings.lss_color, bg, 14),
                       mixc(global_settings.lss_color, bg, 14), false);
        }
        /* thumbnail: gradient + colour dots */
        round_rect(d, 20, y + 9, 76, 52, 11, rgb(e->swatch[0]), rgb(e->swatch[1]), aero);
        for (int k = 0; k < e->ndots; k++)
        {
            int cx = 29 + k * 14, cy = y + 50;
            disc(d, cx, cy, 6, rgb(0xffffff));
            disc(d, cx, cy, 5, rgb(e->dots[k]));
        }
        if (i == cur)
        {   /* the one in use: a tick badge on the thumbnail */
            disc(d, 92, y + 13, 9, rgb(0xffffff));
            disc(d, 92, y + 13, 7, global_settings.lse_color);
            disc(d, 92, y + 13, 3, global_settings.lst_color);
        }
        d->set_drawmode(DRMODE_FG);
        int textw = LCD_WIDTH - 110 - 18;
        vp.font = typo_font(TU_DISPLAY_MENU);
        d->set_foreground(fg);
        put_fit(d, 110, y + 6, textw, vp.font, e->p.name);
        vp.font = typo_font(TU_META);
        d->set_foreground(mixc(fg, bg, 60));
        put_fit(d, 110, y + 40, textw, vp.font, str(e->p.desc_id));
    }
    d->update_viewport();
    d->set_viewport(last);
}

/* --------------------------- "My palette" editor --------------------------- */

/* hue / saturation / lightness bars of the colour being edited */
static void draw_colour_picker(const char *title, int h, int sv[2], int row)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    unsigned fg = global_settings.fg_color, bg = global_settings.bg_color, barfg;
    d->set_drawmode(DRMODE_SOLID);
    d->set_background(bg);
    d->clear_viewport();
    wheel_top_bar(d, LCD_WIDTH, 38, &barfg);
    d->set_drawmode(DRMODE_FG);
    vp.font = typo_font(TU_DISPLAY_MENU);
    d->set_foreground(barfg);
    d->putsxy(16, 2, (unsigned char *)title);

    unsigned now = from_hsv(h, sv[0], sv[1]);
    d->set_drawmode(DRMODE_SOLID);
    round_rect(d, 16, 50, LCD_WIDTH - 32, 70, 14, rgb(now), rgb(now),
               global_settings.wheel_aero);
    static const int labels[3] = { LANG_WHEEL_HUE, LANG_WHEEL_SATURATION, LANG_WHEEL_LIGHTNESS };
    int vals[3] = { h, sv[0], sv[1] };
    for (int r = 0; r < 3; r++)
    {
        int y = 132 + r * 62, bx = 24, bw = LCD_WIDTH - 48;
        d->set_drawmode(DRMODE_FG);
        vp.font = typo_font(TU_META);
        d->set_foreground(r == row ? fg : mixc(fg, bg, 55));
        d->putsxy(bx, y, (unsigned char *)str(labels[r]));
        d->set_drawmode(DRMODE_SOLID);
        for (int x = 0; x < bw; x++)
        {   /* the bar shows what each position would give */
            int v = r == 0 ? x * 359 / (bw - 1) : x * 100 / (bw - 1);
            unsigned c = r == 0 ? from_hsv(v, MAX(sv[0], 55), MAX(sv[1], 70))
                       : r == 1 ? from_hsv(h, v, sv[1]) : from_hsv(h, sv[0], v);
            d->set_foreground(rgb(c));
            d->vline(bx + x, y + 26, y + 26 + (r == row ? 18 : 12));
        }
        int mx = bx + vals[r] * (bw - 1) / (r == 0 ? 359 : 100);
        disc(d, mx, y + 26 + (r == row ? 9 : 6), r == row ? 10 : 7, rgb(0xffffff));
        disc(d, mx, y + 26 + (r == row ? 9 : 6), r == row ? 7 : 5, rgb(now));
    }
    d->set_drawmode(DRMODE_FG);
    vp.font = typo_font(TU_META);
    d->set_foreground(mixc(fg, bg, 55));
    put_fit(d, 16, LCD_HEIGHT - 26, LCD_WIDTH - 32, vp.font, str(LANG_WHEEL_PICK_HINT));
    d->update_viewport();
    d->set_viewport(last);
}

/* edits *colour (0xRRGGBB) with the wheel; Back keeps the change */
static void colour_picker(const char *title, int *colour)
{
    int h, sv[2], row = 0;
    to_hsv(*colour & 0xffffff, &h, &sv[0], &sv[1]);
    while (1)
    {
        draw_colour_picker(title, h, sv, row);
        int a = get_action(CONTEXT_STD, TIMEOUT_BLOCK);
        int step = (a == ACTION_STD_NEXT || a == ACTION_STD_NEXTREPEAT) ? 1 :
                   (a == ACTION_STD_PREV || a == ACTION_STD_PREVREPEAT) ? -1 : 0;
        if (step)
        {
            if (row == 0)
                h = (h + step * 6 + 360) % 360;
            else
                sv[row - 1] = MAX(0, MIN(100, sv[row - 1] + step * 3));
        }
        else if (a == ACTION_STD_OK)
            row = (row + 1) % 3;
        else if (a == ACTION_STD_CANCEL || a == ACTION_STD_MENU)
            break;
        else if (default_event_handler(a) == SYS_USB_CONNECTED)
            break;
    }
    *colour = (int)from_hsv(h, sv[0], sv[1]);
}

enum { ED_BAR, ED_BG, ED_ACCENT, ED_USE, ED_COUNT };

static void draw_editor(int sel)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    unsigned fg = global_settings.fg_color, bg = global_settings.bg_color, barfg;
    d->set_drawmode(DRMODE_SOLID);
    d->set_background(bg);
    d->clear_viewport();
    wheel_top_bar(d, LCD_WIDTH, 38, &barfg);
    d->set_drawmode(DRMODE_FG);
    vp.font = typo_font(TU_DISPLAY_MENU);
    d->set_foreground(barfg);
    d->putsxy(16, 2, (unsigned char *)str(LANG_WHEEL_PAL_MINE));

    /* left: the rows; right: a small preview of the palette */
    static const int labels[ED_COUNT] = { LANG_WHEEL_PAL_BAR, LANG_WHEEL_PAL_BG,
                                          LANG_WHEEL_PAL_ACCENT, LANG_WHEEL_PAL_USE };
    int vals[3] = { global_settings.wheel_custom_bar, global_settings.wheel_custom_bg,
                    global_settings.wheel_custom_accent };
    const int RH = 52, LW = 250;
    for (int i = 0; i < ED_COUNT; i++)
    {
        int y = 46 + i * RH;
        unsigned col = fg;
        if (i == sel)
        {
            d->set_drawmode(DRMODE_SOLID);
            wheel_selection(d, 0, y, LW, RH - 4);
            col = global_settings.lst_color;
        }
        d->set_drawmode(DRMODE_FG);
        vp.font = typo_font(TU_ROW);
        d->set_foreground(col);
        int fh = font_get(vp.font)->height;
        d->putsxy(14, y + (RH - 4 - fh) / 2, (unsigned char *)str(labels[i]));
        if (i < 3)
        {
            d->set_drawmode(DRMODE_SOLID);
            round_rect(d, LW - 52, y + 8, 40, RH - 20, 8, rgb(0xffffff), rgb(0xffffff), false);
            round_rect(d, LW - 50, y + 10, 36, RH - 24, 7, rgb(vals[i] & 0xffffff),
                       rgb(vals[i] & 0xffffff), false);
        }
    }
    /* the preview */
    struct wheel_palette p;
    wheel_palette_derive(&p, vals[0] & 0xffffff, vals[1] & 0xffffff, vals[2] & 0xffffff);
    int px = LW + 14, pw = LCD_WIDTH - px - 12, py = 50, ph = 200;
    d->set_drawmode(DRMODE_SOLID);
    round_rect(d, px - 2, py - 2, pw + 4, ph + 4, 10, rgb(p.c[PR_DIVIDER]), rgb(p.c[PR_DIVIDER]), false);
    round_rect(d, px, py, pw, ph, 9, rgb(p.c[PR_BG]), rgb(p.c[PR_BG]), false);
    for (int k = 0; k < 24; k++)
    {
        d->set_foreground(global_settings.wheel_aero ?
                          rgb(wheel_rgb_gel(p.c[PR_BAR], p.c[PR_BAR], k, 24, 70)) : rgb(p.c[PR_BAR]));
        d->hline(px + (k < 6 ? 6 - k : 0) / 2, px + pw - 1 - (k < 6 ? 6 - k : 0) / 2, py + k);
    }
    for (int r = 0; r < 4; r++)
    {
        int y = py + 34 + r * 38;
        if (r == 1)
        {
            for (int k = 0; k < 30; k++)
            {
                d->set_foreground(global_settings.wheel_aero ?
                    rgb(wheel_rgb_gel(p.sel_start, p.sel_end, k, 30, 100)) :
                    rgb(wheel_rgb_blend(p.sel_start, p.sel_end, k * 100 / 29)));
                d->hline(px, px + pw - 1, y + k);
            }
        }
        d->set_foreground(rgb(r == 1 ? p.sel_text : p.c[PR_TEXT]));
        d->fillrect(px + 10, y + 11, pw * (r == 1 ? 50 : 60 - r * 8) / 100, 8);
    }
    d->update_viewport();
    d->set_viewport(last);
}

void wheel_palette_edit(void)
{
    int sel = 0;
    bool changed = false, used = false;
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    while (1)
    {
        draw_editor(sel);
        int a = get_action(CONTEXT_STD, TIMEOUT_BLOCK);
        if (a == ACTION_STD_NEXT || a == ACTION_STD_NEXTREPEAT)
            sel = (sel + 1) % ED_COUNT;
        else if (a == ACTION_STD_PREV || a == ACTION_STD_PREVREPEAT)
            sel = (sel + ED_COUNT - 1) % ED_COUNT;
        else if (a == ACTION_STD_OK)
        {
            static const int titles[3] = { LANG_WHEEL_PAL_BAR, LANG_WHEEL_PAL_BG,
                                           LANG_WHEEL_PAL_ACCENT };
            int *vars[3] = { &global_settings.wheel_custom_bar, &global_settings.wheel_custom_bg,
                             &global_settings.wheel_custom_accent };
            if (sel < 3)
            {
                int before = *vars[sel];
                colour_picker(str(titles[sel]), vars[sel]);
                changed |= before != *vars[sel];
            }
            else
            {   /* use it (again: the skins are made with these colours) */
                FOR_NB_SCREENS(i)
                    viewportmanager_theme_undo(i, false);
                wheel_palette_set(PAL_MINE);
                used = true;
                break;
            }
        }
        else if (a == ACTION_STD_CANCEL || a == ACTION_STD_MENU)
            break;
        else if (default_event_handler(a) == SYS_USB_CONNECTED)
            break;
    }
    if (!used)
    {
        FOR_NB_SCREENS(i)
            viewportmanager_theme_undo(i, false);
        if (changed && global_settings.wheel_palette == PAL_MINE)
            wheel_palette_set(PAL_MINE);   /* in use: remake it */
        else if (changed)
            settings_save();
    }
}

void wheel_palette_screen(void)
{
    int sel = global_settings.wheel_palette, top = 0;
    if (sel < 0 || sel >= N_PALS) sel = 0;
    const int visible = (LCD_HEIGHT - 44) / ROW_H;
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    while (1)
    {
        if (sel < top) top = sel;
        if (sel >= top + visible) top = sel - visible + 1;
        draw_picker(sel, top);
        int a = get_action(CONTEXT_STD, TIMEOUT_BLOCK);
        if (a == ACTION_STD_NEXT || a == ACTION_STD_NEXTREPEAT)
            sel = (sel + 1) % N_PALS;
        else if (a == ACTION_STD_PREV || a == ACTION_STD_PREVREPEAT)
            sel = (sel + N_PALS - 1) % N_PALS;
        else if (a == ACTION_STD_OK && sel == PAL_MINE)
        {   /* "My palette": edit it first */
            FOR_NB_SCREENS(i)
                viewportmanager_theme_undo(i, false);
            wheel_palette_edit();
            FOR_NB_SCREENS(i)
                viewportmanager_theme_enable(i, false, NULL);
        }
        else if (a == ACTION_STD_OK)
        {   /* applied at once, so the picker shows it too */
            if (sel != global_settings.wheel_palette)
            {
                FOR_NB_SCREENS(i)
                    viewportmanager_theme_undo(i, false);
                wheel_palette_set(sel);
                FOR_NB_SCREENS(i)
                    viewportmanager_theme_enable(i, false, NULL);
            }
        }
        else if (a == ACTION_STD_CANCEL || a == ACTION_STD_MENU)
            break;
        else if (default_event_handler(a) == SYS_USB_CONNECTED)
            break;
    }
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, true);
}
