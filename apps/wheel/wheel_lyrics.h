/* Y1 Wheel edition - Now Playing modes + synced lyrics (.lrc). GPLv2+ */
#ifndef WHEEL_LYRICS_H
#define WHEEL_LYRICS_H
#include <stdbool.h>
enum { WHEEL_MODE_NORMAL = 0, WHEEL_MODE_SCRUB, WHEEL_MODE_LYRICS };
void wheel_lyrics_init(void);
bool wheel_lyrics_available(void);
const char *wheel_lyrics_line(int rel);
int  wheel_wps_mode(void);
void wheel_wps_reset_mode(void);
bool wheel_wps_cycle_mode(void);
void wheel_wps_scrub(int dir);
bool wheel_wps_idle(void);
bool wheel_lyrics_screen(void);
void wheel_lyrics_open(void);   /* from the Now Playing menu */
#endif
