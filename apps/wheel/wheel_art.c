/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - album art for screens that are not the WPS
 * (playlists, lyrics, "For you"): load any track's cover into a small
 * bitmap and work out a background colour from it.
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
#include "file.h"
#include "lcd.h"
#include "metadata.h"
#include "albumart.h"
#include "bmp.h"
#include "jpeg_load.h"
#include "string-extra.h"
#include "wheel_art.h"

#if defined(HAVE_ALBUMART) && (LCD_DEPTH >= 16)

static bool load_into(struct wheel_art *a, const char *path, struct mp3entry *id3)
{
    char aa[MAX_PATH];
    struct dim dim = { .width = a->max, .height = a->max };
    int fmt = FORMAT_NATIVE | FORMAT_RESIZE | FORMAT_KEEP_ASPECT | FORMAT_DITHER;
    a->bm.width = a->max;
    a->bm.height = a->max;
    a->bm.data = (unsigned char *)a->buf;
    int size = -1;

    if (find_albumart(id3, aa, sizeof(aa), &dim))
    {
        const char *ext = strrchr(aa, '.');
        if (ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg")))
            size = read_jpeg_file(aa, &a->bm, a->bufsize, fmt, NULL);
        else
            size = read_bmp_file(aa, &a->bm, a->bufsize, fmt, NULL);
    }
#ifdef HAVE_JPEG
    if (size <= 0 && id3->has_embedded_albumart && id3->albumart.type == AA_TYPE_JPG)
    {
        a->bm.width = a->max;
        a->bm.height = a->max;
        size = clip_jpeg_file(path, id3->albumart.pos, id3->albumart.size,
                              &a->bm, a->bufsize, fmt, NULL);
    }
#endif
    return size > 0 && a->bm.width > 0 && a->bm.height > 0;
}

bool wheel_art_load(struct wheel_art *a, const char *path)
{
    if (!path || !path[0])
        return false;
    if (a->valid && !strcmp(a->path, path))
        return true;
    a->valid = false;
    strlcpy(a->path, path, sizeof(a->path));

    static struct mp3entry id3;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    bool ok = get_metadata(&id3, fd, path);
    close(fd);
    if (!ok)
        return false;
    a->valid = load_into(a, path, &id3);
    if (a->valid)
    {
        /* average colour, darkened enough for white text on top */
        fb_data *px = (fb_data *)a->bm.data;
        long r = 0, g = 0, b = 0, n = 0;
        int step = MAX(1, a->bm.width / 24);
        for (int y = 0; y < a->bm.height; y += step)
            for (int x = 0; x < a->bm.width; x += step)
            {
                unsigned c = FB_UNPACK_SCALAR_LCD(px[y * a->bm.width + x]);
                r += RGB_UNPACK_RED(c); g += RGB_UNPACK_GREEN(c); b += RGB_UNPACK_BLUE(c);
                n++;
            }
        r /= n; g /= n; b /= n;
        /* keep the hue, cap the brightness */
        long mx = MAX(r, MAX(g, b));
        if (mx > 110)
        {
            r = r * 110 / mx; g = g * 110 / mx; b = b * 110 / mx;
        }
        if (mx < 30)
        {   /* near black covers: a soft graphite */
            r = g = b = 38;
        }
        a->tint = LCD_RGBPACK(r, g, b);
    }
    return a->valid;
}

void wheel_art_draw(struct wheel_art *a, struct screen *d, int x, int y, int box)
{
    if (!a->valid)
        return;
    int ox = x + (box - a->bm.width) / 2, oy = y + (box - a->bm.height) / 2;
    d->bitmap((fb_data *)a->bm.data, ox, oy, a->bm.width, a->bm.height);
}

#else /* no album art / mono */

bool wheel_art_load(struct wheel_art *a, const char *path) { (void)a; (void)path; return false; }
void wheel_art_draw(struct wheel_art *a, struct screen *d, int x, int y, int box)
{ (void)a; (void)d; (void)x; (void)y; (void)box; }

#endif
