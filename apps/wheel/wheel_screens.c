/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - music-first screens:
 *   Cover Flow launcher, Shuffle Songs, Bluetooth, Last.fm
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
#include <string.h>
#include <time.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "action.h"
#include "list.h"
#include "menu.h"
#include "splash.h"
#include "yesno.h"
#include "plugin.h"
#include "playlist.h"
#include "audio.h"
#include "settings.h"
#include "misc.h"
#include "file.h"
#include "pathfuncs.h"
#include "string-extra.h"
#include "root_menu.h"
#ifdef HAVE_SPECIAL_DIRS
#include "filesystem-app.h"
#endif
#include "wheel_connect.h"
#include "wheel_screens.h"
#include "wheel_lyrics.h"
#include "wheel_stats.h"
#ifdef HAVE_TAGCACHE
#include "tagcache.h"
#endif
#include "rbpaths.h"
#ifdef INNIOASIS_Y1
#include "reset-bluetooth-android.h"
#endif

#define ON_OFF(b) ((b) ? (const char *)str(LANG_ON) : (const char *)str(LANG_OFF))

/* ------------------------------------------------------------------ */
/* Cover Flow                                                          */
/* ------------------------------------------------------------------ */
int wheel_coverflow(void)
{
    static const char * const paths[] = {
        PLUGIN_DIR "/demos/pictureflow.rock",
        VIEWERS_DIR "/pictureflow.rock",
        PLUGIN_DIR "/apps/pictureflow.rock",
    };
    for (unsigned i = 0; i < ARRAYLEN(paths); i++)
    {
        if (file_exists(paths[i]))
        {
            int ret = plugin_load(paths[i], NULL);
            return (ret == PLUGIN_GOTO_WPS) ? GO_TO_WPS : 0;
        }
    }
    splash(HZ*2, ID2P(LANG_WHEEL_COVERFLOW_MISSING));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Shuffle Songs: every track under the Music folder, shuffled         */
/* ------------------------------------------------------------------ */
char wheel_browse_target[MAX_PATH];

const char *wheel_music_dir(void)
{
    static char path[MAX_PATH];
    static const char * const candidates[] = {
        HOME_DIR "/Music", HOME_DIR "/music", "/Music", "/sdcard/Music",
    };
    for (unsigned i = 0; i < ARRAYLEN(candidates); i++)
    {
        if (dir_exists(candidates[i]))
        {
#ifdef HAVE_SPECIAL_DIRS
            /* playlists need a real absolute path, not "<HOME>/..." */
            const char *real = handle_special_dirs(candidates[i], 0, path, sizeof(path));
            if (real != path)
                strlcpy(path, real, sizeof(path));
#else
            strlcpy(path, candidates[i], sizeof(path));
#endif
            return path;
        }
    }
#ifdef HAVE_SPECIAL_DIRS
    const char *real = handle_special_dirs(HOME_DIR, 0, path, sizeof(path));
    if (real != path)
        strlcpy(path, real, sizeof(path));
    return path;
#else
    return HOME_DIR;
#endif
}

int wheel_shuffle_songs(void)
{
    const char *dir = wheel_music_dir();
    splash(0, ID2P(LANG_WAIT));

    if (playlist_create(NULL, NULL) < 0)
        return 0;
    srand(current_tick);
    playlist_insert_directory(NULL, dir, PLAYLIST_INSERT_LAST, false, true, NULL);
    if (playlist_amount() <= 0)
    {
        splash(HZ*2, ID2P(LANG_WHEEL_NO_SONGS));
        return 0;
    }
    playlist_shuffle(current_tick, -1);
    playlist_start(0, 0, 0);
    return GO_TO_WPS;
}

/* ------------------------------------------------------------------ */
/* shared helpers                                                      */
/* ------------------------------------------------------------------ */
static bool always_nav(int item, void *data)
{
    (void)item; (void)data;
    return true;
}

static const char *bt_state_text(int state)
{
    switch (state)
    {
        case WHEEL_BT_CONNECTED:  return str(LANG_WHEEL_BT_CONNECTED);
        case WHEEL_BT_CONNECTING: return str(LANG_WHEEL_BT_CONNECTING);
        case WHEEL_BT_PAIRING:    return str(LANG_WHEEL_BT_PAIRING);
        case WHEEL_BT_FOUND:      return "";
        default:                  return str(LANG_WHEEL_BT_NOT_CONNECTED);
    }
}

/* ------------------------------------------------------------------ */
/* Bluetooth: device picker                                            */
/* ------------------------------------------------------------------ */
enum bt_row_type {
    BT_ROW_POWER, BT_ROW_DEVICE, BT_ROW_SEARCH, BT_ROW_AUTO,
    BT_ROW_PAUSE, BT_ROW_RESUME, BT_ROW_SYSTEM, BT_ROW_INFO, BT_ROW_RESET,
};

struct bt_row { unsigned char type; signed char dev; };

static struct {
    struct wheel_bt_device devs[WHEEL_BT_MAX_DEVICES];
    int n_devs;
    struct bt_row rows[WHEEL_BT_MAX_DEVICES + 8];
    int n_rows;
    bool enabled;
    unsigned long sig; /* cheap change detection for live refresh */
} bt;

static unsigned long bt_signature(void)
{
    unsigned long s = bt.enabled ? 1 : 0;
    s = s * 31 + bt.n_devs;
    for (int i = 0; i < bt.n_devs; i++)
        s = s * 131 + bt.devs[i].state + (unsigned char)bt.devs[i].addr[15];
    return s;
}

static void bt_rebuild(void)
{
    bt.enabled = wheel_bt_is_enabled();
    bt.n_devs = bt.enabled ? wheel_bt_get_devices(bt.devs, WHEEL_BT_MAX_DEVICES) : 0;
    bt.n_rows = 0;
    bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_POWER, -1 };
    if (bt.enabled)
    {
        for (int i = 0; i < bt.n_devs; i++)
            bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_DEVICE, i };
        bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_SEARCH, -1 };
        bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_AUTO, -1 };
        bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_PAUSE, -1 };
        bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_RESUME, -1 };
    }
    else
        bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_INFO, -1 };
    bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_SYSTEM, -1 };
#ifdef INNIOASIS_Y1
    /* the port's fix for a stuck Bluetooth stack (clears pairings) */
    bt.rows[bt.n_rows++] = (struct bt_row){ BT_ROW_RESET, -1 };
#endif
    bt.sig = bt_signature();
}

static const char *bt_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (item < 0 || item >= bt.n_rows)
        return "";
    struct bt_row *r = &bt.rows[item];
    switch (r->type)
    {
        case BT_ROW_POWER:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_BLUETOOTH), ON_OFF(bt.enabled));
            return buf;
        case BT_ROW_DEVICE:
        {
            struct wheel_bt_device *d = &bt.devs[r->dev];
            const char *st = bt_state_text(d->state);
            if (!d->is_audio && wheel_bt_tether_state(d->addr) == 2)
                st = str(LANG_WHEEL_BT_INTERNET); /* sharing its internet */
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", d->name[0] ? d->name : d->addr, st);
            return buf;
        }
        case BT_ROW_SEARCH: return str(LANG_WHEEL_BT_SEARCH);
        case BT_ROW_AUTO:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_BT_AUTORECONNECT),
                     ON_OFF(wheel_bt_get_option(WHEEL_BT_OPT_AUTORECONNECT)));
            return buf;
        case BT_ROW_PAUSE:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_BT_PAUSE_ON_LOSS),
                     ON_OFF(wheel_bt_get_option(WHEEL_BT_OPT_PAUSE_ON_LOSS)));
            return buf;
        case BT_ROW_RESUME:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_BT_RESUME_ON_CONNECT),
                     ON_OFF(wheel_bt_get_option(WHEEL_BT_OPT_RESUME_ON_CONNECT)));
            return buf;
        case BT_ROW_SYSTEM: return str(LANG_WHEEL_BT_SYSTEM);
        case BT_ROW_INFO:   return str(LANG_WHEEL_BT_OFF_HINT);
#ifdef INNIOASIS_Y1
        case BT_ROW_RESET:  return str(LANG_BLUETOOTH_RESET);
#endif
    }
    return "";
}

static bool bt_get_nav(int item, void *data)
{
    (void)data;
    if (item < 0 || item >= bt.n_rows)
        return false;
    int t = bt.rows[item].type;
    return t == BT_ROW_DEVICE || t == BT_ROW_SEARCH || t == BT_ROW_SYSTEM || t == BT_ROW_RESET;
}

/* --- scan screen --- */
static struct {
    struct wheel_bt_device found[WHEEL_BT_MAX_DEVICES];
    int n;
    bool scanning;
} scan;

static const char *scan_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data; (void)buf; (void)len;
    if (scan.n == 0)
        return scan.scanning ? str(LANG_WHEEL_BT_SEARCHING) : str(LANG_WHEEL_BT_NONE_FOUND);
    if (item == scan.n)
        return scan.scanning ? str(LANG_WHEEL_BT_SEARCHING) : str(LANG_WHEEL_BT_SEARCH_AGAIN);
    struct wheel_bt_device *d = &scan.found[item];
    return d->name[0] ? d->name : d->addr;
}

static int scan_count(void)
{
    return scan.n == 0 ? 1 : scan.n + 1;
}

static int scan_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL; /* MENU = back, like a click wheel */
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, always_nav);
        return action;
    }
    if (action == ACTION_NONE)
    {
        int old_n = scan.n; bool old_s = scan.scanning;
        scan.scanning = wheel_bt_is_scanning();
        scan.n = wheel_bt_get_scan_results(scan.found, WHEEL_BT_MAX_DEVICES);
        if (old_n != scan.n || old_s != scan.scanning)
        {
            gui_synclist_set_nb_items(lists, scan_count());
            return ACTION_REDRAW;
        }
        return action;
    }
    if (action == ACTION_STD_OK)
    {
        int sel = gui_synclist_get_sel_pos(lists);
        if (sel < scan.n)
        {
            wheel_bt_scan_stop();
            splash(0, ID2P(LANG_WHEEL_BT_PAIRING));
            if (wheel_bt_pair(scan.found[sel].addr))
                return ACTION_STD_CANCEL; /* back to the device list */
            splash(HZ*2, ID2P(LANG_WHEEL_BT_FAILED));
            return ACTION_REDRAW;
        }
        if (!scan.scanning)
        {
            wheel_bt_scan_start();
            scan.scanning = true;
        }
        return ACTION_REDRAW;
    }
    return action;
}

static void bt_scan_screen(void)
{
    struct simplelist_info info;
    memset(&scan, 0, sizeof(scan));
    if (!wheel_bt_scan_start())
    {
        splash(HZ*2, ID2P(LANG_WHEEL_BT_FAILED));
        return;
    }
    scan.scanning = true;
    simplelist_info_init(&info, str(LANG_WHEEL_BT_SEARCH), scan_count(), NULL);
    info.get_name = scan_get_name;
    info.action_callback = scan_action;
    info.timeout = HZ/2;
    simplelist_show_list(&info);
    wheel_bt_scan_stop();
}

/* --- per-device menu ---
 * audio devices: connect / forget; phones and computers: use the phone's
 * internet (Bluetooth tethering) / forget */
enum { DM_CONNECT, DM_TETHER, DM_FORGET };
static bool devmenu_connected;
static struct wheel_bt_device *devmenu_dev;
static int devmenu_rows[3], devmenu_n;
static const char *devmenu_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    switch (devmenu_rows[item])
    {
        case DM_CONNECT:
            return devmenu_connected ? str(LANG_WHEEL_BT_DISCONNECT) : str(LANG_WHEEL_BT_CONNECT);
        case DM_TETHER:
        {
            int st = wheel_bt_tether_state(devmenu_dev->addr);
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_BT_TETHER),
                     st == 2 ? str(LANG_WHEEL_BT_CONNECTED) :
                     st == 1 ? str(LANG_WHEEL_BT_CONNECTING) : str(LANG_OFF));
            return buf;
        }
    }
    return str(LANG_WHEEL_BT_FORGET);
}

static int devmenu_action(int action, struct gui_synclist *lists)
{
    (void)lists;
    if (action == ACTION_NONE)
        return ACTION_REDRAW; /* tethering state changes by itself */
    if (action == ACTION_STD_OK && devmenu_rows[gui_synclist_get_sel_pos(lists)] == DM_TETHER)
    {
        bool on = wheel_bt_tether_state(devmenu_dev->addr) != 0;
        if (!wheel_bt_tether(devmenu_dev->addr, !on))
            splash(HZ*3, ID2P(LANG_WHEEL_BT_TETHER_HINT));
        return ACTION_REDRAW;
    }
    return action == ACTION_STD_MENU ? ACTION_STD_CANCEL : action;
}

static void bt_device_menu(struct wheel_bt_device *d)
{
    struct simplelist_info info;
    devmenu_connected = d->state == WHEEL_BT_CONNECTED;
    devmenu_dev = d;
    devmenu_n = 0;
    if (d->is_audio)
        devmenu_rows[devmenu_n++] = DM_CONNECT;
    else
        devmenu_rows[devmenu_n++] = DM_TETHER;
    devmenu_rows[devmenu_n++] = DM_FORGET;
    simplelist_info_init(&info, d->name[0] ? d->name : d->addr, devmenu_n, NULL);
    info.get_name = devmenu_get_name;
    info.action_callback = devmenu_action;
    info.timeout = HZ / 2;
    simplelist_show_list(&info);
    if (info.selection < 0)
        return;
    switch (devmenu_rows[info.selection])
    {
        case DM_CONNECT:
            if (devmenu_connected)
                wheel_bt_disconnect(d->addr);
            else if (!wheel_bt_connect(d->addr))
                splash(HZ*2, ID2P(LANG_WHEEL_BT_FAILED));
            break;
        case DM_FORGET:
            if (yesno_pop(ID2P(LANG_WHEEL_BT_FORGET)))
                wheel_bt_forget(d->addr);
            break;
    }
}

static int bt_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL; /* MENU = back, like a click wheel */
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, bt_get_nav);
        return action;
    }
    if (action == ACTION_NONE)
    {
        /* live refresh: connection states change in the background */
        int sel = gui_synclist_get_sel_pos(lists);
        unsigned long old_sig = bt.sig;
        bt_rebuild();
        if (bt.sig != old_sig)
        {
            gui_synclist_set_nb_items(lists, bt.n_rows);
            gui_synclist_select_item(lists, MIN(sel, bt.n_rows - 1));
            return ACTION_REDRAW;
        }
        return action;
    }
    if (action != ACTION_STD_OK)
        return action;

    int sel = gui_synclist_get_sel_pos(lists);
    struct bt_row r = bt.rows[sel];
    switch (r.type)
    {
        case BT_ROW_POWER:
            wheel_bt_set_enabled(!bt.enabled);
            if (!bt.enabled)
                splash(HZ, ID2P(LANG_WHEEL_BT_TURNING_ON));
            break;
        case BT_ROW_DEVICE:
            bt_device_menu(&bt.devs[r.dev]);
            break;
        case BT_ROW_SEARCH:
            bt_scan_screen();
            break;
        case BT_ROW_AUTO:
            wheel_bt_set_option(WHEEL_BT_OPT_AUTORECONNECT,
                                !wheel_bt_get_option(WHEEL_BT_OPT_AUTORECONNECT));
            break;
        case BT_ROW_PAUSE:
            wheel_bt_set_option(WHEEL_BT_OPT_PAUSE_ON_LOSS,
                                !wheel_bt_get_option(WHEEL_BT_OPT_PAUSE_ON_LOSS));
            break;
        case BT_ROW_RESUME:
            wheel_bt_set_option(WHEEL_BT_OPT_RESUME_ON_CONNECT,
                                !wheel_bt_get_option(WHEEL_BT_OPT_RESUME_ON_CONNECT));
            break;
        case BT_ROW_SYSTEM:
            wheel_bt_open_system_settings();
            break;
#ifdef INNIOASIS_Y1
        case BT_ROW_RESET:
            if (yesno_pop(ID2P(LANG_WHEEL_BT_RESET_WARN)))
                android_reset_bluetooth();
            break;
#endif
        default:
            break;
    }
    bt_rebuild();
    /* nested screens changed the title the status bar shows */
    gui_synclist_set_title(lists, str(LANG_WHEEL_BLUETOOTH), Icon_NOICON);
    gui_synclist_set_nb_items(lists, bt.n_rows);
    gui_synclist_select_item(lists, MIN(sel, bt.n_rows - 1));
    return ACTION_REDRAW;
}

int wheel_bluetooth_screen(void)
{
    struct simplelist_info info;
    if (!wheel_bt_available())
    {
        splash(HZ*2, ID2P(LANG_WHEEL_BT_UNAVAILABLE));
        return 0;
    }
    bt_rebuild();
    push_current_activity(ACTIVITY_SYSTEMSCREEN);
    simplelist_info_init(&info, str(LANG_WHEEL_BLUETOOTH), bt.n_rows, NULL);
    info.get_name = bt_get_name;
    info.action_callback = bt_action;
    info.timeout = HZ/2;
    simplelist_show_list(&info);
    pop_current_activity();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Last.fm                                                             */
/* ------------------------------------------------------------------ */
enum {
    LFM_ENABLED, LFM_ACCOUNT, LFM_PENDING, LFM_SENT, LFM_SYNC,
    LFM_SEND_NOW, LFM_NOWPLAYING, LFM_LOG, LFM_COUNT
};
static struct wheel_scrobble_status lfm;

static void ago(long when, char *buf, size_t len)
{
    if (when <= 0)
    {
        strlcpy(buf, str(LANG_WHEEL_LFM_NEVER), len);
        return;
    }
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    long d = (long)time(NULL) - when;
#else
    long d = 0;
#endif
    if (d < 60)
        strlcpy(buf, str(LANG_WHEEL_LFM_JUST_NOW), len);
    else if (d < 3600)
        snprintf(buf, len, "%ld min", d / 60);
    else if (d < 86400)
        snprintf(buf, len, "%ld h", d / 3600);
    else
        snprintf(buf, len, "%ld d", d / 86400);
}

static const char *lfm_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    char tmp[32];
    switch (item)
    {
        case LFM_ENABLED:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_LFM_SCROBBLING), ON_OFF(lfm.enabled));
            break;
        case LFM_ACCOUNT:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_LFM_ACCOUNT),
                     lfm.signed_in ? lfm.user : (const char *)str(LANG_WHEEL_LFM_NOT_SIGNED_IN));
            break;
        case LFM_PENDING:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d", str(LANG_WHEEL_LFM_PENDING), lfm.pending);
            break;
        case LFM_SENT:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d", str(LANG_WHEEL_LFM_SENT), lfm.sent_total);
            break;
        case LFM_SYNC:
            if (lfm.last_error[0])
                snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_LFM_ERROR), lfm.last_error);
            else
            {
                ago(lfm.last_sync, tmp, sizeof(tmp));
                snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s%s", str(LANG_WHEEL_LFM_LAST_SYNC), tmp,
                         lfm.online ? "" : (const char *)str(LANG_WHEEL_LFM_OFFLINE_SUFFIX));
            }
            break;
        case LFM_SEND_NOW:
            return str(LANG_WHEEL_LFM_SEND_NOW);
        case LFM_NOWPLAYING:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_LFM_NOWPLAYING), ON_OFF(lfm.now_playing));
            break;
        case LFM_LOG:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_LFM_LOG), ON_OFF(lfm.write_log));
            break;
        default:
            return "";
    }
    return buf;
}

static bool lfm_get_nav(int item, void *data)
{
    (void)data;
    return item == LFM_ACCOUNT || item == LFM_SEND_NOW;
}

static int lfm_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL; /* MENU = back, like a click wheel */
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, lfm_get_nav);
        return action;
    }
    if (action == ACTION_NONE)
    {
        struct wheel_scrobble_status old = lfm;
        wheel_scrobble_get_status(&lfm);
        if (memcmp(&old, &lfm, sizeof(lfm)))
            return ACTION_REDRAW;
        return action;
    }
    if (action != ACTION_STD_OK)
        return action;

    switch (gui_synclist_get_sel_pos(lists))
    {
        case LFM_ENABLED:
            wheel_scrobble_set_enabled(!lfm.enabled);
            break;
        case LFM_ACCOUNT:
            if (lfm.signed_in)
            {
                if (yesno_pop(ID2P(LANG_WHEEL_LFM_SIGN_OUT)))
                    wheel_scrobble_sign_out();
            }
            else
            {   /* a lastfm.cfg with a password wins; otherwise the phone/QR flow */
                splash(0, ID2P(LANG_WAIT));
                if (!wheel_scrobble_sign_in())
                    wheel_lastfm_qr_signin();
            }
            break;
        case LFM_SEND_NOW:
            splash(0, ID2P(LANG_WAIT));
            if (!wheel_scrobble_flush())
            {
                wheel_scrobble_get_status(&lfm);
                splashf(HZ*2, "%s", lfm.signed_in ? str(LANG_WHEEL_LFM_OFFLINE)
                                                 : str(LANG_WHEEL_LFM_SETUP_HINT));
            }
            break;
        case LFM_NOWPLAYING:
            wheel_scrobble_set_now_playing(!lfm.now_playing);
            break;
        case LFM_LOG:
            wheel_scrobble_set_write_log(!lfm.write_log);
            break;
        default:
            break;
    }
    wheel_scrobble_get_status(&lfm);
    return ACTION_REDRAW;
}

int wheel_lastfm_screen(void)
{
    struct simplelist_info info;
    if (!wheel_scrobble_get_status(&lfm))
    {
        splash(HZ*2, ID2P(LANG_WHEEL_LFM_UNAVAILABLE));
        return 0;
    }
    push_current_activity(ACTIVITY_SYSTEMSCREEN);
    simplelist_info_init(&info, "Last.fm", LFM_COUNT, NULL);
    info.get_name = lfm_get_name;
    info.action_callback = lfm_action;
    info.timeout = HZ;
    simplelist_show_list(&info);
    pop_current_activity();
    return 0;
}


/* ------------------------------------------------------------------ */
/* Library: build it on the first start, finish it without a restart  */
/* ------------------------------------------------------------------ */
static bool library_autostarted;
static bool library_committing;

/* status line for the theme while the music library is being built */
const char *wheel_library_status(char *buf, size_t len)
{
#ifdef HAVE_TAGCACHE
    struct tagcache_stat *st = tagcache_get_stat();
    if (!st->readyvalid || st->ready || library_committing)
        return library_committing ? str(LANG_WHEEL_LIBRARY_FINISHING) : NULL;
    if (st->commit_step > 0)
        return str(LANG_WHEEL_LIBRARY_FINISHING);
    if (st->processed_entries > 0 || library_autostarted)
    {
        (void)buf; (void)len;
        return str(LANG_WHEEL_LIBRARY_SCANNING);
    }
#else
    (void)buf; (void)len;
#endif
    return NULL;
}

/* called from the main menu loop */
bool wheel_library_tick(void)
{
#ifdef HAVE_TAGCACHE
    struct tagcache_stat *st = tagcache_get_stat();
    if (!st->readyvalid)
        return false;
    if (!st->ready && !library_autostarted && st->processed_entries == 0 &&
        st->commit_step == 0 && !st->commit_delayed)
    {   /* first start: no questions, just build it in the background */
        library_autostarted = true;
        tagcache_rebuild();
        return false;
    }
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s/database_tmp.tcd", st->db_path[0] ? st->db_path : ROCKBOX_DIR);
    if (st->commit_delayed && !(audio_status() & AUDIO_STATUS_PLAY) &&
        file_exists(tmp) && file_exists(PLUGIN_APPS_DIR "/db_commit.rock"))
    {   /* the core ran out of memory to commit while running: the commit
         * plugin can borrow the audio buffer and finishes it right away */
        library_committing = true;
        plugin_load(PLUGIN_APPS_DIR "/db_commit.rock", "wheel:auto");
        library_committing = false;
        return true;
    }
#endif
    return false;
}

void wheel_init(void)
{
    static bool done;
    if (done)
        return;
    done = true;
    wheel_lyrics_init();
    wheel_stats_init();
    wheel_appearance_apply(false);
}
