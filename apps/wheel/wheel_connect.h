/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - platform layer for Bluetooth audio and Last.fm
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
#ifndef WHEEL_CONNECT_H
#define WHEEL_CONNECT_H

#include <stdbool.h>

/* The UI in apps/wheel/wheel_screens.c only talks to this interface.
 * Implementations:
 *   apps/hosted/android/wheel_connect_android.c  (JNI -> org.rockbox.Helper.Wheel*)
 *   apps/wheel/wheel_connect_stub.c               (simulator demo data / no-op) */

/* ---------------------------- Bluetooth ---------------------------- */

#define WHEEL_BT_NAME_LEN 48
#define WHEEL_BT_ADDR_LEN 18
#define WHEEL_BT_MAX_DEVICES 16

enum wheel_bt_state {
    WHEEL_BT_PAIRED = 0,     /* bonded, not connected */
    WHEEL_BT_CONNECTING,
    WHEEL_BT_CONNECTED,      /* A2DP audio connected */
    WHEEL_BT_FOUND,          /* discovered by a scan, not bonded */
    WHEEL_BT_PAIRING,
};

enum wheel_bt_option {
    WHEEL_BT_OPT_AUTORECONNECT = 0,  /* reconnect to the last headphones */
    WHEEL_BT_OPT_PAUSE_ON_LOSS,      /* pause when the headphones drop */
    WHEEL_BT_OPT_RESUME_ON_CONNECT,  /* resume when they come back */
};

struct wheel_bt_device {
    char name[WHEEL_BT_NAME_LEN];
    char addr[WHEEL_BT_ADDR_LEN];
    int  state;   /* enum wheel_bt_state */
    bool is_audio; /* advertises an audio sink profile */
};

bool wheel_bt_available(void);
bool wheel_bt_is_enabled(void);
void wheel_bt_set_enabled(bool on);
/* bonded devices, connected first */
int  wheel_bt_get_devices(struct wheel_bt_device *out, int max);
bool wheel_bt_connect(const char *addr);
bool wheel_bt_disconnect(const char *addr);
bool wheel_bt_forget(const char *addr);
bool wheel_bt_scan_start(void);
void wheel_bt_scan_stop(void);
bool wheel_bt_is_scanning(void);
/* devices found by the running/last scan that are not bonded yet */
int  wheel_bt_get_scan_results(struct wheel_bt_device *out, int max);
/* bond (if needed) and connect audio */
bool wheel_bt_pair(const char *addr);
bool wheel_bt_get_option(enum wheel_bt_option opt);
void wheel_bt_set_option(enum wheel_bt_option opt, bool value);
void wheel_bt_open_system_settings(void);
/* internet through a phone's Bluetooth tethering (PAN) */
bool wheel_bt_tether(const char *addr, bool on);
int  wheel_bt_tether_state(const char *addr); /* 0 off, 1 connecting, 2 on */
/* short status for the status bar / menu: name of the connected device
 * or empty string */
const char *wheel_bt_connected_name(void);

/* ------------------------------ Last.fm ----------------------------- */

struct wheel_scrobble_status {
    bool enabled;
    bool now_playing;       /* send "now playing" updates */
    bool write_log;         /* also append to /.scrobbler.log (offline) */
    bool signed_in;
    char user[48];
    int  pending;           /* queued, not yet accepted by Last.fm */
    int  sent_total;        /* accepted since install */
    long last_sync;         /* unix time of last successful submit, 0 = never */
    char last_error[96];    /* empty when the last attempt worked */
    bool online;            /* network usable right now */
};

bool wheel_scrobble_get_status(struct wheel_scrobble_status *st);
void wheel_scrobble_set_enabled(bool on);
void wheel_scrobble_set_now_playing(bool on);
void wheel_scrobble_set_write_log(bool on);
/* try to submit the queue now; returns false if offline / not signed in */
bool wheel_scrobble_flush(void);
/* re-read /.rockbox/lastfm.cfg and sign in */
bool wheel_scrobble_sign_in(void);
void wheel_scrobble_sign_out(void);

/* cheap, cached state for the status bar (no JNI round trip) */
bool wheel_bt_audio_connected(void);
int  wheel_scrobble_pending_count(void);

/* Last.fm sign-in with a phone: QR code flow */
/* starts the flow; returns false without network / API key */
bool wheel_scrobble_web_auth_start(void);
/* URL to show as QR (empty while it is being fetched) */
const char *wheel_scrobble_web_auth_url(void);
/* 0 waiting, 1 signed in, -1 failed/expired */
int  wheel_scrobble_web_auth_state(void);
void wheel_scrobble_web_auth_cancel(void);
/* love the playing track (queued when offline) */
bool wheel_scrobble_love_current(void);

/* network + lyrics download (LRCLIB) */
bool wheel_net_available(void);
/* starts a background download; false when offline */
bool wheel_lyrics_fetch_start(const char *artist, const char *title,
                              const char *album, int duration_s, const char *song_path);
/* 0 busy, 1 saved a .lrc next to the song, -1 nothing found */
int  wheel_lyrics_fetch_state(void);

/* --------------------- sending songs over Bluetooth --------------------- */
/* OBEX Object Push to a phone/computer (Android phones and computers accept
 * it; iPhones don't support receiving files over Bluetooth) */
enum wheel_send_state {
    WHEEL_SEND_IDLE = 0, WHEEL_SEND_CONNECTING, WHEEL_SEND_WAITING, /* other side must accept */
    WHEEL_SEND_SENDING, WHEEL_SEND_DONE, WHEEL_SEND_FAILED, WHEEL_SEND_REJECTED,
};
bool wheel_bt_send_supported(void);
/* bonded devices that can receive files (phones, computers) */
int  wheel_bt_send_targets(struct wheel_bt_device *out, int max);
/* queue files for one device; false when Bluetooth is off */
bool wheel_bt_send_start(const char *addr, const char * const *paths, int n);
/* progress of the whole batch */
int  wheel_bt_send_state(int *file_index, int *percent);
void wheel_bt_send_cancel(void);

#endif /* WHEEL_CONNECT_H */
