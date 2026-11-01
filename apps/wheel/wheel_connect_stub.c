/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - connectivity stub.
 *
 * On the desktop simulator this provides believable demo data so the
 * Bluetooth / Last.fm screens can be designed and tested without a device.
 * On native targets without Bluetooth everything reports "unavailable".
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
#include <string.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "string-extra.h"
#include "wheel_connect.h"

#if (CONFIG_PLATFORM & PLATFORM_SDL)
#define WHEEL_DEMO 1
#endif

#ifdef WHEEL_DEMO
static bool bt_on = true;
static bool opts[3] = { true, true, false };
static long scan_started = 0;
static long connect_started = 0;
static char connecting_addr[WHEEL_BT_ADDR_LEN];

static struct wheel_bt_device paired[WHEEL_BT_MAX_DEVICES] = {
    { "Studio Buds",        "40:72:18:A0:11:02", WHEEL_BT_CONNECTED, true },
    { "Car Audio",          "00:1A:7D:DA:71:13", WHEEL_BT_PAIRED,    true },
    { "Kitchen Speaker",    "F4:4E:FD:00:3C:9A", WHEEL_BT_PAIRED,    true },
    { "Mi móvil",   "3C:28:6D:10:AA:01", WHEEL_BT_PAIRED,    false },
};
static int n_paired = 4;
static const struct wheel_bt_device found_demo[] = {
    { "Over-Ear 700",       "AC:80:0A:55:12:EE", WHEEL_BT_FOUND, true },
    { "Mini Boom",          "5C:FB:7C:21:90:01", WHEEL_BT_FOUND, true },
    { "Desk Keyboard",      "D0:13:1E:77:42:6B", WHEEL_BT_FOUND, false },
};

static struct wheel_bt_device *find_paired(const char *addr)
{
    for (int i = 0; i < n_paired; i++)
        if (!strcmp(paired[i].addr, addr))
            return &paired[i];
    return NULL;
}

static void tick_connecting(void)
{
    /* finish a demo connection ~1.2s after it was requested */
    if (connecting_addr[0] && TIME_AFTER(current_tick, connect_started + HZ*6/5))
    {
        for (int i = 0; i < n_paired; i++)
            if (paired[i].state == WHEEL_BT_CONNECTED)
                paired[i].state = WHEEL_BT_PAIRED;
        struct wheel_bt_device *d = find_paired(connecting_addr);
        if (d)
            d->state = WHEEL_BT_CONNECTED;
        connecting_addr[0] = '\0';
    }
}

bool wheel_bt_available(void) { return true; }
bool wheel_bt_is_enabled(void) { return bt_on; }
void wheel_bt_set_enabled(bool on)
{
    bt_on = on;
    if (!on)
        for (int i = 0; i < n_paired; i++)
            paired[i].state = WHEEL_BT_PAIRED;
}

int wheel_bt_get_devices(struct wheel_bt_device *out, int max)
{
    tick_connecting();
    int n = 0;
    /* connected first */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n_paired && n < max; i++)
        {
            bool conn = paired[i].state == WHEEL_BT_CONNECTED;
            if ((pass == 0) == conn)
                out[n++] = paired[i];
        }
    return n;
}

bool wheel_bt_connect(const char *addr)
{
    struct wheel_bt_device *d = find_paired(addr);
    if (!d || !bt_on)
        return false;
    d->state = WHEEL_BT_CONNECTING;
    strlcpy(connecting_addr, addr, sizeof(connecting_addr));
    connect_started = current_tick;
    return true;
}

bool wheel_bt_disconnect(const char *addr)
{
    struct wheel_bt_device *d = find_paired(addr);
    if (d)
        d->state = WHEEL_BT_PAIRED;
    return d != NULL;
}

bool wheel_bt_forget(const char *addr)
{
    for (int i = 0; i < n_paired; i++)
        if (!strcmp(paired[i].addr, addr))
        {
            memmove(&paired[i], &paired[i+1], (n_paired - i - 1) * sizeof(paired[0]));
            n_paired--;
            return true;
        }
    return false;
}

bool wheel_bt_scan_start(void) { scan_started = current_tick; return bt_on; }
void wheel_bt_scan_stop(void) { scan_started = 0; }
bool wheel_bt_is_scanning(void)
{
    return scan_started && TIME_BEFORE(current_tick, scan_started + HZ*8);
}

int wheel_bt_get_scan_results(struct wheel_bt_device *out, int max)
{
    if (!scan_started)
        return 0;
    /* reveal one more device every second, like a real inquiry */
    int visible = (current_tick - scan_started) / HZ;
    int n = 0;
    for (int i = 0; i < (int)ARRAYLEN(found_demo) && i < visible && n < max; i++)
        if (!find_paired(found_demo[i].addr))
            out[n++] = found_demo[i];
    return n;
}

bool wheel_bt_pair(const char *addr)
{
    if (find_paired(addr))
        return wheel_bt_connect(addr);
    for (int i = 0; i < (int)ARRAYLEN(found_demo); i++)
        if (!strcmp(found_demo[i].addr, addr) && n_paired < WHEEL_BT_MAX_DEVICES)
        {
            paired[n_paired] = found_demo[i];
            paired[n_paired].state = WHEEL_BT_PAIRED;
            n_paired++;
            return wheel_bt_connect(addr);
        }
    return false;
}

bool wheel_bt_get_option(enum wheel_bt_option opt) { return opts[opt]; }
void wheel_bt_set_option(enum wheel_bt_option opt, bool value) { opts[opt] = value; }
void wheel_bt_open_system_settings(void) { }

const char *wheel_bt_connected_name(void)
{
    for (int i = 0; i < n_paired; i++)
        if (paired[i].state == WHEEL_BT_CONNECTED)
            return paired[i].name;
    return "";
}

/* ------------------------------ Last.fm ----------------------------- */
static struct wheel_scrobble_status demo_st = {
    .enabled = true, .now_playing = true, .write_log = true,
    .signed_in = true, .user = "wheel_listener",
    .pending = 7, .sent_total = 1284, .last_sync = 0,
    .last_error = "", .online = true,
};

bool wheel_scrobble_get_status(struct wheel_scrobble_status *st)
{
    *st = demo_st;
    return true;
}
void wheel_scrobble_set_enabled(bool on) { demo_st.enabled = on; }
void wheel_scrobble_set_now_playing(bool on) { demo_st.now_playing = on; }
void wheel_scrobble_set_write_log(bool on) { demo_st.write_log = on; }
bool wheel_scrobble_flush(void)
{
    demo_st.sent_total += demo_st.pending;
    demo_st.pending = 0;
    demo_st.last_sync = 1;  /* "just now" */
    return true;
}
bool wheel_scrobble_sign_in(void) { demo_st.signed_in = true; return true; }
void wheel_scrobble_sign_out(void) { demo_st.signed_in = false; demo_st.user[0] = 0; }

bool wheel_bt_audio_connected(void) { return bt_on && wheel_bt_connected_name()[0]; }
int  wheel_scrobble_pending_count(void) { return demo_st.pending; }

static long auth_started;
bool wheel_scrobble_web_auth_start(void) { auth_started = current_tick; return true; }
const char *wheel_scrobble_web_auth_url(void)
{
    /* the real token comes from Last.fm; this is shape-alike demo data */
    return TIME_AFTER(current_tick, auth_started + HZ/2) ?
        "https://www.last.fm/api/auth/?api_key=0123456789abcdef0123456789abcdef&token=demo0123456789abcdef0123456789ab" : "";
}
int wheel_scrobble_web_auth_state(void)
{
    if (auth_started && TIME_AFTER(current_tick, auth_started + HZ*12))
    {
        demo_st.signed_in = true;
        strlcpy(demo_st.user, "wheel_listener", sizeof(demo_st.user));
        return 1;
    }
    return 0;
}
void wheel_scrobble_web_auth_cancel(void) { auth_started = 0; }
bool wheel_scrobble_love_current(void) { return true; }

static char tether_addr[WHEEL_BT_ADDR_LEN];
static long tether_started;
bool wheel_bt_tether(const char *addr, bool on)
{
    if (on) { strlcpy(tether_addr, addr, sizeof(tether_addr)); tether_started = current_tick; }
    else tether_addr[0] = '\0';
    return bt_on;
}
int wheel_bt_tether_state(const char *addr)
{
    if (!tether_addr[0] || strcmp(addr, tether_addr)) return 0;
    return TIME_AFTER(current_tick, tether_started + HZ) ? 2 : 1;
}

/* Bluetooth send: a fake phone accepts, then the files "transfer" */
static const struct wheel_bt_device send_demo[] = {
    { "Mi móvil",   "3C:28:6D:10:AA:01", WHEEL_BT_PAIRED, false },
    { "Portátil",           "84:FD:D1:42:07:5E", WHEEL_BT_PAIRED, false },
};
static long send_started;
static int send_n;
static bool send_cancelled;
bool wheel_bt_send_supported(void) { return true; }
int wheel_bt_send_targets(struct wheel_bt_device *out, int max)
{
    int n = MIN(max, (int)ARRAYLEN(send_demo));
    memcpy(out, send_demo, n * sizeof(*out));
    return n;
}
bool wheel_bt_send_start(const char *addr, const char * const *paths, int n)
{
    (void)addr; (void)paths;
    if (!bt_on || n <= 0) return false;
    send_started = current_tick; send_n = n; send_cancelled = false;
    return true;
}
int wheel_bt_send_state(int *file_index, int *percent)
{
    *file_index = 0; *percent = 0;
    if (!send_started) return WHEEL_SEND_IDLE;
    if (send_cancelled) return WHEEL_SEND_FAILED;
    long t = current_tick - send_started;
    if (t < HZ) return WHEEL_SEND_CONNECTING;
    if (t < HZ * 3) return WHEEL_SEND_WAITING;
    long per = HZ * 2; /* two seconds a song */
    long s = t - HZ * 3;
    if (s >= per * send_n) { *file_index = send_n - 1; *percent = 100; return WHEEL_SEND_DONE; }
    *file_index = s / per;
    *percent = (s % per) * 100 / per;
    return WHEEL_SEND_SENDING;
}
void wheel_bt_send_cancel(void) { send_cancelled = true; }

/* the simulator has no network: lyrics come only from .lrc files */
bool wheel_net_available(void) { return false; }
bool wheel_lyrics_fetch_start(const char *a, const char *t, const char *al, int d, const char *p)
{ (void)a; (void)t; (void)al; (void)d; (void)p; return false; }
int  wheel_lyrics_fetch_state(void) { return -1; }

#else /* !WHEEL_DEMO : native targets without radio hardware */

bool wheel_bt_available(void) { return false; }
bool wheel_bt_is_enabled(void) { return false; }
void wheel_bt_set_enabled(bool on) { (void)on; }
int  wheel_bt_get_devices(struct wheel_bt_device *out, int max) { (void)out; (void)max; return 0; }
bool wheel_bt_connect(const char *addr) { (void)addr; return false; }
bool wheel_bt_disconnect(const char *addr) { (void)addr; return false; }
bool wheel_bt_forget(const char *addr) { (void)addr; return false; }
bool wheel_bt_scan_start(void) { return false; }
void wheel_bt_scan_stop(void) { }
bool wheel_bt_is_scanning(void) { return false; }
int  wheel_bt_get_scan_results(struct wheel_bt_device *out, int max) { (void)out; (void)max; return 0; }
bool wheel_bt_pair(const char *addr) { (void)addr; return false; }
bool wheel_bt_get_option(enum wheel_bt_option opt) { (void)opt; return false; }
void wheel_bt_set_option(enum wheel_bt_option opt, bool value) { (void)opt; (void)value; }
void wheel_bt_open_system_settings(void) { }
const char *wheel_bt_connected_name(void) { return ""; }

bool wheel_scrobble_get_status(struct wheel_scrobble_status *st) { (void)st; return false; }
void wheel_scrobble_set_enabled(bool on) { (void)on; }
void wheel_scrobble_set_now_playing(bool on) { (void)on; }
void wheel_scrobble_set_write_log(bool on) { (void)on; }
bool wheel_scrobble_flush(void) { return false; }
bool wheel_scrobble_sign_in(void) { return false; }
void wheel_scrobble_sign_out(void) { }
bool wheel_bt_audio_connected(void) { return false; }
int  wheel_scrobble_pending_count(void) { return 0; }
bool wheel_scrobble_web_auth_start(void) { return false; }
const char *wheel_scrobble_web_auth_url(void) { return ""; }
int  wheel_scrobble_web_auth_state(void) { return -1; }
void wheel_scrobble_web_auth_cancel(void) { }
bool wheel_scrobble_love_current(void) { return false; }
bool wheel_net_available(void) { return false; }
bool wheel_lyrics_fetch_start(const char *a, const char *t, const char *al, int d, const char *p)
{ (void)a; (void)t; (void)al; (void)d; (void)p; return false; }
int  wheel_lyrics_fetch_state(void) { return -1; }
bool wheel_bt_tether(const char *addr, bool on) { (void)addr; (void)on; return false; }
int  wheel_bt_tether_state(const char *addr) { (void)addr; return 0; }
bool wheel_bt_send_supported(void) { return false; }
int  wheel_bt_send_targets(struct wheel_bt_device *out, int max) { (void)out; (void)max; return 0; }
bool wheel_bt_send_start(const char *addr, const char * const *paths, int n)
{ (void)addr; (void)paths; (void)n; return false; }
int  wheel_bt_send_state(int *file_index, int *percent) { *file_index = 0; *percent = 0; return WHEEL_SEND_IDLE; }
void wheel_bt_send_cancel(void) { }
#endif
