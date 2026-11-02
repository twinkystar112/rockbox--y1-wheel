/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - the "Aero" glossy finish (see wheel_gel.h).
 *
 * A gel bar is a highlight band over the upper ~45% (a bright rim line,
 * then a soft sheen fading down), a crisp change at the "waterline", and
 * a deeper body that brightens again towards the bottom, as light passing
 * through coloured glass does.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
#include <string.h>
#include "config.h"
#include "system.h"
#include "lcd.h"
#include "settings.h"
#include "wheel_gel.h"
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
#include "wheel_palette.h"
#endif

bool wheel_aero_on(void)
{
#ifdef HAVE_LCD_COLOR
    return global_settings.wheel_aero &&
           (strstr((char *)global_settings.sbs_file, "Wheel") != NULL ||
            global_settings.sbs_file[0] == '\0');
#else
    return false;
#endif
}

/* ---------------- colour maths on 0xRRGGBB (used for files too) ---------------- */
unsigned wheel_rgb_blend(unsigned a, unsigned b, int pct)
{
    if (pct <= 0) return a;
    if (pct >= 100) return b;
    unsigned out = 0;
    for (int sh = 0; sh <= 16; sh += 8)
    {
        int x = (a >> sh) & 0xff, y = (b >> sh) & 0xff;
        out |= (unsigned)(x + (y - x) * pct / 100) << sh;
    }
    return out;
}

static int rgb_luma(unsigned c)
{
    return (((c >> 16) & 0xff) * 30 + ((c >> 8) & 0xff) * 59 + (c & 0xff) * 11) / 100;
}

unsigned wheel_rgb_gel(unsigned top, unsigned bottom, int k, int h, int strength)
{
    const unsigned white = 0xffffff, black = 0;
    if (h < 2)
        return top;
    /* the colour underneath goes from top to bottom; the gel lights it */
    unsigned base = wheel_rgb_blend(top, bottom, k * 100 / (h - 1));
    int mid = (h * 45 + 50) / 100;
    if (mid < 1)
        mid = 1;
    if (k <= 0) /* rim light */
        return wheel_rgb_blend(base, white, 52 * strength / 100);
    if (k < mid) /* the sheen, fading down to the waterline */
    {
        int span = mid > 2 ? mid - 2 : 1;
        int a = 36 - 24 * (k - 1) / span;
        return wheel_rgb_blend(base, white, a * strength / 100);
    }
    if (k == h - 1 && h >= 5) /* lower edge */
        return wheel_rgb_blend(base, black, 12 * strength / 100);
    /* the body: deepest under the waterline, lit again at the bottom */
    int n = h - 1 - mid;
    int t = n > 0 ? (k - mid) * 100 / n : 0;
    unsigned deep = wheel_rgb_blend(base, black, 8 * strength / 100);
    unsigned glow = wheel_rgb_blend(base, white, 14 * strength / 100);
    return wheel_rgb_blend(deep, glow, t);
}

int wheel_rgb_strength_for(unsigned base, unsigned text)
{
    /* light text on a dark bar: less sheen so the text keeps its contrast */
    return rgb_luma(text) > rgb_luma(base) + 40 ? 55 : 100;
}

#ifdef HAVE_LCD_COLOR
#define WHITE LCD_RGBPACK(255, 255, 255)
#define BLACK LCD_RGBPACK(0, 0, 0)

static unsigned to_rgb(unsigned c)
{
    return ((unsigned)RGB_UNPACK_RED(c) << 16) | (RGB_UNPACK_GREEN(c) << 8) | RGB_UNPACK_BLUE(c);
}

static unsigned to_lcd(unsigned c)
{
    return LCD_RGBPACK((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
}

unsigned wheel_blend(unsigned a, unsigned b, int pct)
{
    return to_lcd(wheel_rgb_blend(to_rgb(a), to_rgb(b), pct));
}

unsigned wheel_gel_row(unsigned top, unsigned bottom, int k, int h, int strength)
{
    return to_lcd(wheel_rgb_gel(to_rgb(top), to_rgb(bottom), k, h, strength));
}

unsigned wheel_groove_row(unsigned base, int k, int h)
{
    if (k <= 0)
        return wheel_blend(base, BLACK, h > 3 ? 14 : 8);
    int t = h > 2 ? (k - 1) * 100 / (h - 2) : 100;
    return wheel_blend(wheel_blend(base, BLACK, 4), WHITE, 30 * t / 100);
}

int wheel_gel_strength_for(unsigned base, unsigned text)
{
    return wheel_rgb_strength_for(to_rgb(base), to_rgb(text));
}

void wheel_gel_part(struct screen *d, int x, int y, int w, int h,
                    unsigned top, unsigned bottom, int total_h, int offset,
                    int strength)
{
    if (w <= 0 || h <= 0)
        return;
    unsigned fg = d->get_foreground();
    for (int k = 0; k < h; k++)
    {
        d->set_foreground(wheel_gel_row(top, bottom, offset + k, total_h, strength));
        d->hline(x, x + w - 1, y + k);
    }
    d->set_foreground(fg);
}

void wheel_gel_rect(struct screen *d, int x, int y, int w, int h,
                    unsigned top, unsigned bottom, int strength)
{
    wheel_gel_part(d, x, y, w, h, top, bottom, h, 0, strength);
}

void wheel_selection(struct screen *d, int x, int y, int w, int h)
{
    unsigned top = global_settings.lss_color, bottom = global_settings.lse_color;
    if (wheel_aero_on())
        wheel_gel_rect(d, x, y, w, h, top, bottom,
                       100);
    else
        d->gradient_fillrect(x, y, w, h, top, bottom);
}

void wheel_top_bar(struct screen *d, int w, int h, unsigned *text)
{
    unsigned bg, fg;
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    wheel_palette_bar(&bg, &fg);
#else
    bg = LCD_RGBPACK(0xf7, 0xf7, 0xf9);
    fg = LCD_RGBPACK(0x14, 0x14, 0x16);
#endif
    if (wheel_aero_on())
        wheel_gel_rect(d, 0, 0, w, h - 1, bg, bg, wheel_gel_strength_for(bg, fg));
    else
    {
        d->set_foreground(bg);
        d->fillrect(0, 0, w, h - 1);
    }
    d->set_foreground(wheel_blend(bg, fg, 16));
    d->hline(0, w - 1, h - 1);
    if (text)
        *text = fg;
}
#endif /* HAVE_LCD_COLOR */
