/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - your listening, always up to date ("Wrapped" that
 * never waits for December).
 *
 * Every song you listen to is written to /.rockbox/wheel-history.tsv
 * (on the player only). From it: minutes, plays, top songs / artists /
 * albums for this month, this year, any past month or year, or ever,
 * your favourite hour, and a card-style summary.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "action.h"
#include "list.h"
#include "splash.h"
#include "file.h"
#include "misc.h"
#include "audio.h"
#include "appevents.h"
#include "metadata.h"
#include "playlist.h"
#include "settings.h"
#include "timefuncs.h"
#include "font.h"
#include "viewport.h"
#include "screen_access.h"
#include "rbpaths.h"
#include "root_menu.h"
#include "string-extra.h"
#include "wheel_art.h"
#include "wheel_stats.h"
#include "wheel_typo.h"

/* ============================ the history ============================ */

static unsigned long now_utc(void)
{
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    return (unsigned long)time(NULL);
#elif CONFIG_RTC
    struct tm *tm = get_time(); /* native players: the RTC holds local time */
    return tm ? (unsigned long)mktime(tm) : 0;
#else
    return 0;
#endif
}

/* "YYYYMMDDHH" in local time for the moment the song started */
static void when_local(unsigned long started, char out[11])
{
    struct tm lt;
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    time_t t = (time_t)started;
    localtime_r(&t, &lt);
#elif CONFIG_RTC
    time_t t = (time_t)started;
    gmtime_r(&t, &lt); /* already local seconds, see now_utc() */
#else
    (void)started;
    strlcpy(out, "0000000000", 11);
    return;
#endif
    snprintf(out, 11, "%04d%02d%02d%02d", lt.tm_year + 1900, lt.tm_mon + 1,
             lt.tm_mday, lt.tm_hour);
}

static void clean_copy(char *dst, const char *src, size_t len)
{
    size_t i = 0;
    for (; src && *src && i + 1 < len; src++)
        dst[i++] = (*src == '\t' || *src == '\n' || *src == '\r') ? ' ' : *src;
    dst[i] = '\0';
}

static void track_finished(unsigned short id, void *param)
{
    (void)id;
    struct track_event *te = (struct track_event *)param;
    struct mp3entry *id3 = te->id3;
    if (!id3 || (te->flags & TEF_REWIND) || !id3->path[0])
        return;
    unsigned long played = id3->elapsed / 1000, len = id3->length / 1000;
    if (played < 10)
        return; /* a skip, not a listen */
    bool listened = played >= 30 || (len && played * 2 >= len);
    unsigned long started = now_utc() - played;
    char when[11], artist[96], album[96], title[128];
    when_local(started, when);
    clean_copy(artist, id3->artist ? id3->artist : id3->albumartist, sizeof(artist));
    clean_copy(album, id3->album, sizeof(album));
    if (id3->title)
        clean_copy(title, id3->title, sizeof(title));
    else
    {
        const char *slash = strrchr(id3->path, '/');
        clean_copy(title, slash ? slash + 1 : id3->path, sizeof(title));
    }
    int fd = open(WHEEL_HISTORY_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    fdprintf(fd, "%lu\t%s\t%lu\t%lu\t%c\t%s\t%s\t%s\t%s\n", started, when, played, len,
             listened ? 'L' : 'S', artist, album, title, id3->path);
    close(fd);
}

void wheel_stats_init(void)
{
    add_event(PLAYBACK_EVENT_TRACK_FINISH, track_finished);
}

/* --------------------------- reading it back -------------------------- */

#define F_TS 0
#define F_WHEN 1
#define F_PLAYED 2
#define F_LEN 3
#define F_FLAG 4
#define F_ARTIST 5
#define F_ALBUM 6
#define F_TITLE 7
#define F_PATH 8
#define N_FIELDS 9

/* split in place; returns false for malformed lines */
bool wheel_history_split(char *line, char *f[9])
{
    int n = 0;
    f[n++] = line;
    for (char *p = line; *p && n < N_FIELDS; p++)
        if (*p == '\t')
        {
            *p = '\0';
            f[n++] = p + 1;
        }
    if (n < N_FIELDS)
        return false;
    char *nl = strchr(f[F_PATH], '\n');
    if (nl) *nl = '\0';
    return true;
}

/* period: "" = ever, "YYYY" = a year, "YYYYMM" = a month */
static bool in_period(const char *when, const char *period)
{
    return !period[0] || !strncmp(when, period, strlen(period));
}

static unsigned long hash_str(const char *s, unsigned long h)
{
    while (*s)
    {
        unsigned char c = *s++;
        if (c >= 'A' && c <= 'Z') c += 32; /* "the beatles" == "The Beatles" */
        h = (h ^ c) * 16777619u;
    }
    return h;
}

#define TABLE_SIZE 8192 /* distinct keys per kind; plenty for a player */
struct agg { unsigned long hash; unsigned plays, secs; long off; };
static struct agg table[TABLE_SIZE];

struct stat_item {
    char name[96];
    char sub[96];
    char path[MAX_PATH];
    unsigned plays, secs;
};
#define TOP_N 50
static struct stat_item top[TOP_N];
static int n_top;

struct stat_summary {
    unsigned plays, secs, songs, artists;
    int hours[24];
};
static struct stat_summary sum;

static long hist_read_line_at(long off, char *buf, int len)
{
    int fd = open(WHEEL_HISTORY_FILE, O_RDONLY);
    if (fd < 0) return -1;
    lseek(fd, off, SEEK_SET);
    int n = read_line(fd, buf, len);
    close(fd);
    return n;
}

static struct agg *find_slot(unsigned long h)
{
    unsigned i = h & (TABLE_SIZE - 1);
    for (int probe = 0; probe < TABLE_SIZE; probe++, i = (i + 1) & (TABLE_SIZE - 1))
        if (table[i].hash == h || table[i].hash == 0)
            return &table[i];
    return NULL; /* full */
}

/* scan the history for one period; kind decides what is ranked.
 * Also fills the summary (only for the song pass, which sees everything). */
static void compute(const char *period, enum wheel_stat_kind kind)
{
    char line[MAX_PATH + 512];
    char *f[N_FIELDS];
    memset(table, 0, sizeof(table));
    memset(&sum, 0, sizeof(sum));
    n_top = 0;
    int fd = open(WHEEL_HISTORY_FILE, O_RDONLY);
    if (fd < 0)
        return;
    long off = 0;
    int n;
    unsigned artists_seen = 0;
    static unsigned long artist_set[2048];
    memset(artist_set, 0, sizeof(artist_set));
    while ((n = read_line(fd, line, sizeof(line))) > 0)
    {
        long this_off = off;
        off = lseek(fd, 0, SEEK_CUR);
        if (!wheel_history_split(line, f) || !in_period(f[F_WHEN], period))
            continue;
        unsigned secs = atoi(f[F_PLAYED]);
        bool listened = f[F_FLAG][0] == 'L';
        sum.secs += secs;
        if (listened)
        {
            sum.plays++;
            int hr = atoi(f[F_WHEN] + 8);
            if (hr >= 0 && hr < 24) sum.hours[hr]++;
        }
        /* distinct artists (small open set) */
        unsigned long ah = hash_str(f[F_ARTIST], 2166136261u) | 1;
        for (unsigned i = ah & 2047, k = 0; k < 2048; k++, i = (i + 1) & 2047)
        {
            if (artist_set[i] == ah) break;
            if (!artist_set[i]) { artist_set[i] = ah; artists_seen++; break; }
        }
        unsigned long h;
        switch (kind)
        {
            case WHEEL_STAT_ARTISTS: h = ah; break;
            case WHEEL_STAT_ALBUMS:
                h = hash_str(f[F_ALBUM], hash_str(f[F_ARTIST], 2166136261u)) | 1; break;
            default: h = hash_str(f[F_PATH], 2166136261u) | 1; break;
        }
        struct agg *a = find_slot(h);
        if (!a) continue;
        if (!a->hash)
        {
            a->hash = h;
            a->off = this_off;
            if (kind == WHEEL_STAT_SONGS) sum.songs++;
        }
        a->secs += secs;
        if (listened) a->plays++;
    }
    close(fd);
    sum.artists = artists_seen;
    if (kind != WHEEL_STAT_SONGS)
    {   /* songs count needs its own pass; cheap enough to skip here */
        sum.songs = 0;
    }

    /* top N by plays, then by time */
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        struct agg *a = &table[i];
        if (!a->hash || (!a->plays && kind != WHEEL_STAT_SONGS)) continue;
        int pos = n_top;
        while (pos > 0 && (top[pos - 1].plays < a->plays ||
               (top[pos - 1].plays == a->plays && top[pos - 1].secs < a->secs)))
            pos--;
        if (pos >= TOP_N) continue;
        int last = MIN(n_top, TOP_N - 1);
        memmove(&top[pos + 1], &top[pos], (last - pos) * sizeof(top[0]));
        top[pos].plays = a->plays;
        top[pos].secs = a->secs;
        top[pos].name[0] = (char)(a->off & 0x7f); /* placeholder, filled below */
        top[pos].path[0] = 0;
        /* stash the offset in the struct temporarily */
        memcpy(top[pos].sub, &a->off, sizeof(long));
        if (n_top < TOP_N) n_top++;
    }
    for (int i = 0; i < n_top; i++)
    {
        long o;
        memcpy(&o, top[i].sub, sizeof(long));
        if (hist_read_line_at(o, line, sizeof(line)) <= 0 || !wheel_history_split(line, f))
        {
            strlcpy(top[i].name, "?", sizeof(top[i].name));
            top[i].sub[0] = 0;
            continue;
        }
        switch (kind)
        {
            case WHEEL_STAT_ARTISTS:
                strlcpy(top[i].name, f[F_ARTIST][0] ? f[F_ARTIST] : "?", sizeof(top[i].name));
                top[i].sub[0] = 0;
                break;
            case WHEEL_STAT_ALBUMS:
                strlcpy(top[i].name, f[F_ALBUM][0] ? f[F_ALBUM] : "?", sizeof(top[i].name));
                strlcpy(top[i].sub, f[F_ARTIST], sizeof(top[i].sub));
                break;
            default:
                strlcpy(top[i].name, f[F_TITLE], sizeof(top[i].name));
                strlcpy(top[i].sub, f[F_ARTIST], sizeof(top[i].sub));
                break;
        }
        strlcpy(top[i].path, f[F_PATH], sizeof(top[i].path));
    }
}

/* ============================== periods =============================== */

static char period[8] = ""; /* current selection */

static void this_month(char *out)
{
#if CONFIG_RTC
    struct tm *tm = get_time();
    snprintf(out, 8, "%04d%02d", tm->tm_year + 1900, tm->tm_mon + 1);
#else
    out[0] = 0;
#endif
}

static void period_name(const char *p, char *buf, size_t len)
{
    char cur[8];
    this_month(cur);
    if (!p[0])
        strlcpy(buf, str(LANG_WHEEL_STATS_ALLTIME), len);
    else if (!strcmp(p, cur))
        strlcpy(buf, str(LANG_WHEEL_STATS_THISMONTH), len);
    else if (strlen(p) == 4 && !strncmp(p, cur, 4))
        strlcpy(buf, str(LANG_WHEEL_STATS_THISYEAR), len);
    else if (strlen(p) == 4)
        strlcpy(buf, p, len);
    else
    {
        int m = atoi(p + 4);
        snprintf(buf, len, "%s %.4s", m >= 1 && m <= 12 ? (const char *)str(LANG_MONTH_JANUARY + m - 1) : "?", p);
    }
}

/* months / years that have history, newest first */
#define MAX_PERIODS 64
static char periods[MAX_PERIODS][8];
static unsigned period_secs[MAX_PERIODS];
static int n_periods;

static void collect_periods(void)
{
    char line[MAX_PATH + 512];
    char *f[N_FIELDS];
    n_periods = 0;
    int fd = open(WHEEL_HISTORY_FILE, O_RDONLY);
    if (fd < 0) return;
    while (read_line(fd, line, sizeof(line)) > 0)
    {
        if (!wheel_history_split(line, f)) continue;
        char ym[8], y[8];
        snprintf(ym, sizeof(ym), "%.6s", f[F_WHEN]);
        snprintf(y, sizeof(y), "%.4s", f[F_WHEN]);
        unsigned secs = atoi(f[F_PLAYED]);
        for (int pass = 0; pass < 2; pass++)
        {
            const char *k = pass ? y : ym;
            int i;
            for (i = 0; i < n_periods; i++)
                if (!strcmp(periods[i], k)) break;
            if (i == n_periods)
            {
                if (n_periods >= MAX_PERIODS) continue;
                strlcpy(periods[n_periods], k, 8);
                period_secs[n_periods] = 0;
                n_periods++;
            }
            period_secs[i] += secs;
        }
    }
    close(fd);
    /* newest first; a year sorts right after its months */
    for (int i = 1; i < n_periods; i++)
        for (int j = i; j > 0; j--)
        {
            char a[8], b[8];
            snprintf(a, 8, "%-6s", periods[j]);
            snprintf(b, 8, "%-6s", periods[j - 1]);
            /* "2026  " vs "202610": years go after their months */
            for (int k = 4; k < 6; k++) { if (a[k] == ' ') a[k] = '/'; if (b[k] == ' ') b[k] = '/'; }
            if (strcmp(a, b) > 0)
            {
                char t[8]; unsigned ts;
                strcpy(t, periods[j]); strcpy(periods[j], periods[j - 1]); strcpy(periods[j - 1], t);
                ts = period_secs[j]; period_secs[j] = period_secs[j - 1]; period_secs[j - 1] = ts;
            }
            else break;
        }
}

static void minutes_text(unsigned secs, char *buf, size_t len)
{
    unsigned m = secs / 60;
    if (m >= 1000)
        snprintf(buf, len, "%u.%03u min", m / 1000, m % 1000);
    else
        snprintf(buf, len, "%u min", m);
}

/* ============================ ranking lists =========================== */

static enum wheel_stat_kind rank_kind;

static const char *rank_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (n_top == 0)
        return str(LANG_WHEEL_STATS_EMPTY);
    struct stat_item *s = &top[item];
    if (s->sub[0])
        snprintf(buf, len, "%d. %s · %s" LIST_VALUE_SEP_STR "%u", item + 1, s->name, s->sub, s->plays);
    else
        snprintf(buf, len, "%d. %s" LIST_VALUE_SEP_STR "%u", item + 1, s->name, s->plays);
    return buf;
}

static void play_paths_from_history(const char *artist_match, const char *album_match)
{
    char line[MAX_PATH + 512];
    char *f[N_FIELDS];
    if (playlist_create(NULL, NULL) < 0)
        return;
    int fd = open(WHEEL_HISTORY_FILE, O_RDONLY);
    if (fd < 0) return;
    /* each file once */
    static unsigned long seen[1024];
    int n_seen = 0;
    while (read_line(fd, line, sizeof(line)) > 0)
    {
        if (!wheel_history_split(line, f)) continue;
        if (artist_match && strcasecmp(f[F_ARTIST], artist_match)) continue;
        if (album_match && strcasecmp(f[F_ALBUM], album_match)) continue;
        unsigned long h = hash_str(f[F_PATH], 2166136261u);
        int k;
        for (k = 0; k < n_seen; k++) if (seen[k] == h) break;
        if (k < n_seen || n_seen >= 1024) continue;
        seen[n_seen++] = h;
        if (file_exists(f[F_PATH]))
            playlist_insert_track(NULL, f[F_PATH], PLAYLIST_INSERT_LAST, false, false);
    }
    close(fd);
    playlist_sync(NULL);
}

static int rank_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK || n_top == 0)
        return action;
    struct stat_item *s = &top[gui_synclist_get_sel_pos(lists)];
    switch (rank_kind)
    {
        case WHEEL_STAT_SONGS:
            if (playlist_create(NULL, NULL) >= 0 &&
                playlist_insert_track(NULL, s->path, PLAYLIST_INSERT_LAST, false, true) >= 0)
                playlist_start(0, 0, 0);
            break;
        case WHEEL_STAT_ARTISTS:
            play_paths_from_history(s->name, NULL);
            if (playlist_amount() > 0) { playlist_shuffle(current_tick, -1); playlist_start(0, 0, 0); }
            break;
        case WHEEL_STAT_ALBUMS:
        {
            char dir[MAX_PATH];
            strlcpy(dir, s->path, sizeof(dir));
            char *slash = strrchr(dir, '/');
            if (slash) *slash = 0;
            if (playlist_create(NULL, NULL) >= 0)
            {
                playlist_insert_directory(NULL, dir, PLAYLIST_INSERT_LAST, false, false, NULL);
                if (playlist_amount() > 0) playlist_start(0, 0, 0);
            }
            break;
        }
    }
    if (audio_status() & AUDIO_STATUS_PLAY)
        return ACTION_STD_CANCEL; /* the caller jumps to Now Playing */
    return ACTION_REDRAW;
}

static bool show_ranking(enum wheel_stat_kind kind, const char *title)
{
    struct simplelist_info info;
    splash(0, ID2P(LANG_WAIT));
    compute(period, kind);
    rank_kind = kind;
    char t[64], p[32];
    period_name(period, p, sizeof(p));
    snprintf(t, sizeof(t), "%s · %s", title, p);
    simplelist_info_init(&info, t, MAX(1, n_top), NULL);
    info.get_name = rank_get_name;
    info.action_callback = rank_action;
    simplelist_show_list(&info);
    return audio_status() & AUDIO_STATUS_PLAY && info.selection >= 0 && n_top > 0;
}

/* =========================== summary cards ============================ */

WHEEL_ART_INIT(card_art, 150);

static unsigned mix(unsigned a, unsigned b, int pct)
{
    return LCD_RGBPACK((RGB_UNPACK_RED(a) * pct + RGB_UNPACK_RED(b) * (100 - pct)) / 100,
                       (RGB_UNPACK_GREEN(a) * pct + RGB_UNPACK_GREEN(b) * (100 - pct)) / 100,
                       (RGB_UNPACK_BLUE(a) * pct + RGB_UNPACK_BLUE(b) * (100 - pct)) / 100);
}

static int card_font_big = -1, card_font_mid = -1, card_font_small = -1;

static void card_text(struct screen *d, struct viewport *vp, int font, unsigned col,
                      int x, int y, const char *s)
{
    vp->font = font >= 0 ? font : FONT_UI;
    d->set_foreground(col);
    d->putsxy(x, y, s);
}

/* page 0: big minutes; 1: top songs; 2: top artists; 3: your hour */
static void draw_card(int page, int pages, const char *pname,
                      struct stat_item *songs, int n_songs,
                      struct stat_item *artists, int n_artists, int fav_hour)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    unsigned white = LCD_RGBPACK(255, 255, 255);
    unsigned bg = card_art.valid ? card_art.tint : (unsigned)global_settings.lse_color;
    if (page % 2) bg = mix(bg, LCD_RGBPACK(0, 0, 0), 70);
    d->set_background(bg);
    d->set_drawmode(DRMODE_SOLID);
    d->clear_viewport();
    d->set_drawmode(DRMODE_FG);
    char buf[160];

    card_text(d, &vp, card_font_small, mix(white, bg, 70), 28, 22, pname);
    switch (page)
    {
        case 0:
            card_text(d, &vp, card_font_mid, white, 28, 70, str(LANG_WHEEL_STATS_YOU_LISTENED));
            {   /* the big font only has digits and capitals: the unit goes
                 * next to the number in the normal font */
                unsigned m = sum.secs / 60;
                if (m >= 1000)
                    snprintf(buf, sizeof(buf), "%u.%03u", m / 1000, m % 1000);
                else
                    snprintf(buf, sizeof(buf), "%u", m);
                card_text(d, &vp, card_font_big, white, 28, 112, buf);
                int w, h;
                d->getstringsize(buf, &w, &h);
                card_text(d, &vp, card_font_mid, white, 28 + w + 10, 112 + h - 40, "min");
            }
            snprintf(buf, sizeof(buf), "%u %s · %u %s", sum.plays, str(LANG_WHEEL_STATS_PLAYS_LOWER),
                     sum.artists, str(LANG_WHEEL_STATS_ARTISTS_LOWER));
            card_text(d, &vp, card_font_small, mix(white, bg, 75), 28, 190, buf);
            if (n_songs)
            {
                snprintf(buf, sizeof(buf), "%s: %s", str(LANG_WHEEL_STATS_YOUR_SONG), songs[0].name);
                card_text(d, &vp, card_font_small, white, 28, 250, buf);
            }
            if (n_artists)
            {
                snprintf(buf, sizeof(buf), "%s: %s", str(LANG_WHEEL_STATS_YOUR_ARTIST), artists[0].name);
                card_text(d, &vp, card_font_small, white, 28, 280, buf);
            }
            break;
        case 1:
        case 2:
        {
            struct stat_item *it = page == 1 ? songs : artists;
            int n = MIN(5, page == 1 ? n_songs : n_artists);
            card_text(d, &vp, card_font_mid, white, 28, 56,
                      str(page == 1 ? LANG_WHEEL_STATS_TOP_SONGS : LANG_WHEEL_STATS_TOP_ARTISTS));
            int x = 28;
            if (page == 1 && card_art.valid)
            {
                wheel_art_draw(&card_art, d, LCD_WIDTH - 28 - 150, 104, 150);
            }
            for (int i = 0; i < n; i++)
            {
                int y = 104 + i * 46;
                snprintf(buf, sizeof(buf), "%d", i + 1);
                card_text(d, &vp, card_font_mid, mix(white, bg, 60), x, y, buf);
                card_text(d, &vp, card_font_small, white, x + 34, y + 2, it[i].name);
                char sub[120];
                if (it[i].sub[0])
                    strlcpy(sub, it[i].sub, sizeof(sub));
                else
                    snprintf(sub, sizeof(sub), "%u %s", it[i].plays, str(LANG_WHEEL_STATS_PLAYS_LOWER));
                card_text(d, &vp, card_font_small, mix(white, bg, 65), x + 34, y + 22, sub);
            }
            if (!n)
                card_text(d, &vp, card_font_small, white, 28, 120, str(LANG_WHEEL_STATS_EMPTY));
            break;
        }
        case 3:
            card_text(d, &vp, card_font_mid, white, 28, 70, str(LANG_WHEEL_STATS_YOUR_HOUR));
            if (fav_hour >= 0)
            {
                snprintf(buf, sizeof(buf), "%02d:00", fav_hour);
                card_text(d, &vp, card_font_big, white, 28, 112, buf);
                /* a small bar chart of the day */
                int maxh = 1;
                for (int h = 0; h < 24; h++) maxh = MAX(maxh, sum.hours[h]);
                d->set_drawmode(DRMODE_SOLID);
                for (int h = 0; h < 24; h++)
                {
                    int bh = sum.hours[h] * 90 / maxh;
                    d->set_foreground(h == fav_hour ? white : mix(white, bg, 45));
                    d->fillrect(28 + h * 18, 300 - bh, 12, MAX(bh, 2));
                }
            }
            break;
    }
    /* page dots */
    d->set_drawmode(DRMODE_SOLID);
    for (int i = 0; i < pages; i++)
    {
        d->set_foreground(i == page ? white : mix(white, bg, 40));
        d->fillrect(LCD_WIDTH / 2 - pages * 9 + i * 18, LCD_HEIGHT - 20, 8, 8);
    }
    d->update();
    d->set_viewport(last);
}

static void show_summary(void)
{
    static struct stat_item songs[5], artists[5];
    int n_songs, n_artists, fav = -1;
    splash(0, ID2P(LANG_WAIT));
    compute(period, WHEEL_STAT_ARTISTS);
    n_artists = MIN(n_top, 5);
    memcpy(artists, top, n_artists * sizeof(top[0]));
    compute(period, WHEEL_STAT_SONGS); /* last: keeps the full summary */
    n_songs = MIN(n_top, 5);
    memcpy(songs, top, n_songs * sizeof(top[0]));
    int best = 0;
    for (int h = 0; h < 24; h++)
        if (sum.hours[h] > best) { best = sum.hours[h]; fav = h; }
    if (n_songs)
        wheel_art_load(&card_art, songs[0].path);
    else
        card_art.valid = false;
    card_font_big = typo_font(TU_JUMBO);
    card_font_mid = typo_font(TU_DISPLAY_NP);
    card_font_small = typo_font(TU_META);

    char pname[32];
    period_name(period, pname, sizeof(pname));
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    int page = 0, pages = 4;
    while (1)
    {
        draw_card(page, pages, pname, songs, n_songs, artists, n_artists, fav);
        int a = get_action(CONTEXT_STD, TIMEOUT_BLOCK);
        if (a == ACTION_STD_NEXT || a == ACTION_STD_OK)
        {
            if (++page >= pages) break;
        }
        else if (a == ACTION_STD_PREV)
            page = MAX(0, page - 1);
        else if (a == ACTION_STD_CANCEL || a == ACTION_STD_MENU)
            break;
        else if (default_event_handler(a) == SYS_USB_CONNECTED)
            break;
    }
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, true);
}

/* ============================ main screen ============================= */

enum { ST_PERIOD, ST_SUMMARY, ST_MINUTES, ST_PLAYS, ST_SONGS, ST_ARTISTS, ST_ALBUMS,
       ST_MONTHS, ST_HISTORY, ST_COUNT };
static bool went_playing;

static const char *st_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    char v[48];
    switch (item)
    {
        case ST_PERIOD:
            period_name(period, v, sizeof(v));
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_STATS_PERIOD), v);
            return buf;
        case ST_SUMMARY: return str(LANG_WHEEL_STATS_SUMMARY);
        case ST_MINUTES:
            minutes_text(sum.secs, v, sizeof(v));
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_STATS_MINUTES), v);
            return buf;
        case ST_PLAYS:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%u", str(LANG_WHEEL_STATS_PLAYS), sum.plays);
            return buf;
        case ST_SONGS:   return str(LANG_WHEEL_STATS_TOP_SONGS);
        case ST_ARTISTS: return str(LANG_WHEEL_STATS_TOP_ARTISTS);
        case ST_ALBUMS:  return str(LANG_WHEEL_STATS_TOP_ALBUMS);
        case ST_MONTHS:  return str(LANG_WHEEL_STATS_BY_MONTH);
        case ST_HISTORY: return str(LANG_WHEEL_STATS_HISTORY);
    }
    return "";
}

static bool st_get_nav(int item, void *data)
{
    (void)data;
    return item != ST_MINUTES && item != ST_PLAYS;
}

/* the period picker: this month, this year, ever, then every month/year */
static const char *pp_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    char v[32];
    period_name(periods[item], buf, len);
    minutes_text(period_secs[item], v, sizeof(v));
    size_t l = strlen(buf);
    snprintf(buf + l, len - l, LIST_VALUE_SEP_STR "%s", v);
    return buf;
}

static void pick_period(bool months_only)
{
    collect_periods();
    /* "ever" on top */
    if (!months_only && n_periods < MAX_PERIODS)
    {
        memmove(&periods[1], &periods[0], n_periods * sizeof(periods[0]));
        memmove(&period_secs[1], &period_secs[0], n_periods * sizeof(period_secs[0]));
        periods[0][0] = 0;
        unsigned total = 0;
        for (int i = 1; i <= n_periods; i++) if (strlen(periods[i]) == 4) total += period_secs[i];
        period_secs[0] = total;
        n_periods++;
    }
    if (months_only)
    {   /* drop the years */
        int k = 0;
        for (int i = 0; i < n_periods; i++)
            if (strlen(periods[i]) == 6)
            {
                strcpy(periods[k], periods[i]);
                period_secs[k++] = period_secs[i];
            }
        n_periods = k;
    }
    if (n_periods == 0)
    {
        splash(HZ*2, ID2P(LANG_WHEEL_STATS_EMPTY));
        return;
    }
    struct simplelist_info info;
    simplelist_info_init(&info, str(months_only ? LANG_WHEEL_STATS_BY_MONTH : LANG_WHEEL_STATS_PERIOD),
                         n_periods, NULL);
    info.get_name = pp_get_name;
    simplelist_show_list(&info);
    if (info.selection >= 0)
        strlcpy(period, periods[info.selection], sizeof(period));
}

/* recent listens, newest first */
#define HIST_N 100
static struct { char title[96], artist[64], when[11]; } hist[HIST_N];
static int n_hist;

static void load_recent(void)
{
    char line[MAX_PATH + 512];
    char *f[N_FIELDS];
    n_hist = 0;
    int fd = open(WHEEL_HISTORY_FILE, O_RDONLY);
    if (fd < 0) return;
    /* only the tail of the file matters */
    long size = ffilesize(fd);
    if (size > 64 * 1024) { lseek(fd, size - 64 * 1024, SEEK_SET); read_line(fd, line, sizeof(line)); }
    int w = 0;
    while (read_line(fd, line, sizeof(line)) > 0)
    {
        if (!wheel_history_split(line, f) || f[F_FLAG][0] != 'L') continue;
        strlcpy(hist[w].title, f[F_TITLE], sizeof(hist[w].title));
        strlcpy(hist[w].artist, f[F_ARTIST], sizeof(hist[w].artist));
        strlcpy(hist[w].when, f[F_WHEN], sizeof(hist[w].when));
        w = (w + 1) % HIST_N;
        if (n_hist < HIST_N) n_hist++;
    }
    close(fd);
    /* reverse into newest-first order */
    static typeof(hist) tmp;
    for (int i = 0; i < n_hist; i++)
        memcpy(&tmp[i], &hist[(w - 1 - i + HIST_N) % HIST_N], sizeof(hist[0]));
    memcpy(hist, tmp, n_hist * sizeof(hist[0]));
}

static const char *hist_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (!n_hist) return str(LANG_WHEEL_STATS_EMPTY);
    const char *w = hist[item].when;
    snprintf(buf, len, "%s · %s" LIST_VALUE_SEP_STR "%.2s/%.2s %.2s:00", hist[item].title,
             hist[item].artist, w + 6, w + 4, w + 8);
    return buf;
}

static int st_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, st_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    switch (gui_synclist_get_sel_pos(lists))
    {
        case ST_PERIOD:
            pick_period(false);
            splash(0, ID2P(LANG_WAIT));
            compute(period, WHEEL_STAT_SONGS);
            break;
        case ST_SUMMARY:
            show_summary();
            compute(period, WHEEL_STAT_SONGS);
            break;
        case ST_SONGS:
            went_playing = show_ranking(WHEEL_STAT_SONGS, str(LANG_WHEEL_STATS_TOP_SONGS));
            break;
        case ST_ARTISTS:
            went_playing = show_ranking(WHEEL_STAT_ARTISTS, str(LANG_WHEEL_STATS_TOP_ARTISTS));
            break;
        case ST_ALBUMS:
            went_playing = show_ranking(WHEEL_STAT_ALBUMS, str(LANG_WHEEL_STATS_TOP_ALBUMS));
            break;
        case ST_MONTHS:
            pick_period(true);
            splash(0, ID2P(LANG_WAIT));
            compute(period, WHEEL_STAT_SONGS);
            break;
        case ST_HISTORY:
        {
            struct simplelist_info info;
            load_recent();
            simplelist_info_init(&info, str(LANG_WHEEL_STATS_HISTORY), MAX(1, n_hist), NULL);
            info.get_name = hist_get_name;
            simplelist_show_list(&info);
            break;
        }
        default:
            return ACTION_REDRAW;
    }
    if (went_playing)
        return ACTION_STD_CANCEL;
    /* sub screens set their own titles; ours comes back */
    gui_synclist_set_title(lists, str(LANG_WHEEL_STATS), Icon_NOICON);
    return ACTION_REDRAW;
}

int wheel_stats_screen(void)
{
    struct simplelist_info info;
    if (period[0] == 0 && !went_playing)
        this_month(period);
    went_playing = false;
    splash(0, ID2P(LANG_WAIT));
    compute(period, WHEEL_STAT_SONGS);
    simplelist_info_init(&info, str(LANG_WHEEL_STATS), ST_COUNT, NULL);
    info.get_name = st_get_name;
    info.action_callback = st_action;
    simplelist_show_list(&info);
    return went_playing ? GO_TO_WPS : 0;
}

/* ---------- small helpers other screens use ("For you") ---------- */

/* the last N distinct albums you listened to (folder paths), newest first */
int wheel_history_recent_albums(char out[][MAX_PATH], char names[][96], int max)
{
    load_recent();
    int n = 0;
    char line[MAX_PATH + 512];
    char *f[N_FIELDS];
    int fd = open(WHEEL_HISTORY_FILE, O_RDONLY);
    if (fd < 0) return 0;
    long size = ffilesize(fd);
    if (size > 96 * 1024) { lseek(fd, size - 96 * 1024, SEEK_SET); read_line(fd, line, sizeof(line)); }
    /* keep a ring of the latest, then dedupe newest-first */
    static char ring[64][MAX_PATH];
    static char ring_name[64][96];
    int w = 0, cnt = 0;
    while (read_line(fd, line, sizeof(line)) > 0)
    {
        if (!wheel_history_split(line, f)) continue;
        char *slash = strrchr(f[F_PATH], '/');
        if (slash) *slash = 0;
        strlcpy(ring[w], f[F_PATH], MAX_PATH);
        snprintf(ring_name[w], 96, "%s · %s", f[F_ALBUM], f[F_ARTIST]);
        w = (w + 1) % 64;
        if (cnt < 64) cnt++;
    }
    close(fd);
    for (int i = 0; i < cnt && n < max; i++)
    {
        int idx = (w - 1 - i + 64) % 64;
        int k;
        for (k = 0; k < n; k++) if (!strcmp(out[k], ring[idx])) break;
        if (k < n) continue;
        strlcpy(out[n], ring[idx], MAX_PATH);
        strlcpy(names[n], ring_name[idx], 96);
        n++;
    }
    return n;
}

/* the newest song in the history (for "Similar to ...") */
bool wheel_history_last(char *path, size_t plen, char *title, size_t tlen)
{
    char line[MAX_PATH + 512];
    char *f[N_FIELDS];
    bool found = false;
    int fd = open(WHEEL_HISTORY_FILE, O_RDONLY);
    if (fd < 0) return false;
    long size = ffilesize(fd);
    if (size > 8 * 1024) { lseek(fd, size - 8 * 1024, SEEK_SET); read_line(fd, line, sizeof(line)); }
    while (read_line(fd, line, sizeof(line)) > 0)
    {
        if (!wheel_history_split(line, f) || f[F_FLAG][0] != 'L') continue;
        strlcpy(path, f[F_PATH], plen);
        strlcpy(title, f[F_TITLE][0] ? f[F_TITLE] : f[F_PATH], tlen);
        found = true;
    }
    close(fd);
    return found;
}
