/* Y1 Wheel edition - personalisation screens (wheel_custom.c). GPLv2+ */
#ifndef WHEEL_CUSTOM_H
#define WHEEL_CUSTOM_H
#include <stdbool.h>
#include <stddef.h>

/* Appearance sub-screens */
void wheel_finish_screen(void);      /* gloss, reflection, corners */
void wheel_background_screen(void);  /* own background image */
void wheel_np_screen(void);          /* Now Playing layout and details */
void wheel_mainmenu_screen(void);    /* right pane, items and order */
void wheel_looks_screen(void);       /* saved looks, day and night */
void wheel_topbar_screen(void);      /* battery %, clock, name */
void wheel_wheelclick_screen(void);  /* click sound, sensitivity */
void wheel_idleboot_screen(void);    /* idle screen, startup image */

/* the value shown next to those rows in Appearance */
const char *wheel_custom_value(int which, char *buf, size_t len);
enum { WCV_FINISH, WCV_BACKGROUND, WCV_NP, WCV_MENU, WCV_LOOKS, WCV_WHEEL, WCV_IDLE };

/* day/night looks: applies the one for this time if needed (main menu) */
bool wheel_looks_check(void);

/* the idle screen, if it's time for it; true if it was shown */
bool wheel_idle_maybe(void);

/* boot: the startup image saved in the settings, if any; true if shown */
bool wheel_boot_logo(void);

#endif
