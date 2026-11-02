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
#define N_PALS ((int)ARRAYLEN(pals))

int wheel_palette_count(void) { return N_PALS; }

const struct wheel_palette *wheel_palette_get(int i)
{
    return i >= 0 && i < N_PALS ? &pals[i].p : NULL;
}

const struct wheel_palette *wheel_palette_active(void)
{
    int i = global_settings.wheel_palette;
    return i > 0 && i < N_PALS ? &pals[i].p : NULL;
}

const char *wheel_palette_map(const char *hex6)
{
    static char out[8];
    const struct wheel_palette *p = wheel_palette_active();
    if (!p)
        return NULL;
    for (int r = 0; r < PR_COUNT; r++)
        if (!strncasecmp(hex6, tmpl[r], 6))
        {
            snprintf(out, sizeof(out), "%06x", p->c[r]);
            return out;
        }
    return NULL;
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

/* the colours Rockbox uses for lists and screens drawn in C */
bool wheel_palette_patch_colours(void)
{
    const struct wheel_palette *p = wheel_palette_active();
    if (!p)
        return false;
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

/* ------------------------- the Aero backdrop ------------------------- */
#define BAR_H 38

/* row y of the backdrop: a gel top bar, a soft shadow under it and a
 * sheen down the page tinted by the selection colour at the bottom */
static unsigned backdrop_row(const struct wheel_palette *p, int y)
{
    unsigned bar = p->c[PR_BAR], bg = p->c[PR_BG];
    if (y < BAR_H - 1)
        return wheel_rgb_gel(bar, bar, y, BAR_H - 1,
                             wheel_rgb_strength_for(bar, p->c[PR_BARTEXT]));
    if (y == BAR_H - 1)
        return p->c[PR_HAIRLINE];
    int t = (y - BAR_H) * 100 / (LCD_HEIGHT - BAR_H);
    unsigned c = wheel_rgb_blend(wheel_rgb_blend(bg, 0xffffff, p->dark ? 4 : 45),
                                 wheel_rgb_blend(bg, p->sel_end, p->dark ? 10 : 7), t);
    if (y - BAR_H < 6) /* the bar floats a little above the page */
        c = wheel_rgb_blend(c, 0, (6 - (y - BAR_H)) * (p->dark ? 7 : 2));
    return c;
}

static void put_le(unsigned char *b, unsigned v, int n)
{
    for (int i = 0; i < n; i++)
        b[i] = (v >> (8 * i)) & 0xff;
}

/* writes wps/Wheel-Pal/aero-<hash>.bmp for the active palette (once) and
 * removes the ones of other palettes; name gets the file name */
bool wheel_palette_backdrop(char *name, size_t len)
{
    const struct wheel_palette *p = wheel_palette_active();
    if (!p)
        return false;
    unsigned h = 2166136261u;
    for (int y = 0; y < LCD_HEIGHT; y++)
        h = (h ^ backdrop_row(p, y)) * 16777619u;
    h = (h ^ (LCD_WIDTH << 16 | LCD_HEIGHT)) * 16777619u;
    snprintf(name, len, "aero-%08x.bmp", h);

    char dir[MAX_PATH], path[MAX_PATH];
    strlcpy(dir, WPS_DIR "/Wheel-Pal", sizeof(dir));
    mkdir(dir);
    DIR *dp = opendir(dir);
    if (dp)
    {
        struct dirent *e;
        while ((e = readdir(dp)))
            if (!strncmp((char *)e->d_name, "aero-", 5) && strcmp((char *)e->d_name, name))
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
    for (int y = LCD_HEIGHT - 1; y >= 0 && ok; y--)
    {
        unsigned c = backdrop_row(p, y);
        for (int x = 0; x < LCD_WIDTH; x++)
        {
            row[x * 3] = c & 0xff;
            row[x * 3 + 1] = (c >> 8) & 0xff;
            row[x * 3 + 2] = (c >> 16) & 0xff;
        }
        ok = write(fd, row, stride) == stride;
    }
    close(fd);
    if (!ok)
        remove(path);
    return ok;
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
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
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
        const struct pal_entry *e = &pals[i];
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
