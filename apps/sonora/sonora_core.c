/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - one playback engine, three kinds of listening.
 *
 * Music plays exactly as Rockbox always did. When the current file is in
 * the audiobooks or podcasts folder SONORA applies that kind's policy at
 * runtime only (never written to config.cfg):
 *   - speed per book / per show (Rockbox's timestretch),
 *   - Prev / Next move by chapter, or skip back / ahead N seconds,
 *   - progress saved on pause, stop, track change and every N seconds,
 *   - a short rewind when resuming after a long pause,
 *   - a book that ends stops (it does not roll over to its first file).
 * Starting a book or an episode first saves the music queue so "Resume
 * music" can bring it back: Rockbox has a single queue.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "splash.h"
#include "settings.h"
#include "audio.h"
#include "playlist.h"
#include "appevents.h"
#include "metadata.h"
#include "dir.h"
#include "file.h"
#include "misc.h"
#include "sound.h"
#include "tdspeed.h"
#include "timefuncs.h"
#include "string-extra.h"
#ifdef HAVE_SPECIAL_DIRS
#include "filesystem-app.h"
#endif
#include "sonora.h"
#include "sonora_util.h"

#define MUSIC_KEY "music"

static bool inited;
static char books_root[MAX_PATH], podcasts_root[MAX_PATH];

static struct {
    enum sonora_kind kind;          /* of the current track */
    char key[MAX_PATH];             /* the book or the episode */
    char file[MAX_PATH];            /* the current file */
    /* the current book's files and where each starts on its timeline */
    char book_key[MAX_PATH];
    int nfiles, file_index, prev_index;
    uint32_t start_ms[SONORA_MAX_BOOK_FILES + 1];
    /* chapters of the current file */
    struct sonora_chapter ch[SONORA_MAX_CHAPTERS];
    int nch;
    char ch_file[MAX_PATH];
    uint32_t elapsed, length;
    bool was_playing, was_paused, done_flagged;
    long pause_tick, last_save;
    int32_t music_speed;
    bool speed_touched;
    volatile bool track_changed;
    int sleep_mode;                 /* 0, minutes, SONORA_SLEEP_CHAPTER/_FILE */
    long sleep_deadline;
} st;

static char book_files[SONORA_MAX_BOOK_FILES][MAX_PATH];

/* ------------------------------- basics ------------------------------- */

unsigned long sonora_now(void)
{
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    return (unsigned long)time(NULL);
#elif CONFIG_RTC
    struct tm *tm = get_time();
    return tm ? (unsigned long)mktime(tm) : 0;
#else
    return current_tick / HZ;
#endif
}

static void expand(const char *in, char *out, size_t len)
{
#ifdef HAVE_SPECIAL_DIRS
    const char *real = handle_special_dirs(in, 0, out, len);
    if (real != out)
        strlcpy(out, real, len);
#else
    strlcpy(out, in, len);
#endif
    size_t l = strlen(out);
    while (l > 1 && out[l - 1] == '/')
        out[--l] = '\0';
}

static void set_roots(void)
{
    expand(global_settings.sonora_books_dir[0] ? global_settings.sonora_books_dir
                                               : HOME_DIR "/Audiobooks",
           books_root, sizeof(books_root));
    expand(global_settings.sonora_podcasts_dir[0] ? global_settings.sonora_podcasts_dir
                                                  : HOME_DIR "/Podcasts",
           podcasts_root, sizeof(podcasts_root));
}

void sonora_reload_roots(void) { set_roots(); }

const char *sonora_books_root(void) { return books_root; }
const char *sonora_podcasts_root(void) { return podcasts_root; }

static bool under(const char *path, const char *root)
{
    size_t l = strlen(root);
    return l && !strncmp(path, root, l) && path[l] == '/';
}

enum sonora_kind sonora_kind_of_path(const char *path)
{
    if (!path || !path[0])
        return SK_MUSIC;
    if (!inited)
        set_roots();
    if (under(path, podcasts_root))
        return SK_EPISODE;
    if (under(path, books_root) || sonora_ext_is(path, "m4b"))
        return SK_BOOK;
    return SK_MUSIC;
}

enum sonora_kind sonora_current_kind(void)
{
    struct mp3entry *id3 = audio_status() ? audio_current_track() : NULL;
    return id3 ? sonora_kind_of_path(id3->path) : SK_MUSIC;
}

const char *sonora_current_key(void)
{
    return st.kind != SK_MUSIC ? st.key : NULL;
}

bool sonora_current_position(char *file, size_t flen, uint32_t *pos, uint32_t *done)
{
    struct mp3entry *id3 = audio_status() ? audio_current_track() : NULL;
    if (!id3 || st.kind == SK_MUSIC || strcmp(id3->path, st.file))
        return false;
    if (file)
        strlcpy(file, id3->path, flen);
    if (pos)
        *pos = id3->elapsed;
    if (done)
        *done = (st.kind == SK_BOOK && st.file_index >= 0 && st.nfiles > 0)
                ? st.start_ms[st.file_index] + id3->elapsed : id3->elapsed;
    return true;
}

int sonora_current_file_index(void)
{
    return st.kind == SK_BOOK ? st.file_index : -1;
}

static void on_track_change(unsigned short id, void *param)
{
    (void)id; (void)param;
    st.track_changed = true;
}

void sonora_init(void)
{
    if (inited)
        return;
    set_roots();
    char dir[MAX_PATH];
    expand(ROCKBOX_DIR "/sonora", dir, sizeof(dir));
    if (!dir_exists(dir))
        mkdir(dir);
    sonora_data_init(dir);
    sonora_progress_load();
    sonora_bookmarks_load();
    sonora_shows_load();
    sonora_downloads_load();
    st.music_speed = PITCH_SPEED_100;
    add_event(PLAYBACK_EVENT_TRACK_CHANGE, on_track_change);
    inited = true;
}

/* ------------------------------- speed -------------------------------- */

static void apply_speed(int32_t speed)
{
    if (speed <= 0)
        speed = PITCH_SPEED_100;
    if (speed != PITCH_SPEED_100 && !dsp_timestretch_available())
        dsp_timestretch_enable(true);
    if (dsp_timestretch_available())
        dsp_set_timestretch(speed);
}

static int32_t show_speed_of(const char *episode)
{
    size_t l = strlen(podcasts_root);
    if (!under(episode, podcasts_root))
        return 0;
    char folder[96];
    strlcpy(folder, episode + l + 1, sizeof(folder));
    char *slash = strchr(folder, '/');
    if (slash)
        *slash = '\0';
    struct sonora_show *s = sonora_show_find(folder);
    return s ? s->speed : 0;
}

uint32_t sonora_item_speed(const char *key, enum sonora_kind kind)
{
    int32_t v = 0;
    if (kind == SK_BOOK)
    {
        struct sonora_progress *p = sonora_progress_find(key);
        v = p ? p->speed : 0;
    }
    else if (kind == SK_EPISODE)
        v = show_speed_of(key);
    return v > 0 ? (uint32_t)v : (uint32_t)PITCH_SPEED_100;
}

bool sonora_set_item_speed(const char *key, enum sonora_kind kind, int32_t speed)
{
    if (speed != PITCH_SPEED_100 && !dsp_timestretch_available())
    {
        dsp_timestretch_enable(true);
        if (!dsp_timestretch_available())
            return false; /* needs the setting on and a restart */
    }
    if (kind == SK_BOOK)
    {
        struct sonora_progress *p = sonora_progress_get(key, SK_BOOK_C);
        if (p)
            p->speed = speed;
        sonora_progress_save();
    }
    else if (kind == SK_EPISODE)
    {
        size_t l = strlen(podcasts_root);
        if (under(key, podcasts_root))
        {
            char folder[96];
            strlcpy(folder, key + l + 1, sizeof(folder));
            char *slash = strchr(folder, '/');
            if (slash) *slash = '\0';
            struct sonora_show *s = sonora_show_add(folder);
            if (s)
                s->speed = speed;
            sonora_shows_save();
        }
    }
    if (st.kind == kind && !strcmp(st.key, key))
        apply_speed(speed);
    return true;
}

/* ------------------------------ timeline ------------------------------ */

static int load_book(const char *key)
{
    if (!strcmp(st.book_key, key) && st.nfiles > 0)
        return st.nfiles;
    struct sonora_book *b = sonora_book_find(key);
    struct sonora_book tmp;
    if (!b)
    {
        memset(&tmp, 0, sizeof(tmp));
        strlcpy(tmp.path, key, sizeof(tmp.path));
        tmp.single = !dir_exists(key);
        b = &tmp;
    }
    st.nfiles = sonora_book_files(b, book_files, SONORA_MAX_BOOK_FILES);
    uint32_t t = 0;
    for (int i = 0; i < st.nfiles; i++)
    {
        st.start_ms[i] = t;
        t += sonora_file_length(book_files[i]);
    }
    st.start_ms[st.nfiles] = t;
    strlcpy(st.book_key, key, sizeof(st.book_key));
    return st.nfiles;
}

static int file_index_of(const char *file)
{
    for (int i = 0; i < st.nfiles; i++)
        if (!strcmp(book_files[i], file))
            return i;
    return -1;
}

static void book_key_for(const char *file, char *key, size_t len)
{
    struct sonora_book *b = sonora_book_for_file(file);
    if (b)
    {
        strlcpy(key, b->path, len);
        return;
    }
    /* not in the library (an .m4b elsewhere): the file is the book */
    strlcpy(key, file, len);
}

/* --------------------------- progress record -------------------------- */

static void record(void)
{
    if (st.kind == SK_MUSIC || !st.key[0])
        return;
    struct sonora_progress *p = sonora_progress_get(st.key,
                                    st.kind == SK_BOOK ? SK_BOOK_C : SK_EPISODE_C);
    if (!p)
        return;
    strlcpy(p->cur, st.file, sizeof(p->cur));
    p->pos_ms = st.elapsed;
    if (st.kind == SK_BOOK && st.file_index >= 0 && st.nfiles > 0)
    {
        p->done_ms = st.start_ms[st.file_index] + st.elapsed;
        p->total_ms = st.start_ms[st.nfiles];
    }
    else
    {
        p->done_ms = st.elapsed;
        p->total_ms = st.length;
    }
    if (p->state == SP_NEW)
        p->state = SP_PLAYING;
    p->when = (uint32_t)sonora_now();
}

void sonora_save_now(void)
{
    record();
    sonora_progress_save();
    st.last_save = current_tick;
}

void sonora_mark(const char *key, char kind, char state)
{
    struct sonora_progress *p = sonora_progress_get(key, kind);
    if (!p)
        return;
    p->state = state;
    if (state == SP_DONE)
        p->done_ms = p->total_ms;
    else if (state == SP_NEW)
    {
        p->done_ms = p->pos_ms = 0;
        p->cur[0] = '\0';
    }
    p->when = (uint32_t)sonora_now();
    if (!strcmp(key, st.key))
        st.done_flagged = state == SP_DONE;
    sonora_progress_save();
}

/* ------------------------------ seeking ------------------------------- */

/* the same two steps Rockbox's own cue-sheet and pause-rewind code use */
void sonora_seek(long ms)
{
    if (!(audio_status() & AUDIO_STATUS_PLAY))
        return;
    audio_pre_ff_rewind();
    audio_ff_rewind(ms > 0 ? ms : 0);
}

/* ------------------------------ chapters ------------------------------ */

static void load_chapters(const char *file)
{
    if (!strcmp(st.ch_file, file))
        return;
    strlcpy(st.ch_file, file, sizeof(st.ch_file));
    int src;
    st.nch = sonora_chapters_read(file, st.ch, SONORA_MAX_CHAPTERS, &src);
}

int sonora_current_chapters(struct sonora_chapter **ch)
{
    struct mp3entry *id3 = audio_status() ? audio_current_track() : NULL;
    if (!id3 || !id3->path[0])
        return 0;
    load_chapters(id3->path);
    *ch = st.ch;
    return st.nch;
}

bool sonora_seek_chapter(int i)
{
    if (i < 0 || i >= st.nch || !(audio_status() & AUDIO_STATUS_PLAY))
        return false;
    sonora_seek(st.ch[i].start_ms);
    return true;
}

static int chapter_at(uint32_t ms)
{
    int c = 0;
    for (int i = 0; i < st.nch; i++)
        if (st.ch[i].start_ms <= ms)
            c = i;
    return c;
}

/* --------------------------- track changes ---------------------------- */

static void track_changed(struct mp3entry *id3)
{
    enum sonora_kind k = sonora_kind_of_path(id3->path);
    enum sonora_kind prev = st.kind;
    char key[MAX_PATH] = "";

    if (prev != SK_MUSIC && st.key[0])
        sonora_save_now(); /* the file that just ended / was left */

    if (k == SK_BOOK)
    {
        book_key_for(id3->path, key, sizeof(key));
        load_book(key);
        st.prev_index = (!strcmp(key, st.key)) ? st.file_index : -1;
        st.file_index = file_index_of(id3->path);
        struct sonora_progress *p = sonora_progress_find(key);
        /* repeat-all would start the book again after its last file */
        if (p && p->state == SP_DONE && st.prev_index == st.nfiles - 1 &&
            st.file_index == 0 && st.nfiles > 1)
        {
            audio_stop();
            k = SK_MUSIC;
            key[0] = '\0';
        }
    }
    else if (k == SK_EPISODE)
        strlcpy(key, id3->path, sizeof(key));

    if (strcmp(key, st.key))
        st.done_flagged = false;
    st.kind = k;
    strlcpy(st.key, key, sizeof(st.key));
    strlcpy(st.file, id3->path, sizeof(st.file));
    st.elapsed = id3->elapsed;
    st.length = id3->length;

    if (k != SK_MUSIC)
    {
        if (prev == SK_MUSIC)
            st.music_speed = dsp_timestretch_available() ? dsp_get_timestretch()
                                                         : PITCH_SPEED_100;
        apply_speed((int32_t)sonora_item_speed(key, k));
        st.speed_touched = true;
        load_chapters(id3->path);
        struct sonora_progress *p = sonora_progress_get(key, k == SK_BOOK ? SK_BOOK_C
                                                                         : SK_EPISODE_C);
        if (p)
        {
            if (!p->title[0])
            {
                struct sonora_book *b = k == SK_BOOK ? sonora_book_find(key) : NULL;
                if (b)
                    strlcpy(p->title, b->title, sizeof(p->title));
                else if (id3->title && id3->title[0])
                    sonora_clean(p->title, id3->title, sizeof(p->title));
                else
                    sonora_basename_noext(key, p->title, sizeof(p->title));
            }
            if (p->state == SP_DONE && !st.done_flagged)
                p->state = SP_PLAYING; /* listening again */
        }
        sonora_save_now();
    }
    else if (prev != SK_MUSIC && st.speed_touched)
    {
        apply_speed(st.music_speed);
        st.speed_touched = false;
    }
}

static void check_finished(void)
{
    if (st.done_flagged || !st.length)
        return;
    bool done = false;
    if (st.kind == SK_BOOK)
        done = (st.file_index < 0 || st.file_index == st.nfiles - 1) &&
               st.elapsed + 3000 >= st.length;
    else if (st.kind == SK_EPISODE)
        done = st.elapsed >= st.length / 100 * 95;
    if (done)
    {
        record();
        sonora_mark(st.key, st.kind == SK_BOOK ? SK_BOOK_C : SK_EPISODE_C, SP_DONE);
        st.done_flagged = true;
    }
}

/* -------------------------------- sleep ------------------------------- */

void sonora_sleep_set(int minutes)
{
    st.sleep_mode = minutes;
    if (minutes > 0)
        st.sleep_deadline = current_tick + (long)minutes * 60 * HZ;
}

int sonora_sleep_mode(void) { return st.sleep_mode; }

int sonora_sleep_left(void)
{
    if (st.sleep_mode > 0)
    {
        long left = st.sleep_deadline - current_tick;
        return left > 0 ? (int)(left / HZ) : 0;
    }
    return st.sleep_mode;
}

static void fade_and_pause(void)
{
    int vol = global_status.volume, lo = sound_min(SOUND_VOLUME);
    for (int i = 1; i <= 15 && (audio_status() & AUDIO_STATUS_PLAY); i++)
    {
        sound_set_volume(vol - (vol - lo) * i / 15);
        sleep(HZ / 5);
    }
    audio_pause();
    sleep(HZ / 10);
    sound_set_volume(vol);
    st.sleep_mode = 0;
    if (st.kind != SK_MUSIC)
        sonora_save_now();
}

static void check_sleep(bool playing)
{
    if (!st.sleep_mode || !playing)
        return;
    bool fire = false;
    /* the fade takes ~3 s of real time: start it early enough that the
     * pause lands on the boundary, at whatever speed is playing */
    uint32_t speed = dsp_timestretch_available() ? (uint32_t)dsp_get_timestretch()
                                                 : PITCH_SPEED_100;
    uint32_t margin = 500 + 3000u * speed / PITCH_SPEED_100;
    if (st.sleep_mode > 0)
        fire = TIME_AFTER(current_tick, st.sleep_deadline);
    else if (st.sleep_mode == SONORA_SLEEP_FILE)
        fire = st.length && st.elapsed + margin >= st.length;
    else if (st.sleep_mode == SONORA_SLEEP_CHAPTER)
    {
        uint32_t end = st.length;
        if (st.nch >= 2 && !strcmp(st.ch_file, st.file))
        {
            int c = chapter_at(st.elapsed);
            if (c + 1 < st.nch)
                end = st.ch[c + 1].start_ms;
        }
        fire = end && st.elapsed + margin >= end;
    }
    if (fire)
        fade_and_pause();
}

/* -------------------------------- tick -------------------------------- */

void sonora_tick(void)
{
    static long next;
    if (!inited || TIME_BEFORE(current_tick, next))
        return;
    next = current_tick + HZ / 4;

    int status = audio_status();
    struct mp3entry *id3 = status ? audio_current_track() : NULL;
    if (id3 && id3->path[0] && (st.track_changed || strcmp(id3->path, st.file)))
    {
        st.track_changed = false;
        track_changed(id3);
    }
    else if (id3 && id3->path[0])
    {
        st.elapsed = id3->elapsed;
        st.length = id3->length;
    }
    bool playing = (status & AUDIO_STATUS_PLAY) && !(status & AUDIO_STATUS_PAUSE);
    bool paused = (status & AUDIO_STATUS_PAUSE) != 0;

    if (st.kind != SK_MUSIC)
    {
        if (st.was_playing && !playing)
        {   /* paused or stopped: save right away */
            sonora_save_now();
            st.pause_tick = current_tick;
        }
        else if (playing && !st.was_playing && st.was_paused && st.pause_tick &&
                 global_settings.sonora_rewind > 0 &&
                 current_tick - st.pause_tick >= 60 * HZ)
        {   /* back after a long pause: a few seconds of context */
            long to = (long)st.elapsed - global_settings.sonora_rewind * 1000L;
            sonora_seek(to > 0 ? to : 0);
            st.pause_tick = 0;
        }
        if (playing)
        {
            check_finished();
            if (current_tick - st.last_save >= global_settings.sonora_checkpoint * HZ)
                sonora_save_now();
        }
        if (!status && st.was_playing)
        {   /* stopped: the item is no longer current */
            if (st.speed_touched)
            {
                apply_speed(st.music_speed);
                st.speed_touched = false;
            }
            st.kind = SK_MUSIC;
            st.key[0] = st.file[0] = '\0';
        }
    }
    check_sleep(playing);
    st.was_playing = playing;
    st.was_paused = paused;
}

/* ------------------------------ starting ------------------------------ */

static void save_music_queue(void)
{
    if (sonora_current_kind() != SK_MUSIC || !(audio_status() & AUDIO_STATUS_PLAY))
        return;
    int n = playlist_amount();
    if (n <= 0)
        return;
    char path[MAX_PATH], tmp[MAX_PATH];
    sonora_data_path("music-queue.m3u8", path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    fdprintf(fd, "#EXTM3U\n");
    int first = playlist_get_first_index(NULL);
    struct playlist_track_info info;
    for (int k = 0; k < n; k++)
        if (playlist_get_track_info(NULL, (first + k) % n, &info) == 0)
            fdprintf(fd, "%s\n", info.filename);
    close(fd);
    remove(path);
    rename(tmp, path);
    struct mp3entry *id3 = audio_current_track();
    struct sonora_progress *p = sonora_progress_get(MUSIC_KEY, SK_MUSIC_C);
    if (!p)
        return;
    p->index = playlist_get_display_index() - 1;
    p->pos_ms = id3 ? id3->elapsed : 0;
    p->state = SP_PLAYING;
    p->when = (uint32_t)sonora_now();
    if (id3 && id3->title)
        sonora_clean(p->title, id3->title, sizeof(p->title));
    sonora_progress_save();
}

bool sonora_music_saved(void)
{
    struct sonora_progress *p = sonora_progress_find(MUSIC_KEY);
    char path[MAX_PATH];
    sonora_data_path("music-queue.m3u8", path, sizeof(path));
    return p && p->state == SP_PLAYING && file_exists(path);
}

bool sonora_resume_music(void)
{
    struct sonora_progress *p = sonora_progress_find(MUSIC_KEY);
    if (!p)
        return false;
    if (playlist_create(sonora_data_dir(), "music-queue.m3u8") < 0 || playlist_amount() <= 0)
        return false;
    int idx = p->index;
    if (idx < 0 || idx >= playlist_amount())
        idx = 0;
    playlist_start(idx, p->pos_ms, 0);
    p->state = SP_DONE; /* used: Continue stops offering it */
    sonora_progress_save();
    return true;
}

static void start_queue(char (*files)[MAX_PATH], int n, int index, uint32_t pos)
{
    playlist_create(NULL, NULL);
    struct playlist_insert_context ctx;
    if (playlist_insert_context_create(NULL, &ctx, PLAYLIST_INSERT_LAST, false, false) >= 0)
    {
        for (int i = 0; i < n; i++)
            playlist_insert_context_add(&ctx, files[i]);
        playlist_insert_context_release(&ctx);
    }
    if (playlist_amount() > 0)
        playlist_start(MAX(0, MIN(index, playlist_amount() - 1)), pos, 0);
}

static uint32_t rewound(const struct sonora_progress *p, uint32_t pos)
{
    unsigned long away = sonora_now() - p->when;
    uint32_t back = (uint32_t)global_settings.sonora_rewind * 1000;
    if (p->when && away >= 60 && back)
        return pos > back ? pos - back : 0;
    return pos;
}

bool sonora_play_book(struct sonora_book *b, int how, const char *file, uint32_t pos_ms)
{
    if (!b)
        return false;
    if (st.kind != SK_MUSIC && st.key[0])
        sonora_save_now(); /* the item being left, before its timeline goes */
    save_music_queue();
    st.book_key[0] = '\0'; /* reload: the folder may have changed */
    int n = load_book(b->path);
    if (n <= 0)
        return false;
    struct sonora_progress *p = sonora_progress_get(b->path, SK_BOOK_C);
    int index = 0;
    uint32_t pos = 0;
    if (how == SONORA_AT && file)
    {
        index = MAX(0, file_index_of(file));
        pos = pos_ms;
    }
    else if (how == SONORA_RESUME && p && p->state == SP_PLAYING && p->cur[0])
    {
        int i = file_index_of(p->cur);
        if (i >= 0)
        {
            index = i;
            pos = rewound(p, p->pos_ms);
        }
    }
    if (p)
    {
        if (p->state == SP_DONE || how == SONORA_FROM_START)
            p->state = SP_PLAYING;
        strlcpy(p->title, b->title, sizeof(p->title));
        p->total_ms = st.start_ms[n];
        p->when = (uint32_t)sonora_now();
    }
    st.done_flagged = false;
    /* speed before the first sample plays */
    if (st.kind == SK_MUSIC)
        st.music_speed = dsp_timestretch_available() ? dsp_get_timestretch() : PITCH_SPEED_100;
    apply_speed((int32_t)sonora_item_speed(b->path, SK_BOOK));
    st.speed_touched = true;
    start_queue(book_files, n, index, pos);
    return playlist_amount() > 0;
}

bool sonora_play_episode_at(const char *path, int how, uint32_t at_ms)
{
    if (st.kind != SK_MUSIC && st.key[0])
        sonora_save_now(); /* the item being left */
    save_music_queue();
    struct sonora_progress *p = sonora_progress_get(path, SK_EPISODE_C);
    uint32_t pos = 0;
    if (how == SONORA_RESUME && p && p->state == SP_PLAYING)
        pos = rewound(p, p->pos_ms);
    else if (how == SONORA_AT)
        pos = at_ms;
    if (p && (p->state == SP_DONE || how == SONORA_FROM_START))
        p->state = SP_PLAYING;
    static char files[64][MAX_PATH];
    int n = 0;
    strlcpy(files[n++], path, MAX_PATH);
    if (global_settings.sonora_autonext && under(path, podcasts_root))
    {   /* the newer unplayed episodes of the same show follow */
        char folder[96];
        strlcpy(folder, path + strlen(podcasts_root) + 1, sizeof(folder));
        char *slash = strchr(folder, '/');
        if (slash) *slash = '\0';
        static struct sonora_episode eps[200];
        int k = sonora_show_episodes(folder, eps, 200);
        int me = -1;
        for (int i = 0; i < k; i++)
            if (!strcmp(eps[i].path, path))
                me = i;
        for (int i = me - 1; i >= 0 && n < 64; i--)
        {   /* newest first in the list: walk towards newer */
            struct sonora_progress *q = sonora_progress_find(eps[i].path);
            if (!q || q->state != SP_DONE)
                strlcpy(files[n++], eps[i].path, MAX_PATH);
        }
    }
    st.done_flagged = false;
    if (st.kind == SK_MUSIC)
        st.music_speed = dsp_timestretch_available() ? dsp_get_timestretch() : PITCH_SPEED_100;
    apply_speed((int32_t)sonora_item_speed(path, SK_EPISODE));
    st.speed_touched = true;
    start_queue(files, n, 0, pos);
    return playlist_amount() > 0;
}

bool sonora_play_episode(const char *path, int how)
{
    return sonora_play_episode_at(path, how, 0);
}

/* --------------------------- Now Playing keys -------------------------- */

static void hop_seconds(int dir, int secs)
{
    struct mp3entry *id3 = audio_current_track();
    if (!id3)
        return;
    long to = (long)id3->elapsed + dir * secs * 1000L;
    if (to < 0)
        to = 0;
    if (id3->length && to > (long)id3->length - 1000)
        to = (long)id3->length - 1000;
    sonora_seek(to);
}

bool sonora_wps_skip(int dir)
{
    struct mp3entry *id3 = audio_status() ? audio_current_track() : NULL;
    if (!id3)
        return false;
    enum sonora_kind k = sonora_kind_of_path(id3->path);
    if (k == SK_MUSIC)
        return false;
    if (k == SK_EPISODE)
    {
        hop_seconds(dir, dir < 0 ? global_settings.sonora_pod_back
                                 : global_settings.sonora_pod_fwd);
        return true;
    }
    load_chapters(id3->path);
    if (st.nch >= 2)
    {
        uint32_t now = id3->elapsed;
        int c = chapter_at(now);
        if (dir < 0)
        {
            if (now - st.ch[c].start_ms > 3000 || c == 0)
            {
                if (c == 0 && now < 3000 && st.nfiles > 1 && st.file_index > 0)
                    return false; /* to the previous file */
                sonora_seek(st.ch[c].start_ms);
            }
            else
                sonora_seek(st.ch[c - 1].start_ms);
            return true;
        }
        if (c + 1 < st.nch)
        {
            sonora_seek(st.ch[c + 1].start_ms);
            return true;
        }
        if (st.nfiles > 1 && st.file_index >= 0 && st.file_index + 1 < st.nfiles)
            return false; /* the next file is the next chapter */
        splash(HZ, ID2P(LANG_SONORA_LAST_CHAPTER));
        return true;
    }
    if (st.nfiles > 1)
        return false; /* one file per chapter: plain track skips */
    hop_seconds(dir, dir < 0 ? global_settings.sonora_book_back
                             : global_settings.sonora_book_fwd);
    return true;
}

bool sonora_wps_hotkey(void)
{
    enum sonora_kind k = sonora_current_kind();
    if (k == SK_BOOK)
    {
        sonora_bookmark_add_current();
        return true;
    }
    if (k == SK_EPISODE)
    {
        sonora_mark_current();
        return true;
    }
    return false;
}
