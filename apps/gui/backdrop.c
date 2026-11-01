/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2006 Dave Chapman
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include <stdio.h>
#include "config.h"
#include "lcd.h"
#ifdef HAVE_REMOTE_LCD
#include "lcd-remote.h"
#endif
#include "backdrop.h"
#include <string.h>
#include "file.h"

bool backdrop_load(const char* filename, char *backdrop_buffer)
{
    struct bitmap bm;
    int ret;
    int format = FORMAT_NATIVE | FORMAT_DITHER;
    int maxsize = LCD_BACKDROP_BYTES;

#if defined(HAVE_LCD_COLOR) && defined(HAVE_BMP_SCALING)
    /* Y1 Wheel: a backdrop made for another screen size is scaled to fit
     * (the buffer has room for the scaler, see LCD_BACKDROP_ALLOC) */
    int fd = open(filename, O_RDONLY);
    if (fd >= 0)
    {
        struct bitmap probe;
        memset(&probe, 0, sizeof(probe));
        if (read_bmp_fd(fd, &probe, 0, FORMAT_NATIVE|FORMAT_RETURN_SIZE, NULL) > 0 &&
            (probe.width != LCD_WIDTH || probe.height != LCD_HEIGHT))
        {
            format |= FORMAT_RESIZE;
            maxsize = LCD_BACKDROP_ALLOC;
        }
        close(fd);
    }
    bm.width = LCD_WIDTH;
    bm.height = LCD_HEIGHT;
#endif
    /* load the image */
    bm.data = backdrop_buffer;
    ret = read_bmp_file(filename, &bm, maxsize, format, NULL);

    return ((ret > 0)
            && (bm.width == LCD_WIDTH) && (bm.height == LCD_HEIGHT));
}
  
  
void backdrop_show(char *backdrop_buffer)
{
    lcd_set_backdrop((fb_data*)backdrop_buffer);
}
  

#if defined(HAVE_REMOTE_LCD)

#if LCD_REMOTE_DEPTH > 1
/* api functions */
bool remote_backdrop_load(const char *filename, char* backdrop_buffer)
{
    struct bitmap bm;
    int ret;

    /* load the image */
    bm.data = backdrop_buffer;
    ret = read_bmp_file(filename, &bm, REMOTE_LCD_BACKDROP_BYTES,
                        FORMAT_NATIVE | FORMAT_DITHER | FORMAT_REMOTE, NULL);
    return ((ret > 0)
            && (bm.width == LCD_REMOTE_WIDTH) && (bm.height == LCD_REMOTE_HEIGHT));
}

void remote_backdrop_show(char *backdrop_buffer)
{
    lcd_remote_set_backdrop((fb_remote_data*)backdrop_buffer);
}

#else /* needs stubs */

bool remote_backdrop_load(const char *filename, char* backdrop_buffer)
{
    (void)filename; (void) backdrop_buffer;
    return false;
}
void remote_backdrop_show(char* backdrop_buffer)
{
    (void)backdrop_buffer;
}
#endif
#endif
