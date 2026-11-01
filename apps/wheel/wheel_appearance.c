/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - Appearance: light / dark / automatic theme, accent
 * colour, battery percentage, wheel clicker and letter jump.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "action.h"
#include "list.h"
#include "settings.h"
#include "settings_list.h"
#include "option_select.h"
#include "misc.h"
#include "file.h"
#include "rbpaths.h"
#include "timefuncs.h"
#include "string-extra.h"
#include "wheel_screens.h"
#include "splash.h"
#include "audio.h"
#include "viewport.h"
#include "playlist.h"
#include "metadata.h"
#include "wheel_themes.h"

#define WHEEL_LIGHT_CFG THEME_DIR "/Wheel.cfg"
#define WHEEL_DARK_CFG  THEME_DIR "/Wheel-Dark.cfg"

#ifdef HAVE_LCD_COLOR
/* selector gradient start (lighter) / end (darker) per accent preset */
static const unsigned char accents[][6] = {
    { 0x4a, 0x97, 0xfa,   0x1c, 0x63, 0xd8 }, /* blue */
    { 0xff, 0x6f, 0xae,   0xe0, 0x33, 0x7f }, /* pink */
    { 0xa7, 0x7b, 0xf3,   0x71, 0x40, 0xd9 }, /* purple */
    { 0x4c, 0xd0, 0x7a,   0x1f, 0x9a, 0x4a }, /* green */
    { 0xff, 0xad, 0x4a,   0xec, 0x74, 0x16 }, /* orange */
    { 0xff, 0x6a, 0x63,   0xd6, 0x2d, 0x2a }, /* red */
    { 0x92, 0x92, 0x9a,   0x5a, 0x5a, 0x62 }, /* graphite */
};
#endif

static int loaded_variant = -1; /* 0 light, 1 dark, -1 unknown */

static bool is_night(void)
{
#if CONFIG_RTC
    struct tm *tm = get_time();
    if (tm)
        return tm->tm_hour >= 21 || tm->tm_hour < 7;
#endif
    return false;
}

static int wanted_variant(void)
{
    switch (global_settings.wheel_appearance)
    {
        case 1:  return 1;
        case 2:  return is_night() ? 1 : 0;
        default: return 0;
    }
}

static bool wheel_theme_active(void)
{
    /* only manage the theme when one of ours is in use, so people who
     * picked another theme keep it */
    return strstr(global_settings.sbs_file, "Wheel") != NULL ||
           global_settings.sbs_file[0] == '\0';
}

/* settings_apply_skins() stops playback (album art buffers are rebuilt);
 * put the music back where it was so a colour change isn't an interruption */
void wheel_reload_skins(void)
{
    int status = audio_status();
    bool playing = status & AUDIO_STATUS_PLAY;
    bool paused = status & AUDIO_STATUS_PAUSE;
    int index = 0;
    unsigned long elapsed = 0, offset = 0;
    if (playing)
    {
        struct mp3entry *id3 = audio_current_track();
        playlist_get_resume_info(&index);
        if (id3)
        {
            elapsed = id3->elapsed;
            offset = id3->offset;
        }
    }
    settings_apply_skins();
    if (playing)
    {
        playlist_start(index, elapsed, offset);
        if (paused)
            audio_pause();
    }
}

/* load light/dark theme as needed, then apply the accent on top */
void wheel_appearance_apply(bool force)
{
    if (!wheel_theme_active())
        return;
    if (loaded_variant < 0)
        loaded_variant = strstr(global_settings.sbs_file, "Dark") ? 1 : 0;

    int want = wanted_variant();
    bool changed = false;
    if (force || want != loaded_variant)
    {
        const char *cfg = want ? WHEEL_DARK_CFG : WHEEL_LIGHT_CFG;
        /* (file_exists() doesn't see the read-only theme dir on hosted
         * builds, so just try to load it) */
        if (settings_load_config(cfg, false))
        {
            loaded_variant = want;
            changed = true;
        }
    }
#ifdef HAVE_LCD_COLOR
    int a = global_settings.wheel_accent;
    if (a < 0 || a >= (int)ARRAYLEN(accents))
        a = 0;
    int lss = LCD_RGBPACK(accents[a][0], accents[a][1], accents[a][2]);
    int lse = LCD_RGBPACK(accents[a][3], accents[a][4], accents[a][5]);
    if (lss != global_settings.lss_color || lse != global_settings.lse_color)
    {
        global_settings.lss_color = lss;
        global_settings.lse_color = lse;
        global_settings.lst_color = LCD_RGBPACK(0xff, 0xff, 0xff);
        changed = true;
    }
#endif
    if (changed || force)
    {
        settings_apply(false);
        wheel_reload_skins();
        settings_save();
    }
}

/* called when the main menu shows: follows the clock in automatic mode */
bool wheel_appearance_check(void)
{
    if (global_settings.wheel_appearance == 2 && wheel_theme_active())
    {
        if (loaded_variant < 0)
            loaded_variant = strstr(global_settings.sbs_file, "Dark") ? 1 : 0;
        if (wanted_variant() != loaded_variant &&
            (audio_status() & (AUDIO_STATUS_PLAY|AUDIO_STATUS_PAUSE)) != AUDIO_STATUS_PLAY)
        {
            wheel_appearance_apply(false);
            return true;
        }
    }
    return false;
}

/* ------------------------------ screen ------------------------------ */
enum { AP_THEME, AP_MODE, AP_ACCENT, AP_BATTERY, AP_CLICK, AP_LETTERS, AP_COUNT };

static const char *choice_text(void *var, char *buf, size_t len)
{
    const struct settings_list *s = find_setting(var);
    if (!s)
        return "";
    intptr_t v = ((s->flags & F_T_MASK) == F_T_BOOL) ? (intptr_t)*(bool *)var
                                                     : (intptr_t)*(int *)var;
    return option_get_valuestring(s, buf, len, v);
}

static const char *ap_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    char val[40];
    const char *label = "";
    const char *value = "";
    switch (item)
    {
        case AP_THEME:
            label = str(LANG_WHEEL_THEME);
            value = wheel_theme_current();
            break;
        case AP_MODE:
            label = str(LANG_WHEEL_APPEARANCE);
            value = choice_text(&global_settings.wheel_appearance, val, sizeof(val));
            break;
        case AP_ACCENT:
            label = str(LANG_WHEEL_ACCENT);
            value = choice_text(&global_settings.wheel_accent, val, sizeof(val));
            break;
        case AP_BATTERY:
            label = str(LANG_WHEEL_BATTERY_PERCENT);
            value = choice_text(&global_settings.wheel_battery_percent, val, sizeof(val));
            break;
        case AP_CLICK:
            label = str(LANG_WHEEL_CLICKER);
            value = global_settings.keyclick ? str(LANG_ON) : str(LANG_OFF);
            break;
        case AP_LETTERS:
            label = str(LANG_WHEEL_LETTER_JUMP);
            value = choice_text(&global_settings.wheel_letter_jump, val, sizeof(val));
            break;
    }
    snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", label, value);
    return buf;
}

static bool ap_get_nav(int item, void *data)
{
    (void)data;
    return item == AP_THEME;
}

static int ap_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, ap_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    switch (gui_synclist_get_sel_pos(lists))
    {
        case AP_THEME:
            wheel_theme_screen();
            break;
        case AP_MODE:
            global_settings.wheel_appearance = (global_settings.wheel_appearance + 1) % 3;
            wheel_appearance_apply(false);
            break;
        case AP_ACCENT:
            global_settings.wheel_accent = (global_settings.wheel_accent + 1) % 7;
            wheel_appearance_apply(false);
            break;
        case AP_BATTERY:
            global_settings.wheel_battery_percent = !global_settings.wheel_battery_percent;
            viewportmanager_theme_changed(THEME_STATUSBAR); /* just redraw */
            settings_save();
            break;
        case AP_CLICK:
            /* "moderate" is a short soft tick through the current output */
            global_settings.keyclick = global_settings.keyclick ? 0 : 2;
            settings_save();
            break;
        case AP_LETTERS:
            global_settings.wheel_letter_jump = !global_settings.wheel_letter_jump;
            settings_save();
            break;
    }
    gui_synclist_set_title(lists, str(LANG_WHEEL_APPEARANCE), Icon_NOICON);
    return ACTION_REDRAW;
}

int wheel_appearance_screen(void)
{
    struct simplelist_info info;
    push_current_activity(ACTIVITY_SETTINGS);
    simplelist_info_init(&info, str(LANG_WHEEL_APPEARANCE), AP_COUNT, NULL);
    info.get_name = ap_get_name;
    info.action_callback = ap_action;
    simplelist_show_list(&info);
    pop_current_activity();
    return 0;
}
