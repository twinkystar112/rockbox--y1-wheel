/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - Appearance: light / dark / automatic theme, accent
 * colour or a whole palette, battery percentage, wheel clicker and letter
 * jump.
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
#include "language.h"
#include "menus/exported_menus.h"
#include "talk.h"
#include "tree.h"
#include "filetypes.h"
#include "wheel_typo.h"
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
#include "wheel_palette.h"
/* any palette (it sets the accent colours) */
#define PALETTE_ANY() (wheel_palette_active() != NULL)
/* a palette that brings its own light or dark colours (the cover's follows
 * the light/dark setting) */
#define PALETTE_ON() (PALETTE_ANY() && !wheel_palette_is_dynamic())
#include "wheel_custom.h"
#include "wheel_listview.h"
#else
#define PALETTE_ANY() false
#define PALETTE_ON() false
#endif

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

/* night: from the "night starts" hour to the "day starts" one */
bool wheel_is_night(void)
{
#if CONFIG_RTC
    struct tm *tm = get_time();
    int day = global_settings.wheel_day_hour, night = global_settings.wheel_night_hour;
    if (tm && day != night)
        return night > day ? (tm->tm_hour >= night || tm->tm_hour < day)
                           : (tm->tm_hour >= night && tm->tm_hour < day);
#endif
    return false;
}
#define is_night wheel_is_night

static int wanted_variant(void)
{
    if (PALETTE_ANY())
        return 0; /* a palette brings its own light or dark colours (the
                   * cover's switches by itself, see wheel_palette_poll) */
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
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    /* the typography profile replaces the theme's skins and UI font */
    if (typo_patch_settings_changed())
        changed = true;
#endif
#ifdef HAVE_LCD_COLOR
    int a = global_settings.wheel_accent;
    if (a < 0 || a >= (int)ARRAYLEN(accents))
        a = 0;
    int lss = LCD_RGBPACK(accents[a][0], accents[a][1], accents[a][2]);
    int lse = LCD_RGBPACK(accents[a][3], accents[a][4], accents[a][5]);
    /* (a palette sets the selector colours itself) */
    if (!PALETTE_ANY() &&
        (lss != global_settings.lss_color || lse != global_settings.lse_color))
    {
        global_settings.lss_color = lss;
        global_settings.lse_color = lse;
        global_settings.lst_color = LCD_RGBPACK(0xff, 0xff, 0xff);
        changed = true;
    }
#endif
    if (changed || force)
    {
        settings_apply(true); /* (re)loads the UI font too */
        wheel_reload_skins();
        settings_save();
    }
}

/* called when the main menu shows: follows the clock in automatic mode */
bool wheel_appearance_check(void)
{
    /* the language may have been changed in General Settings */
    typo_update_fallbacks(false);
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    if (wheel_looks_check())
        return true;
#endif
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
enum { AP_THEME, AP_PALETTE, AP_FINISH, AP_BACKGROUND, AP_TYPO, AP_NOWPLAYING, AP_PLVIEW,
       AP_MAINMENU, AP_LOOKS, AP_LANG, AP_MODE, AP_ACCENT, AP_BATTERY, AP_WHEEL, AP_IDLE,
       AP_LETTERS, AP_COUNT };

/* ----------------------------- language ----------------------------- */
/* the languages the Wheel edition is translated into, by their own names
 * (Chinese, Japanese and Korean are drawn with the CJK fallback fonts) */
static const struct { const char *file, *name; } wheel_langs[] = {
    { "espanol", "Español" }, { "english", "English" }, { "francais", "Français" },
    { "chinese-simp", "简体中文" }, { "japanese", "日本語" }, { "korean", "한국어" },
};
#define N_LANGS ((int)ARRAYLEN(wheel_langs))

static const char *lang_name(void)
{
    for (int i = 0; i < N_LANGS; i++)
        if (!strcmp((char *)global_settings.lang_file, wheel_langs[i].file))
            return wheel_langs[i].name;
    return (const char *)global_settings.lang_file;
}

static const char *lg_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data; (void)buf; (void)len;
    return item < N_LANGS ? wheel_langs[item].name : (const char *)str(LANG_WHEEL_LANG_MORE);
}

static int lg_action(int action, struct gui_synclist *lists)
{
    (void)lists;
    return action == ACTION_STD_MENU ? ACTION_STD_CANCEL : action;
}

static void language_screen(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, str(LANG_LANGUAGE), N_LANGS + 1, NULL);
    info.get_name = lg_get_name;
    info.action_callback = lg_action;
    for (int i = 0; i < N_LANGS; i++)
        if (!strcmp((char *)global_settings.lang_file, wheel_langs[i].file))
            info.selection = i;
    simplelist_show_list(&info);
    if (info.selection < 0)
        return;
    if (info.selection == N_LANGS)
    {   /* every language Rockbox has */
        static struct browse_folder_info langs = { LANG_DIR, SHOW_LNG };
        browse_folder(&langs);
    }
    else
    {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), LANG_DIR "/%s.lng", wheel_langs[info.selection].file);
        splash(0, ID2P(LANG_WAIT));
        if (lang_core_load(path))
        {
            splash(HZ, ID2P(LANG_FAILED));
            return;
        }
        /* (set_file() checks file_exists(), which can't see the
         * read-only share dir of hosted builds) */
        strlcpy((char *)global_settings.lang_file, wheel_langs[info.selection].file,
                sizeof(global_settings.lang_file));
        talk_init();
        settings_save();
    }
    typo_update_fallbacks(true);
    viewportmanager_theme_changed(THEME_LANGUAGE);
    settings_apply_skins();
}

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
        case AP_PALETTE:
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
            label = str(LANG_WHEEL_PALETTE);
            value = wheel_palette_get(global_settings.wheel_palette) ?
                    wheel_palette_get(global_settings.wheel_palette)->name : "Wheel";
#endif
            break;
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
        case AP_FINISH:
            label = str(LANG_WHEEL_FINISH);
            value = wheel_custom_value(WCV_FINISH, val, sizeof(val));
            break;
        case AP_BACKGROUND:
            label = str(LANG_WHEEL_WALLPAPER);
            value = wheel_custom_value(WCV_BACKGROUND, val, sizeof(val));
            break;
        case AP_NOWPLAYING:
            label = str(LANG_WHEEL_NOW_PLAYING);
            value = wheel_custom_value(WCV_NP, val, sizeof(val));
            break;
        case AP_MAINMENU:
            label = str(LANG_WHEEL_MAIN_MENU);
            value = wheel_custom_value(WCV_MENU, val, sizeof(val));
            break;
        case AP_PLVIEW:
            label = str(LANG_WHEEL_PL_VIEW);
            value = wheel_playlist_view_name(global_settings.wheel_pl_view);
            break;
        case AP_LOOKS:
            label = str(LANG_WHEEL_LOOKS);
            break;
        case AP_WHEEL:
            label = str(LANG_WHEEL_WHEEL_CLICK);
            value = wheel_custom_value(WCV_WHEEL, val, sizeof(val));
            break;
        case AP_IDLE:
            label = str(LANG_WHEEL_IDLE_BOOT);
            value = wheel_custom_value(WCV_IDLE, val, sizeof(val));
            break;
#endif
        case AP_TYPO:
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
            label = str(LANG_WHEEL_TYPO);
            value = typo_is_custom() ? typo_active()->name : "Wheel";
#endif
            break;
        case AP_LANG:
            label = str(LANG_LANGUAGE);
            value = lang_name();
            break;
        case AP_MODE:
            label = str(LANG_WHEEL_APPEARANCE);
            value = choice_text(&global_settings.wheel_appearance, val, sizeof(val));
            if (PALETTE_ON()) /* the palette decides; OK goes back to Wheel */
                value = str(LANG_WHEEL_PALETTE);
            break;
        case AP_ACCENT:
            label = str(LANG_WHEEL_ACCENT);
            value = choice_text(&global_settings.wheel_accent, val, sizeof(val));
            if (PALETTE_ANY())
                value = str(LANG_WHEEL_PALETTE);
            break;
        case AP_BATTERY:   /* battery %, clock and the bar's name */
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
            label = str(LANG_WHEEL_TOP_BAR);
            value = global_settings.wheel_bar_name[0] ? global_settings.wheel_bar_name :
                    (const char *)str(LANG_WHEEL_BAR_NAME_DEFAULT);
#else
            label = str(LANG_WHEEL_BATTERY_PERCENT);
            value = choice_text(&global_settings.wheel_battery_percent, val, sizeof(val));
#endif
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
    return item == AP_THEME || item == AP_PALETTE || item == AP_FINISH ||
           item == AP_BACKGROUND || item == AP_TYPO || item == AP_NOWPLAYING ||
           item == AP_PLVIEW || item == AP_MAINMENU || item == AP_LOOKS || item == AP_LANG || item == AP_WHEEL ||
           item == AP_IDLE
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
           || item == AP_BATTERY
#endif
           ;
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
        case AP_PALETTE:
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
            wheel_palette_screen();
#endif
            break;
        case AP_TYPO:
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
            wheel_typography_screen();
#endif
            break;
        case AP_LANG:
            language_screen();
            break;
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
        case AP_FINISH:     wheel_finish_screen(); break;
        case AP_BACKGROUND: wheel_background_screen(); break;
        case AP_NOWPLAYING: wheel_np_screen(); break;
        case AP_PLVIEW:     wheel_pick_playlist_view(); break;
        case AP_MAINMENU:   wheel_mainmenu_screen(); break;
        case AP_LOOKS:      wheel_looks_screen(); break;
        case AP_WHEEL:      wheel_wheelclick_screen(); break;
        case AP_IDLE:       wheel_idleboot_screen(); break;
#endif
        case AP_MODE:
        case AP_ACCENT:
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
            if ((gui_synclist_get_sel_pos(lists) == AP_MODE && PALETTE_ON()) ||
                (gui_synclist_get_sel_pos(lists) == AP_ACCENT && PALETTE_ANY()))
            {   /* first press: back to the classic Wheel colours */
                wheel_palette_set(0);
                break;
            }
#endif
            if (gui_synclist_get_sel_pos(lists) == AP_MODE)
                global_settings.wheel_appearance = (global_settings.wheel_appearance + 1) % 3;
            else
                global_settings.wheel_accent = (global_settings.wheel_accent + 1) % 7;
            wheel_appearance_apply(false);
            break;
        case AP_BATTERY:
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
            wheel_topbar_screen();
            break;
#endif
            global_settings.wheel_battery_percent = !global_settings.wheel_battery_percent;
            viewportmanager_theme_changed(THEME_STATUSBAR); /* just redraw */
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
