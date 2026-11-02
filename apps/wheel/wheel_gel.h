/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - the "Aero" glossy finish: gel bars for the selector,
 * top bars, progress bars and buttons, all drawn from the current colours
 * (no images, so every palette and accent gets its own gloss).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
#ifndef WHEEL_GEL_H
#define WHEEL_GEL_H

#include <stdbool.h>
#ifdef __PCTOOL__
#define wheel_role_colour(c) (c)
#else
#include "screen_access.h"

/* glossy finish switched on and a Wheel theme in use */
bool wheel_aero_on(void);

/* colours that change while the skin is loaded ("wrNN" in a skin, see
 * parse_color()): the palette that follows the cover sets them */
#define WHEEL_ROLE_FLAG     0x7f000000u
#define WHEEL_ROLE_ACCENT   17  /* the template's "accent" (lse) */
#define WHEEL_ROLE_ACCENT2  18  /* "accent2" (lss) */
#define WHEEL_ROLE_ACCENTTX 19  /* "accenttext" (lst) */
#define WHEEL_ROLES         20
/* c itself, or the current colour of the role it names */
unsigned wheel_role_colour(unsigned c);
void wheel_role_set(int role, unsigned lcd_colour);

/* gloss strength of the finish setting (subtle / iPod / max), percent */
int wheel_gloss_pct(void);
/* corner radius of selections and covers (0 = square) */
int wheel_corner_radius(void);

/* the same maths on 0xRRGGBB colours (for generated images) */
unsigned wheel_rgb_blend(unsigned a, unsigned b, int pct);
unsigned wheel_rgb_gel(unsigned top, unsigned bottom, int k, int h, int strength);
int wheel_rgb_strength_for(unsigned base, unsigned text);

/* a + (b - a) * pct / 100, LCD colours */
unsigned wheel_blend(unsigned a, unsigned b, int pct);

/* colour of row k of an h pixel gel going from top (lighter) to bottom
 * (deeper); strength 100 = full gloss, less for bars with light text */
unsigned wheel_gel_row(unsigned top, unsigned bottom, int k, int h, int strength);

/* rows [offset, offset + h) of a gel total_h pixels tall at (x, y) */
void wheel_gel_part(struct screen *d, int x, int y, int w, int h,
                    unsigned top, unsigned bottom, int total_h, int offset,
                    int strength);
void wheel_gel_rect(struct screen *d, int x, int y, int w, int h,
                    unsigned top, unsigned bottom, int strength);

/* a recessed groove (the empty part of a progress bar) */
unsigned wheel_groove_row(unsigned base, int k, int h);

/* the strength that keeps text of colour text readable on base */
int wheel_gel_strength_for(unsigned base, unsigned text);

/* a fading mirror image of a picture (rows [0, h) of src, stride pixels a
 * row) drawn at (x, y) of the current viewport, under it, as far as the
 * viewport has room (up to REFLECT_ROWS); clear just wipes that area */
#define REFLECT_ROWS 24
void wheel_reflect(struct screen *d, const fb_data *src, int stride,
                   int x, int y, int w, int h, bool clear, int radius);

/* a Wheel theme is in use (corners and such only change those) */
bool wheel_skin_is_wheel(void);

/* rounds the corners of a picture just drawn at (x, y) of the current
 * viewport (radius r; a circle when r >= w/2): what is outside the arc
 * becomes the backdrop or background again, with soft edges */
void wheel_round_mask(struct screen *d, int x, int y, int w, int h, int r);

/* rows [offset, offset + h) of a selection bar total_h tall: gel or plain
 * gradient, with rounded corners when the Corners setting asks for them */
void wheel_selection_part(struct screen *d, int x, int y, int w, int h,
                          unsigned top, unsigned bottom, int total_h, int offset);

/* selection bar in the accent colours (gel, or the plain gradient) */
void wheel_selection(struct screen *d, int x, int y, int w, int h);
/* top bar of the screens drawn in C, in the theme's bar colour */
void wheel_top_bar(struct screen *d, int w, int h, unsigned *text);

#endif /* __PCTOOL__ */
#endif
