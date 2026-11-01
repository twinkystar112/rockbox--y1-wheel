/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - small screens reachable from Now Playing:
 *   Sound (equaliser on/off + presets + fine tuning), Sleep timer,
 *   Send over Bluetooth.
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
#include "splash.h"
#include "yesno.h"
#include "settings.h"
#include "file.h"
#include "dir.h"
#include "rbpaths.h"
#include "powermgmt.h"
#include "misc.h"
#include "string-extra.h"
#include "menus/eq_menu.h"
#include "wheel_connect.h"
#include "wheel_screens.h"
#include "wheel_listview.h"
#include "wheel_themes.h"

/* ------------------------------- Sound ------------------------------- */

#define EQ_STATE_FILE ROCKBOX_DIR "/wheel-eq.txt"
#define MAX_PRESETS 32

static struct {
    char names[MAX_PRESETS][64];
    int n;
    char current[48];
} eq;

static void eq_scan(void)
{
    /* built-in presets and the user's own (hosted builds keep them apart) */
    eq.n = wheel_dir_list(EQS_DIR, ".cfg", eq.names, MAX_PRESETS);
    eq.current[0] = '\0';
    int fd = open(EQ_STATE_FILE, O_RDONLY);
    if (fd >= 0)
    {
        read_line(fd, eq.current, sizeof(eq.current));
        close(fd);
    }
}

static void eq_remember(const char *name)
{
    strlcpy(eq.current, name, sizeof(eq.current));
    int fd = open(EQ_STATE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0)
    {
        fdprintf(fd, "%s\n", name);
        close(fd);
    }
}

/* rows: on/off, presets..., fine tuning */
static const char *eq_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (item == 0)
    {
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_EQUALIZER),
                 str(global_settings.eq_enabled ? LANG_ON : LANG_OFF));
        return buf;
    }
    if (item == eq.n + 1)
        return str(LANG_WHEEL_EQ_FINE_TUNE);
    const char *name = eq.names[item - 1];
    if (global_settings.eq_enabled && !strcmp(name, eq.current))
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "✓", name);
    else
        strlcpy(buf, name, len);
    return buf;
}

static bool eq_get_nav(int item, void *data)
{
    (void)data;
    return item == eq.n + 1;
}

static int eq_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, eq_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    int item = gui_synclist_get_sel_pos(lists);
    if (item == 0)
    {
        global_settings.eq_enabled = !global_settings.eq_enabled;
        eq_enabled_option_callback(global_settings.eq_enabled);
    }
    else if (item == eq.n + 1)
    {
        eq_menu_graphical();
        eq_remember(""); /* custom now */
        gui_synclist_set_title(lists, str(LANG_WHEEL_SOUND), Icon_NOICON);
    }
    else
    {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), EQS_DIR "/%s.cfg", eq.names[item - 1]);
        settings_load_config(path, false);
        global_settings.eq_enabled = true;
        eq_enabled_option_callback(true);
        eq_remember(eq.names[item - 1]);
    }
    settings_save();
    return ACTION_REDRAW;
}

int wheel_eq_screen(void)
{
    eq_scan();
    struct simplelist_info info;
    simplelist_info_init(&info, str(LANG_WHEEL_SOUND), eq.n + 2, NULL);
    info.get_name = eq_get_name;
    info.action_callback = eq_action;
    for (int i = 0; i < eq.n; i++)
        if (global_settings.eq_enabled && !strcmp(eq.names[i], eq.current))
            info.selection = i + 1;
    simplelist_show_list(&info);
    return 0;
}

/* ---------------------------- Sleep timer ---------------------------- */

static const int sleep_mins[] = { 0, 10, 15, 30, 45, 60, 90, 120 };

static const char *sl_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (item == 0)
    {
        int left = get_sleep_timer();
        if (left > 0)
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d:%02d", str(LANG_OFF),
                     left / 60, left % 60);
        else
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "✓", str(LANG_OFF));
        return buf;
    }
    snprintf(buf, len, "%d min", sleep_mins[item]);
    return buf;
}

static int sl_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_NONE)
        return ACTION_REDRAW; /* keep the countdown moving */
    if (action != ACTION_STD_OK)
        return action;
    int item = gui_synclist_get_sel_pos(lists);
    set_sleeptimer_duration(sleep_mins[item]);
    if (item)
        splashf(HZ * 3 / 2, str(LANG_WHEEL_SLEEP_SET), sleep_mins[item]);
    else
        splash(HZ, ID2P(LANG_WHEEL_SLEEP_OFF));
    return ACTION_STD_CANCEL;
}

int wheel_sleep_screen(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, str(LANG_SLEEP_TIMER), ARRAYLEN(sleep_mins), NULL);
    info.get_name = sl_get_name;
    info.action_callback = sl_action;
    info.timeout = HZ / 2;
    simplelist_show_list(&info);
    return 0;
}

/* ------------------------- Send over Bluetooth ------------------------- */

void wheel_bt_send_screen(const char * const *paths, int n)
{
    if (n <= 0)
        return;
    if (!wheel_bt_send_supported())
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_BT_SEND_UNSUPPORTED));
        return;
    }
    if (!wheel_bt_is_enabled())
    {
        if (!yesno_pop(ID2P(LANG_WHEEL_BT_TURN_ON)))
            return;
        wheel_bt_set_enabled(true);
        for (int i = 0; i < 40 && !wheel_bt_is_enabled(); i++)
            sleep(HZ / 10);
    }
    static struct wheel_bt_device devs[WHEEL_BT_MAX_DEVICES];
    int nd = wheel_bt_send_targets(devs, WHEEL_BT_MAX_DEVICES);
    if (nd == 0)
    {
        splash(HZ * 3, ID2P(LANG_WHEEL_BT_SEND_NO_TARGETS));
        return;
    }
    struct simplelist_info info;
    static const char *names[WHEEL_BT_MAX_DEVICES];
    for (int i = 0; i < nd; i++)
        names[i] = devs[i].name;
    int pick = wheel_pick(str(LANG_WHEEL_BT_SEND_TO), names, nd, 0);
    (void)info;
    if (pick < 0)
        return;
    if (!wheel_bt_send_start(devs[pick].addr, paths, n))
    {
        splash(HZ * 2, ID2P(LANG_FAILED));
        return;
    }
    while (1)
    {
        int file, pct;
        int st = wheel_bt_send_state(&file, &pct);
        switch (st)
        {
            case WHEEL_SEND_CONNECTING:
                splashf(0, "%s %s…", str(LANG_WHEEL_BT_CONNECTING), devs[pick].name);
                break;
            case WHEEL_SEND_WAITING:
                splash(0, ID2P(LANG_WHEEL_BT_SEND_ACCEPT));
                break;
            case WHEEL_SEND_SENDING:
                if (n > 1)
                    splashf(0, str(LANG_WHEEL_BT_SENDING_N), file + 1, n, pct);
                else
                    splashf(0, str(LANG_WHEEL_BT_SENDING_1), pct);
                break;
            case WHEEL_SEND_DONE:
                splash(HZ * 2, ID2P(LANG_WHEEL_BT_SEND_DONE));
                return;
            case WHEEL_SEND_REJECTED:
                splash(HZ * 2, ID2P(LANG_WHEEL_BT_SEND_REJECTED));
                return;
            case WHEEL_SEND_FAILED:
            case WHEEL_SEND_IDLE:
                splash(HZ * 2, ID2P(LANG_WHEEL_BT_SEND_FAILED));
                return;
        }
        int a = get_action(CONTEXT_STD, HZ / 5);
        if (a == ACTION_STD_CANCEL || a == ACTION_STD_MENU)
        {
            wheel_bt_send_cancel();
            splash(HZ, ID2P(LANG_CANCEL));
            return;
        }
    }
}
