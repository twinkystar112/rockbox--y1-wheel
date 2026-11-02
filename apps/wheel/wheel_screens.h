/***************************************************************************
 * Y1 Wheel edition - music-first screens
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef WHEEL_SCREENS_H
#define WHEEL_SCREENS_H

#include <stdbool.h>
#include <stddef.h>

/* return GO_TO_WPS (or 0) so they can be used as MENU_FUNC_CHECK_RETVAL items */
int wheel_coverflow(void);
int wheel_shuffle_songs(void);
int wheel_bluetooth_screen(void);
int wheel_lastfm_screen(void);
int wheel_foryou_screen(void);
const char *wheel_music_dir(void);
extern char wheel_browse_target[]; /* MAX_PATH */

const char *wheel_library_status(char *buf, size_t len);
bool wheel_library_tick(void);
void wheel_init(void);
int  wheel_lastfm_qr_signin(void);

/* Bluetooth file sending (picker + progress) */
void wheel_bt_send_screen(const char * const *paths, int n);
/* quick sound + sleep timer screens for the Now Playing menu */
int  wheel_eq_screen(void);
int  wheel_sleep_screen(void);

/* wheel_appearance.c */
int  wheel_appearance_screen(void);
void wheel_appearance_apply(bool force);
bool wheel_appearance_check(void);
bool wheel_is_night(void);
void wheel_reload_skins(void);
/* Settings > Appearance > Typography & Fonts (wheel_typo_ui.c) */
int  wheel_typography_screen(void);

#endif
