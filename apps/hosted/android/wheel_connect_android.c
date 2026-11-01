/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - JNI glue between the Rockbox UI and
 *   org.rockbox.Helper.WheelBluetooth / WheelScrobbler / WheelMedia
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
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "appevents.h"
#include "metadata.h"
#include "audio.h"
#include "albumart.h"
#include "string-extra.h"
#include "debug.h"
#include "misc.h"
#include "file.h"
#include "thread.h"
#include "settings.h"
#include "wheel/wheel_connect.h"

extern JNIEnv *env_ptr;

static jclass bt_class, lfm_class, media_class;

static jclass global_class(const char *name)
{
    JNIEnv e = *env_ptr;
    jclass local = e->FindClass(env_ptr, name);
    if (!local)
    {
        e->ExceptionClear(env_ptr);
        DEBUGF("wheel: class %s not found\n", name);
        return NULL;
    }
    jclass g = e->NewGlobalRef(env_ptr, local);
    e->DeleteLocalRef(env_ptr, local);
    return g;
}

static void clear_exc(void)
{
    if ((*env_ptr)->ExceptionCheck(env_ptr))
    {
        (*env_ptr)->ExceptionDescribe(env_ptr);
        (*env_ptr)->ExceptionClear(env_ptr);
    }
}

static jstring jstr(const char *s)
{
    return (*env_ptr)->NewStringUTF(env_ptr, s ? s : "");
}

/* --- small typed helpers for static method calls --- */
static jmethodID smethod(jclass c, const char *name, const char *sig)
{
    if (!c) return NULL;
    jmethodID m = (*env_ptr)->GetStaticMethodID(env_ptr, c, name, sig);
    if (!m) clear_exc();
    return m;
}

static bool call_bool(jclass c, const char *name)
{
    jmethodID m = smethod(c, name, "()Z");
    if (!m) return false;
    bool r = (*env_ptr)->CallStaticBooleanMethod(env_ptr, c, m);
    clear_exc();
    return r;
}

static bool call_bool_str(jclass c, const char *name, const char *arg)
{
    jmethodID m = smethod(c, name, "(Ljava/lang/String;)Z");
    if (!m) return false;
    jstring s = (*env_ptr)->NewStringUTF(env_ptr, arg ? arg : "");
    bool r = (*env_ptr)->CallStaticBooleanMethod(env_ptr, c, m, s);
    (*env_ptr)->DeleteLocalRef(env_ptr, s);
    clear_exc();
    return r;
}

static void call_void_bool(jclass c, const char *name, bool v)
{
    jmethodID m = smethod(c, name, "(Z)V");
    if (!m) return;
    (*env_ptr)->CallStaticVoidMethod(env_ptr, c, m, (jboolean)v);
    clear_exc();
}

static void call_void(jclass c, const char *name)
{
    jmethodID m = smethod(c, name, "()V");
    if (!m) return;
    (*env_ptr)->CallStaticVoidMethod(env_ptr, c, m);
    clear_exc();
}

static void copy_jstring(jstring js, char *buf, size_t len)
{
    buf[0] = '\0';
    if (!js) return;
    const char *s = (*env_ptr)->GetStringUTFChars(env_ptr, js, NULL);
    if (s)
    {
        strlcpy(buf, s, len);
        (*env_ptr)->ReleaseStringUTFChars(env_ptr, js, s);
    }
}

/* Java returns "state|audio|addr|name" strings */
static int parse_devices(jobjectArray arr, struct wheel_bt_device *out, int max)
{
    if (!arr) return 0;
    int n = (*env_ptr)->GetArrayLength(env_ptr, arr), count = 0;
    char line[160];
    for (int i = 0; i < n && count < max; i++)
    {
        jstring js = (jstring)(*env_ptr)->GetObjectArrayElement(env_ptr, arr, i);
        copy_jstring(js, line, sizeof(line));
        (*env_ptr)->DeleteLocalRef(env_ptr, js);
        char *p1 = strchr(line, '|'); if (!p1) continue;
        char *p2 = strchr(p1 + 1, '|'); if (!p2) continue;
        char *p3 = strchr(p2 + 1, '|'); if (!p3) continue;
        *p1 = *p2 = *p3 = '\0';
        struct wheel_bt_device *d = &out[count++];
        d->state = atoi(line);
        d->is_audio = p1[1] == '1';
        strlcpy(d->addr, p2 + 1, sizeof(d->addr));
        strlcpy(d->name, p3 + 1, sizeof(d->name));
    }
    (*env_ptr)->DeleteLocalRef(env_ptr, arr);
    return count;
}

static jobjectArray call_strarray(jclass c, const char *name)
{
    jmethodID m = smethod(c, name, "()[Ljava/lang/String;");
    if (!m) return NULL;
    jobjectArray r = (jobjectArray)(*env_ptr)->CallStaticObjectMethod(env_ptr, c, m);
    clear_exc();
    return r;
}

/* ---------------------------- Bluetooth ---------------------------- */

bool wheel_bt_available(void) { return bt_class && call_bool(bt_class, "available"); }
bool wheel_bt_is_enabled(void) { return call_bool(bt_class, "isEnabled"); }
void wheel_bt_set_enabled(bool on) { call_void_bool(bt_class, "setEnabled", on); }

int wheel_bt_get_devices(struct wheel_bt_device *out, int max)
{
    return parse_devices(call_strarray(bt_class, "getDevices"), out, max);
}

bool wheel_bt_connect(const char *addr)    { return call_bool_str(bt_class, "connect", addr); }
bool wheel_bt_disconnect(const char *addr) { return call_bool_str(bt_class, "disconnect", addr); }
bool wheel_bt_forget(const char *addr)     { return call_bool_str(bt_class, "forget", addr); }
bool wheel_bt_pair(const char *addr)       { return call_bool_str(bt_class, "pair", addr); }
bool wheel_bt_scan_start(void)             { return call_bool(bt_class, "startScan"); }
void wheel_bt_scan_stop(void)              { call_void(bt_class, "stopScan"); }
bool wheel_bt_is_scanning(void)            { return call_bool(bt_class, "isScanning"); }

int wheel_bt_get_scan_results(struct wheel_bt_device *out, int max)
{
    return parse_devices(call_strarray(bt_class, "getScanResults"), out, max);
}

bool wheel_bt_get_option(enum wheel_bt_option opt)
{
#ifdef INNIOASIS_Y1
    /* the Y1 port turns A2DP connect/disconnect into headphone plug events
     * (HeadphoneMonitor), so Rockbox's own "Pause on headphone unplug"
     * already does this; show and change that setting instead of running a
     * second, competing mechanism. */
    if (opt == WHEEL_BT_OPT_PAUSE_ON_LOSS)
        return global_settings.unplug_mode >= 1;
    if (opt == WHEEL_BT_OPT_RESUME_ON_CONNECT)
        return global_settings.unplug_mode >= 2;
#endif
    jmethodID m = smethod(bt_class, "getOption", "(I)Z");
    if (!m) return false;
    bool r = (*env_ptr)->CallStaticBooleanMethod(env_ptr, bt_class, m, (jint)opt);
    clear_exc();
    return r;
}

void wheel_bt_set_option(enum wheel_bt_option opt, bool value)
{
#ifdef INNIOASIS_Y1
    if (opt == WHEEL_BT_OPT_PAUSE_ON_LOSS || opt == WHEEL_BT_OPT_RESUME_ON_CONNECT)
    {
        int m = global_settings.unplug_mode; /* 0 off, 1 pause, 2 pause+resume */
        if (opt == WHEEL_BT_OPT_PAUSE_ON_LOSS)
            m = value ? MAX(m, 1) : 0;
        else
            m = value ? 2 : MIN(m, 1);
        if (m != global_settings.unplug_mode)
        {
            global_settings.unplug_mode = m;
            settings_save();
        }
        return;
    }
#endif
    jmethodID m = smethod(bt_class, "setOption", "(IZ)V");
    if (!m) return;
    (*env_ptr)->CallStaticVoidMethod(env_ptr, bt_class, m, (jint)opt, (jboolean)value);
    clear_exc();
}

void wheel_bt_open_system_settings(void) { call_void(bt_class, "openSystemSettings"); }

const char *wheel_bt_connected_name(void)
{
    static char name[WHEEL_BT_NAME_LEN];
    jmethodID m = smethod(bt_class, "connectedName", "()Ljava/lang/String;");
    name[0] = '\0';
    if (!m) return name;
    jstring js = (jstring)(*env_ptr)->CallStaticObjectMethod(env_ptr, bt_class, m);
    clear_exc();
    copy_jstring(js, name, sizeof(name));
    if (js) (*env_ptr)->DeleteLocalRef(env_ptr, js);
    return name;
}

/* called by WheelBluetooth (Java main thread) */
JNIEXPORT void JNICALL
Java_org_rockbox_Helper_WheelBluetooth_nativeSinkLost(JNIEnv *env, jclass c)
{
    (void)env; (void)c;
#ifndef INNIOASIS_Y1 /* Y1: HeadphoneMonitor already reports this as unplug */
    if (is_rockbox_ready())
        queue_broadcast(SYS_AUDIO_SINK_LOST, 0);
#endif
}

JNIEXPORT void JNICALL
Java_org_rockbox_Helper_WheelBluetooth_nativeSinkBack(JNIEnv *env, jclass c)
{
    (void)env; (void)c;
#ifndef INNIOASIS_Y1
    if (is_rockbox_ready())
        queue_broadcast(SYS_AUDIO_SINK_BACK, 0);
#endif
}

/* pushed by Java so the status bar never waits on JNI */
static volatile int bt_audio_connected, scrobbles_pending;

JNIEXPORT void JNICALL
Java_org_rockbox_Helper_WheelBluetooth_nativeAudioState(JNIEnv *env, jclass c, jboolean on)
{
    (void)env; (void)c;
    bt_audio_connected = on ? 1 : 0;
}

JNIEXPORT void JNICALL
Java_org_rockbox_Helper_WheelScrobbler_nativePending(JNIEnv *env, jclass c, jint n)
{
    (void)env; (void)c;
    scrobbles_pending = n;
}

bool wheel_bt_audio_connected(void) { return bt_audio_connected != 0; }
int  wheel_scrobble_pending_count(void) { return scrobbles_pending; }

/* ------------------------------ Last.fm ----------------------------- */

bool wheel_scrobble_get_status(struct wheel_scrobble_status *st)
{
    jmethodID m = smethod(lfm_class, "getStatus", "()Ljava/lang/String;");
    if (!m) return false;
    jstring js = (jstring)(*env_ptr)->CallStaticObjectMethod(env_ptr, lfm_class, m);
    clear_exc();
    char buf[512];
    copy_jstring(js, buf, sizeof(buf));
    if (js) (*env_ptr)->DeleteLocalRef(env_ptr, js);
    if (!buf[0]) return false;

    /* enabled \t nowplaying \t log \t signedin \t user \t pending \t sent \t lastsync \t online \t error */
    char *f[10] = { 0 };
    int n = 0;
    char *p = buf;
    f[n++] = p;
    while (n < 10 && (p = strchr(p, '\t')))
    {
        *p++ = '\0';
        f[n++] = p;
    }
    if (n < 10) return false;
    memset(st, 0, sizeof(*st));
    st->enabled = f[0][0] == '1';
    st->now_playing = f[1][0] == '1';
    st->write_log = f[2][0] == '1';
    st->signed_in = f[3][0] == '1';
    strlcpy(st->user, f[4], sizeof(st->user));
    st->pending = atoi(f[5]);
    st->sent_total = atoi(f[6]);
    st->last_sync = atol(f[7]);
    st->online = f[8][0] == '1';
    strlcpy(st->last_error, f[9], sizeof(st->last_error));
    return true;
}

void wheel_scrobble_set_enabled(bool on)     { call_void_bool(lfm_class, "setEnabled", on); }
void wheel_scrobble_set_now_playing(bool on) { call_void_bool(lfm_class, "setNowPlaying", on); }
void wheel_scrobble_set_write_log(bool on)   { call_void_bool(lfm_class, "setWriteLog", on); }
bool wheel_scrobble_flush(void)              { return call_bool(lfm_class, "flush"); }
bool wheel_scrobble_sign_in(void)            { return call_bool(lfm_class, "signIn"); }
void wheel_scrobble_sign_out(void)           { call_void(lfm_class, "signOut"); }
bool wheel_scrobble_web_auth_start(void)     { return call_bool(lfm_class, "webAuthStart"); }
void wheel_scrobble_web_auth_cancel(void)    { call_void(lfm_class, "webAuthCancel"); }
bool wheel_scrobble_love_current(void)       { return call_bool(lfm_class, "loveCurrent"); }

const char *wheel_scrobble_web_auth_url(void)
{
    static char url[256];
    jmethodID m = smethod(lfm_class, "webAuthUrl", "()Ljava/lang/String;");
    url[0] = '\0';
    if (!m) return url;
    jstring js = (jstring)(*env_ptr)->CallStaticObjectMethod(env_ptr, lfm_class, m);
    clear_exc();
    copy_jstring(js, url, sizeof(url));
    if (js) (*env_ptr)->DeleteLocalRef(env_ptr, js);
    return url;
}

int wheel_scrobble_web_auth_state(void)
{
    jmethodID m = smethod(lfm_class, "webAuthState", "()I");
    if (!m) return -1;
    int r = (*env_ptr)->CallStaticIntMethod(env_ptr, lfm_class, m);
    clear_exc();
    return r;
}

/* ------------------------------ lyrics ----------------------------- */
static jclass lyr_class;

bool wheel_net_available(void) { return call_bool(lyr_class, "netAvailable"); }

bool wheel_lyrics_fetch_start(const char *artist, const char *title,
                              const char *album, int duration_s, const char *song_path)
{
    jmethodID m = smethod(lyr_class, "fetch",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)Z");
    if (!m) return false;
    jstring a = jstr(artist), t = jstr(title), al = jstr(album), p = jstr(song_path);
    bool r = (*env_ptr)->CallStaticBooleanMethod(env_ptr, lyr_class, m, a, t, al,
                                                 (jint)duration_s, p);
    clear_exc();
    (*env_ptr)->DeleteLocalRef(env_ptr, a);
    (*env_ptr)->DeleteLocalRef(env_ptr, t);
    (*env_ptr)->DeleteLocalRef(env_ptr, al);
    (*env_ptr)->DeleteLocalRef(env_ptr, p);
    return r;
}

int wheel_lyrics_fetch_state(void)
{
    jmethodID m = smethod(lyr_class, "state", "()I");
    if (!m) return -1;
    int r = (*env_ptr)->CallStaticIntMethod(env_ptr, lyr_class, m);
    clear_exc();
    return r;
}

bool wheel_bt_tether(const char *addr, bool on)
{
    jmethodID m = smethod(bt_class, "tether", "(Ljava/lang/String;Z)Z");
    if (!m) return false;
    jstring a = jstr(addr);
    bool r = (*env_ptr)->CallStaticBooleanMethod(env_ptr, bt_class, m, a, (jboolean)on);
    clear_exc();
    (*env_ptr)->DeleteLocalRef(env_ptr, a);
    return r;
}

int wheel_bt_tether_state(const char *addr)
{
    jmethodID m = smethod(bt_class, "tetherState", "(Ljava/lang/String;)I");
    if (!m) return 0;
    jstring a = jstr(addr);
    int r = (*env_ptr)->CallStaticIntMethod(env_ptr, bt_class, m, a);
    clear_exc();
    (*env_ptr)->DeleteLocalRef(env_ptr, a);
    return r;
}

/* ------------------------ Bluetooth file send ----------------------- */
static jclass send_class;

bool wheel_bt_send_supported(void) { return send_class && call_bool(send_class, "supported"); }

int wheel_bt_send_targets(struct wheel_bt_device *out, int max)
{
    return parse_devices(call_strarray(send_class, "targets"), out, max);
}

bool wheel_bt_send_start(const char *addr, const char * const *paths, int n)
{
    jmethodID m = smethod(send_class, "start", "(Ljava/lang/String;[Ljava/lang/String;)Z");
    if (!m || n <= 0) return false;
    jclass strc = (*env_ptr)->FindClass(env_ptr, "java/lang/String");
    jobjectArray arr = (*env_ptr)->NewObjectArray(env_ptr, n, strc, NULL);
    for (int i = 0; i < n; i++)
    {
        jstring js = jstr(paths[i]);
        (*env_ptr)->SetObjectArrayElement(env_ptr, arr, i, js);
        (*env_ptr)->DeleteLocalRef(env_ptr, js);
    }
    jstring a = jstr(addr);
    bool r = (*env_ptr)->CallStaticBooleanMethod(env_ptr, send_class, m, a, arr);
    clear_exc();
    (*env_ptr)->DeleteLocalRef(env_ptr, a);
    (*env_ptr)->DeleteLocalRef(env_ptr, arr);
    (*env_ptr)->DeleteLocalRef(env_ptr, strc);
    return r;
}

/* Java packs state | file index << 8 | percent << 20 */
int wheel_bt_send_state(int *file_index, int *percent)
{
    *file_index = 0; *percent = 0;
    jmethodID m = smethod(send_class, "progress", "()I");
    if (!m) return WHEEL_SEND_IDLE;
    int r = (*env_ptr)->CallStaticIntMethod(env_ptr, send_class, m);
    clear_exc();
    *file_index = (r >> 8) & 0xfff;
    *percent = (r >> 20) & 0x7f;
    return r & 0xff;
}

void wheel_bt_send_cancel(void) { call_void(send_class, "cancel"); }

/* --------------------------- track events --------------------------- */

static jmethodID m_track_start, m_track_end;


static void wheel_track_changed(unsigned short id, void *param)
{
    (void)id;
    struct mp3entry *id3 = ((struct track_event *)param)->id3;
    if (!id3 || !media_class || !m_track_start)
        return;

    char buf[MAX_PATH];
    const char *title = id3->title;
    if (!title && id3->path[0])
    {
        const char *slash = strrchr(id3->path, '/');
        title = strip_extension(buf, sizeof(buf), slash ? slash + 1 : id3->path);
    }
    /* reuse the album art notification.c extracted, if any */
    char art[MAX_PATH];
    static const struct dim dim = { .width = 200, .height = 200 };
    jstring jart = NULL;
    if (find_albumart(id3, art, sizeof(art), &dim))
        jart = jstr(art);
    else if (id3->has_embedded_albumart)
    {
        /* notification.c (runs first) extracts embedded JPEG art here */
        snprintf(art, sizeof(art), "/sdcard/rockbox/.temp_albumart_%d.jpg",
                 thread_self());
        if (file_exists(art))
            jart = jstr(art);
    }

    jstring a = jstr(title), b = jstr(id3->artist), c = jstr(id3->album),
            d = jstr(id3->albumartist), p = jstr(id3->path), mb = jstr(id3->mb_track_id);
    (*env_ptr)->CallStaticVoidMethod(env_ptr, media_class, m_track_start,
            a, b, c, d, (jint)id3->tracknum, (jint)id3->length, p, mb, jart);
    clear_exc();
    (*env_ptr)->DeleteLocalRef(env_ptr, a);
    (*env_ptr)->DeleteLocalRef(env_ptr, b);
    (*env_ptr)->DeleteLocalRef(env_ptr, c);
    (*env_ptr)->DeleteLocalRef(env_ptr, d);
    (*env_ptr)->DeleteLocalRef(env_ptr, p);
    (*env_ptr)->DeleteLocalRef(env_ptr, mb);
    if (jart) (*env_ptr)->DeleteLocalRef(env_ptr, jart);
}

static void wheel_track_finished(unsigned short id, void *param)
{
    (void)id;
    struct track_event *te = (struct track_event *)param;
    if (!te->id3 || (te->flags & TEF_REWIND) || !media_class || !m_track_end)
        return;
    (*env_ptr)->CallStaticVoidMethod(env_ptr, media_class, m_track_end,
            (jint)te->id3->elapsed, (jint)te->id3->length,
            (jboolean)((te->flags & TEF_AUTO_SKIP) != 0));
    clear_exc();
}

/* called from notification_init() once the service is up */
void wheel_connect_init(void)
{
    bt_class    = global_class("org/rockbox/Helper/WheelBluetooth");
    lfm_class   = global_class("org/rockbox/Helper/WheelScrobbler");
    media_class = global_class("org/rockbox/Helper/WheelMedia");
    lyr_class   = global_class("org/rockbox/Helper/WheelLyrics");
    send_class  = global_class("org/rockbox/Helper/WheelSend");
    if (media_class)
    {
        m_track_start = smethod(media_class, "onTrackStart",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
            "IILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        m_track_end = smethod(media_class, "onTrackEnd", "(IIZ)V");
        add_event(PLAYBACK_EVENT_TRACK_CHANGE, wheel_track_changed);
        add_event(PLAYBACK_EVENT_TRACK_FINISH, wheel_track_finished);
    }
}
