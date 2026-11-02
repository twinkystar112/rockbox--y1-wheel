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
#include "screen_access.h"

/* glossy finish switched on and a Wheel theme in use */
bool wheel_aero_on(void);

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
                   int x, int y, int w, int h, bool clear);

/* selection bar in the accent colours (gel, or the plain gradient) */
void wheel_selection(struct screen *d, int x, int y, int w, int h);
/* top bar of the screens drawn in C, in the theme's bar colour */
void wheel_top_bar(struct screen *d, int w, int h, unsigned *text);

#endif
