/* Y1 Wheel edition - listening history + statistics. GPLv2+ */
#ifndef WHEEL_STATS_H
#define WHEEL_STATS_H
#include <stdbool.h>
#include <stddef.h>
#include "file.h"
#include "rbpaths.h"
#define WHEEL_HISTORY_FILE ROCKBOX_DIR "/wheel-history.tsv"
enum wheel_stat_kind { WHEEL_STAT_SONGS = 0, WHEEL_STAT_ARTISTS, WHEEL_STAT_ALBUMS };
void wheel_stats_init(void);
int  wheel_stats_screen(void);
bool wheel_history_split(char *line, char *f[9]);
int  wheel_history_recent_albums(char out[][MAX_PATH], char names[][96], int max);
bool wheel_history_last(char *path, size_t plen, char *title, size_t tlen);
#endif
