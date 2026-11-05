/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - music, audiobooks and podcasts for the Y1 Wheel edition.
 * Portability shim: the parsers and the data store build both inside
 * Rockbox and as a plain host program for the unit tests.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
#ifndef SONORA_PORT_H
#define SONORA_PORT_H

#ifdef SONORA_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define fdprintf dprintf
static inline long sonora_filesize(int fd)
{
    struct stat st;
    return fstat(fd, &st) ? -1 : (long)st.st_size;
}
#ifndef SONORA_CUE_DIR
#define SONORA_CUE_DIR "/tmp/sonora-cue"
#endif
#else /* Rockbox */
#include "config.h"
#include "system.h"
#include "file.h"
#include "string-extra.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "rbpaths.h"
#define sonora_filesize(fd) ((long)ffilesize(fd))
#define SONORA_CUE_DIR ROCKBOX_DIR "/cue"
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#endif /* SONORA_PORT_H */
