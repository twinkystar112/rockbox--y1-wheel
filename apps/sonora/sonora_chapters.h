/***************************************************************************
 * SONORA - chapter lists read from audio files, never written to them.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef SONORA_CHAPTERS_H
#define SONORA_CHAPTERS_H

#include "sonora_port.h"

#define SONORA_MAX_CHAPTERS 400
#define SONORA_CH_TITLE 80

struct sonora_chapter {
    uint32_t start_ms;
    char title[SONORA_CH_TITLE];
};

enum sonora_chapter_source {
    SCH_NONE = 0,
    SCH_QUICKTIME,  /* MP4 / M4B chapter text track (Apple, most M4B) */
    SCH_NERO,       /* MP4 udta/chpl */
    SCH_ID3,        /* ID3v2 CHAP frames (MP3) */
    SCH_CUE,        /* a .cue sheet next to the file or in .rockbox/cue */
};

/* each returns the number of chapters (0 = none), sorted by start time */
int sonora_chapters_mp4(int fd, struct sonora_chapter *ch, int max,
                        int *source, uint32_t *duration_ms);
int sonora_chapters_id3(int fd, struct sonora_chapter *ch, int max);
int sonora_chapters_cue(const char *cue_path, const char *audio_path,
                        struct sonora_chapter *ch, int max);
/* everything above, in order of trust: embedded first, then cue sheets */
int sonora_chapters_read(const char *path, struct sonora_chapter *ch, int max,
                         int *source);

#endif
