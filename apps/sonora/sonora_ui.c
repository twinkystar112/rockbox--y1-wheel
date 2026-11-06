/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - the screens: Continue, Audiobooks, Podcasts and the Now Playing
 * extras (chapters, bookmarks, speed, sleep). Every screen is an ordinary
 * Rockbox list, so the wheel, letter jump and the theme work as everywhere:
 * Centre opens, hold Centre for options, Menu goes back.
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
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "action.h"
#include "list.h"
#include "splash.h"
#include "yesno.h"
#include "settings.h"
#include "audio.h"
#include "playlist.h"
#include "root_menu.h"
#include "misc.h"
#include "dir.h"
#include "file.h"
#include "talk.h"
#include "tdspeed.h"
#include "string-extra.h"
#include "wheel/wheel_listview.h"
#ifdef HAVE_SPECIAL_DIRS
#include "filesystem-app.h"
#endif
#include "sonora.h"
#include "sonora_util.h"
#include "sonora_feed.h"

#define SEP LIST_VALUE_SEP_STR
#define ROWS_MAX (SONORA_MAX_CHAPTERS + 20)

enum { R_STAY = 0, R_BACK, R_WPS };

struct sscreen {
    char title[96];
    int n, sel;
    int16_t tag[ROWS_MAX];
    int16_t arg[ROWS_MAX];
    void (*build)(struct sscreen *s);
    const char *(*name)(struct sscreen *s, int row, char *buf, size_t len);
    int (*ok)(struct sscreen *s, int row);
    int (*ctx)(struct sscreen *s, int row);
    bool live;               /* redraw every second (timers) */
    int result;
    char key[MAX_PATH];      /* the book / show / episode the screen is about */
    int kind;
    int filter;
};

static struct sonora_book *book_or_file(const char *key);

static struct sscreen pool[6];
static int depth;

static struct sscreen *push(const char *title)
{
    if (depth >= (int)ARRAYLEN(pool))
        return NULL;
    struct sscreen *s = &pool[depth++];
    memset(s, 0, sizeof(*s));
    strlcpy(s->title, title, sizeof(s->title));
    return s;
}

static void add(struct sscreen *s, int tag, int arg)
{
    if (s->n < ROWS_MAX)
    {
        s->tag[s->n] = (int16_t)tag;
        s->arg[s->n] = (int16_t)arg;
        s->n++;
    }
}

static const char *sc_name(int item, void *data, char *buf, size_t len)
{
    struct sscreen *s = data;
    if (item < 0 || item >= s->n)
        return "";
    return s->name(s, item, buf, len);
}

static int sc_action(int action, struct gui_synclist *lists)
{
    struct sscreen *s = lists->data;
    if (action == ACTION_STD_OK || action == ACTION_STD_CONTEXT)
    {
        int row = gui_synclist_get_sel_pos(lists);
        int (*fn)(struct sscreen *, int) = action == ACTION_STD_OK ? s->ok : s->ctx;
        if (!fn && action == ACTION_STD_CONTEXT)
            fn = s->ok; /* hold Centre = Centre when there are no options */
        if (!fn || row < 0 || row >= s->n)
            return ACTION_REDRAW;
        int r = fn(s, row);
        if (r == R_BACK || r == R_WPS)
        {
            s->result = r;
            return ACTION_STD_CANCEL;
        }
        if (s->build)
        {   /* rebuild, keeping the cursor on the same row even if rows
             * above it came or went */
            int16_t tag = s->tag[row], arg = s->arg[row];
            s->n = 0;
            s->build(s);
            gui_synclist_set_nb_items(lists, s->n);
            int keep = -1;
            for (int i = 0; i < s->n && keep < 0; i++)
                if (s->tag[i] == tag && s->arg[i] == arg)
                    keep = i;
            for (int i = 0; i < s->n && keep < 0; i++)
                if (s->tag[i] == tag)
                    keep = i; /* same kind of row (a count changed) */
            if (keep < 0)
                keep = row < s->n ? row : (s->n > 0 ? s->n - 1 : 0);
            gui_synclist_select_item(lists, keep);
        }
        /* a screen opened from here may have replaced the title */
        gui_synclist_set_title(lists, s->title, Icon_NOICON);
        return ACTION_REDRAW;
    }
    if (action == ACTION_NONE && s->live)
        return ACTION_REDRAW;
    return action;
}

/* show the screen on top of the stack, then pop it */
static int run(struct sscreen *s)
{
    s->result = R_STAY;
    if (s->build)
    {   /* screens without a build function were filled by the caller */
        s->n = 0;
        s->build(s);
    }
    struct simplelist_info info;
    simplelist_info_init(&info, s->title, s->n, s);
    info.get_name = sc_name;
    info.action_callback = sc_action;
    info.selection = s->sel;
    if (s->live)
        info.timeout = HZ;
    simplelist_show_list(&info);
    int r = s->result;
    depth--;
    return r == R_WPS ? R_WPS : R_STAY;
}

static int to_root(int r)
{
    return r == R_WPS ? GO_TO_WPS : GO_TO_ROOT;
}

/* --------------------------------- text -------------------------------- */

static void fmt_left(char *buf, size_t len, uint32_t done, uint32_t total)
{
    char t[24];
    sonora_fmt_time(t, sizeof(t), total > done ? total - done : 0);
    snprintf(buf, len, str(LANG_SONORA_LEFT), t);
}

static int percent(const struct sonora_progress *p)
{
    if (!p || !p->total_ms)
        return 0;
    unsigned long long v = (unsigned long long)p->done_ms * 100 / p->total_ms;
    return v > 100 ? 100 : (int)v;
}

static void fmt_speed(char *buf, size_t len, uint32_t speed)
{
    unsigned v = speed / 100; /* percent */
    if (v % 100 == 0)
        snprintf(buf, len, "%u.0\xc3\x97", v / 100);
    else if (v % 10 == 0)
        snprintf(buf, len, "%u.%u\xc3\x97", v / 100, (v % 100) / 10);
    else
        snprintf(buf, len, "%u.%02u\xc3\x97", v / 100, v % 100);
}

static const char *two(char *buf, size_t len, const char *left, const char *right)
{
    if (right && right[0])
        snprintf(buf, len, "%s" SEP "%s", left, right);
    else
        strlcpy(buf, left, len);
    return buf;
}

/* ============================ shared screens ============================ */

static const int32_t speeds[] = { 7500, 8000, 9000, 10000, 11000, 12000, 12500,
                                  13000, 14000, 15000, 17500, 20000 };

static const char *speed_name(struct sscreen *s, int row, char *buf, size_t len)
{
    char v[16];
    fmt_speed(v, sizeof(v), (uint32_t)speeds[s->arg[row]]);
    bool cur = sonora_item_speed(s->key, s->kind) == (uint32_t)speeds[s->arg[row]];
    return two(buf, len, v, cur ? "\xe2\x9c\x93" : NULL);
}

static void speed_build(struct sscreen *s)
{
    for (int i = 0; i < (int)ARRAYLEN(speeds); i++)
        add(s, 0, i);
}

static void need_timestretch(void)
{
    if (!yesno_pop(ID2P(LANG_SONORA_SPEED_NEEDS_TS)))
        return;
    global_settings.timestretch_enabled = true;
    settings_save();
#ifdef INNIOASIS_Y1
    /* what the port's own Timestretch setting does on the Y1 */
    splash(HZ, ID2P(LANG_SONORA_RESTARTING));
    sleep(HZ / 2);
    if (system("am force-stop org.rockbox") != 0 ||
        system("monkey -p org.rockbox -c android.intent.category.LAUNCHER 1") != 0)
        splash(HZ * 2, ID2P(LANG_SONORA_RESTART_NEEDED));
#else
    splash(HZ * 2, ID2P(LANG_SONORA_RESTART_NEEDED));
#endif
}

static int speed_ok(struct sscreen *s, int row)
{
    if (!sonora_set_item_speed(s->key, s->kind, speeds[s->arg[row]]))
    {
        need_timestretch();
        return R_STAY;
    }
    return R_BACK;
}

static int speed_screen_for(const char *key, int kind)
{
    struct sscreen *s = push(str(LANG_SONORA_SPEED));
    if (!s)
        return R_STAY;
    strlcpy(s->key, key, sizeof(s->key));
    s->kind = kind;
    s->build = speed_build;
    s->name = speed_name;
    s->ok = speed_ok;
    uint32_t cur = sonora_item_speed(key, kind);
    for (int i = 0; i < (int)ARRAYLEN(speeds); i++)
        if ((uint32_t)speeds[i] == cur)
            s->sel = i;
    return run(s);
}

/* ------------------------------- chapters ------------------------------ */

static struct sonora_chapter book_ch[SONORA_MAX_CHAPTERS];
static char book_files[SONORA_MAX_BOOK_FILES][MAX_PATH];

enum { CH_CURRENT = 1, CH_FILE_CHAPTER, CH_BOOK_FILE };

static const char *chapter_name(struct sscreen *s, int row, char *buf, size_t len)
{
    char t[24], title[SONORA_CH_TITLE + 8];
    int i = s->arg[row];
    if (s->tag[row] == CH_BOOK_FILE)
    {
        sonora_basename_noext(book_files[i], title, sizeof(title));
        return two(buf, len, title, NULL);
    }
    struct sonora_chapter *ch = book_ch;
    if (s->tag[row] == CH_CURRENT && i >= sonora_current_chapters(&ch))
        return ""; /* the track changed under this list */
    if (ch[i].title[0])
        strlcpy(title, ch[i].title, sizeof(title));
    else
        snprintf(title, sizeof(title), "%s %d", str(LANG_SONORA_CHAPTER), i + 1);
    sonora_fmt_time(t, sizeof(t), ch[i].start_ms);
    return two(buf, len, title, t);
}

static int chapter_ok(struct sscreen *s, int row)
{
    int i = s->arg[row];
    if (s->tag[row] == CH_CURRENT)
        return sonora_seek_chapter(i) ? R_WPS : R_STAY;
    struct sonora_book *b = sonora_book_find(s->key);
    if (!b)
        return R_STAY;
    if (s->tag[row] == CH_BOOK_FILE)
        return sonora_play_book(b, SONORA_AT, book_files[i], 0) ? R_WPS : R_STAY;
    return sonora_play_book(b, SONORA_AT, b->path, book_ch[i].start_ms) ? R_WPS : R_STAY;
}

/* chapters of what is playing now, or of a book that is not playing */
static int chapters_for(const char *book_key)
{
    struct sonora_chapter *cur;
    const char *now = sonora_current_key();
    int tag = 0, n = 0, sel = 0;
    if (!book_key || (now && !strcmp(now, book_key)))
    {
        n = sonora_current_chapters(&cur);
        if (n >= 2)
        {
            tag = CH_CURRENT;
            uint32_t pos = 0;
            sonora_current_position(NULL, 0, &pos, NULL);
            for (int i = 0; i < n; i++)
                if (cur[i].start_ms <= pos)
                    sel = i;
        }
        if (!book_key)
            book_key = now;
    }
    struct sonora_book *b = book_key ? sonora_book_find(book_key) : NULL;
    if (!tag && b)
    {
        if (b->single)
        {
            int src;
            n = sonora_chapters_read(b->path, book_ch, SONORA_MAX_CHAPTERS, &src);
            if (n >= 2)
                tag = CH_FILE_CHAPTER;
        }
        else
        {
            n = sonora_book_files(b, book_files, SONORA_MAX_BOOK_FILES);
            if (n >= 2)
            {
                tag = CH_BOOK_FILE;
                if (now && !strcmp(now, b->path))
                    sel = MAX(0, sonora_current_file_index());
            }
        }
    }
    if (!tag)
    {
        splash(HZ * 2, ID2P(LANG_SONORA_NO_CHAPTERS));
        return R_STAY;
    }
    struct sscreen *s = push(str(LANG_SONORA_CHAPTERS));
    if (!s)
        return R_STAY;
    if (book_key)
        strlcpy(s->key, book_key, sizeof(s->key));
    for (int i = 0; i < n && i < ROWS_MAX; i++)
        add(s, tag, i);
    s->name = chapter_name;
    s->ok = chapter_ok;
    s->sel = sel;
    return run(s);
}

/* ------------------------------- bookmarks ----------------------------- */

static int bm_rows[SONORA_MAX_BOOKMARKS];
enum { BM_ADD = 1, BM_ITEM };

static void bm_build(struct sscreen *s)
{
    const char *now = sonora_current_key();
    if (now && !strcmp(now, s->key))
        add(s, BM_ADD, 0);
    int n = sonora_bookmarks_for(s->key, bm_rows, SONORA_MAX_BOOKMARKS);
    for (int i = 0; i < n; i++)
        add(s, BM_ITEM, i);
}

static const char *bm_name(struct sscreen *s, int row, char *buf, size_t len)
{
    if (s->tag[row] == BM_ADD)
        return str(LANG_SONORA_ADD_BOOKMARK);
    struct sonora_bookmark *b = sonora_bookmark_at(bm_rows[s->arg[row]]);
    char t[24];
    sonora_fmt_time(t, sizeof(t), b ? b->done_ms : 0);
    return two(buf, len, t, b ? b->label : NULL);
}

static int bm_ok(struct sscreen *s, int row)
{
    if (s->tag[row] == BM_ADD)
    {
        sonora_bookmark_add_current();
        return R_STAY;
    }
    struct sonora_bookmark *b = sonora_bookmark_at(bm_rows[s->arg[row]]);
    if (!b)
        return R_STAY;
    char file[MAX_PATH];
    if (sonora_current_position(file, sizeof(file), NULL, NULL) &&
        !strcmp(file, b->cur))
    {
        sonora_seek(b->pos_ms);
        return R_WPS;
    }
    if (s->kind == SK_EPISODE)
        return sonora_play_episode_at(b->key, SONORA_AT, b->pos_ms) ? R_WPS : R_STAY;
    return sonora_play_book(book_or_file(b->key), SONORA_AT, b->cur, b->pos_ms) ? R_WPS : R_STAY;
}

static int bm_ctx(struct sscreen *s, int row)
{
    if (s->tag[row] != BM_ITEM)
        return R_STAY;
    if (yesno_pop(ID2P(LANG_SONORA_DELETE_BOOKMARK)))
    {
        sonora_bookmark_delete(bm_rows[s->arg[row]]);
        sonora_bookmarks_save();
    }
    return R_STAY;
}

static int bookmarks_for(const char *key, int kind)
{
    struct sscreen *s = push(str(LANG_SONORA_BOOKMARKS));
    if (!s || !key)
        return R_STAY;
    strlcpy(s->key, key, sizeof(s->key));
    s->kind = kind;
    s->build = bm_build;
    s->name = bm_name;
    s->ok = bm_ok;
    s->ctx = bm_ctx;
    return run(s);
}

int sonora_bookmark_add_current(void)
{
    const char *key = sonora_current_key();
    char file[MAX_PATH];
    uint32_t pos, done;
    if (!key || !sonora_current_position(file, sizeof(file), &pos, &done))
        return 0;
    struct sonora_bookmark *b = sonora_bookmark_add(key);
    if (!b)
        return 0;
    strlcpy(b->cur, file, sizeof(b->cur));
    b->pos_ms = pos;
    b->done_ms = done;
    struct sonora_chapter *ch;
    int n = sonora_current_chapters(&ch);
    for (int i = n - 1; i >= 0; i--)
        if (ch[i].start_ms <= pos)
        {
            sonora_clean(b->label, ch[i].title, sizeof(b->label));
            break;
        }
    sonora_bookmarks_save();
    splash(HZ, ID2P(LANG_SONORA_BOOKMARK_ADDED));
    return 0;
}

/* --------------------------------- sleep -------------------------------- */

static const int sleep_opts[] = { 0, 5, 10, 15, 30, 45, 60, 90,
                                  SONORA_SLEEP_CHAPTER, SONORA_SLEEP_FILE };

static const char *sleep_name(struct sscreen *s, int row, char *buf, size_t len)
{
    int v = sleep_opts[s->arg[row]];
    int mode = sonora_sleep_mode();
    bool on = (v > 0 && mode > 0 && v == mode) || (v < 0 && v == mode) || (!v && !mode);
    char right[24] = "";
    if (on && v > 0)
    {
        int left = sonora_sleep_left();
        snprintf(right, sizeof(right), "%d:%02d", left / 60, left % 60);
    }
    else if (on)
        strlcpy(right, "\xe2\x9c\x93", sizeof(right));
    char left[48];
    if (!v)
        strlcpy(left, str(LANG_OFF), sizeof(left));
    else if (v == SONORA_SLEEP_CHAPTER)
        strlcpy(left, str(LANG_SONORA_END_OF_CHAPTER), sizeof(left));
    else if (v == SONORA_SLEEP_FILE)
        strlcpy(left, str(LANG_SONORA_END_OF_FILE), sizeof(left));
    else
        snprintf(left, sizeof(left), "%d min", v);
    return two(buf, len, left, right);
}

static void sleep_build(struct sscreen *s)
{
    for (int i = 0; i < (int)ARRAYLEN(sleep_opts); i++)
        add(s, 0, i);
}

static int sleep_ok(struct sscreen *s, int row)
{
    int v = sleep_opts[s->arg[row]];
    sonora_sleep_set(v);
    if (v > 0)
        splashf(HZ * 3 / 2, str(LANG_SONORA_SLEEP_SET), v);
    else if (v == SONORA_SLEEP_CHAPTER)
        splash(HZ * 3 / 2, ID2P(LANG_SONORA_SLEEP_SET_CHAPTER));
    else if (v == SONORA_SLEEP_FILE)
        splash(HZ * 3 / 2, ID2P(LANG_SONORA_SLEEP_SET_FILE));
    else
        splash(HZ, ID2P(LANG_SONORA_SLEEP_OFF));
    return R_BACK;
}

int sonora_sleep_screen(void)
{
    struct sscreen *s = push(str(LANG_SLEEP_TIMER));
    if (!s)
        return 0;
    s->build = sleep_build;
    s->name = sleep_name;
    s->ok = sleep_ok;
    s->live = true;
    run(s);
    return 0;
}

/* ------------------------- Now Playing extras ------------------------- */

/* these return 1 when they moved playback (the caller closes its menu) */
int sonora_chapters_screen(void)
{
    return chapters_for(NULL) == R_WPS;
}

int sonora_bookmarks_screen(void)
{
    const char *key = sonora_current_key();
    return key ? bookmarks_for(key, sonora_current_kind()) == R_WPS : 0;
}

int sonora_speed_screen(void)
{
    const char *key = sonora_current_key();
    if (key)
        speed_screen_for(key, sonora_current_kind());
    return 0;
}

int sonora_mark_current(void)
{
    const char *key = sonora_current_key();
    if (!key)
        return 0;
    enum sonora_kind k = sonora_current_kind();
    sonora_mark(key, k == SK_BOOK ? SK_BOOK_C : SK_EPISODE_C, SP_DONE);
    splash(HZ, k == SK_BOOK ? ID2P(LANG_SONORA_MARKED_FINISHED) : ID2P(LANG_SONORA_MARKED_PLAYED));
    return 0;
}

/* =============================== Continue ============================== */

/* a book from the library, or an .m4b played from Files (not in the
 * Audiobooks folder): then the file itself is the book */
static struct sonora_book *book_or_file(const char *key)
{
    struct sonora_book *b = sonora_book_find(key);
    if (!b)
    {
        sonora_books_scan(true);
        b = sonora_book_find(key);
    }
    if (!b && file_exists(key))
    {
        static struct sonora_book loose;
        memset(&loose, 0, sizeof(loose));
        strlcpy(loose.path, key, sizeof(loose.path));
        sonora_basename_noext(key, loose.title, sizeof(loose.title));
        loose.single = true;
        loose.nfiles = 1;
        b = &loose;
    }
    return b;
}

static int cont_rows[24];
enum { C_ITEM = 1, C_MUSIC, C_EMPTY };

static void cont_build(struct sscreen *s)
{
    int n = sonora_progress_recent(0, SP_PLAYING, cont_rows, ARRAYLEN(cont_rows));
    for (int i = 0; i < n; i++)
        if (sonora_progress_at(cont_rows[i])->kind != SK_MUSIC_C)
            add(s, C_ITEM, i);
    if (sonora_music_saved())
        add(s, C_MUSIC, 0);
    if (!s->n)
        add(s, C_EMPTY, 0);
}

static const char *cont_name(struct sscreen *s, int row, char *buf, size_t len)
{
    if (s->tag[row] == C_EMPTY)
        return str(LANG_SONORA_NOTHING_TO_CONTINUE);
    if (s->tag[row] == C_MUSIC)
        return str(LANG_SONORA_RESUME_MUSIC);
    struct sonora_progress *p = sonora_progress_at(cont_rows[s->arg[row]]);
    char right[32];
    if (p->kind == SK_BOOK_C)
        snprintf(right, sizeof(right), "%d%%", percent(p));
    else
        fmt_left(right, sizeof(right), p->done_ms, p->total_ms);
    return two(buf, len, p->title[0] ? p->title : p->key, right);
}

static int play_progress(struct sonora_progress *p)
{
    if (p->kind == SK_EPISODE_C)
        return sonora_play_episode(p->key, SONORA_RESUME) ? R_WPS : R_STAY;
    struct sonora_book *b = book_or_file(p->key);
    if (!b)
    {
        splash(HZ * 2, ID2P(LANG_SONORA_MISSING));
        return R_STAY;
    }
    return sonora_play_book(b, SONORA_RESUME, NULL, 0) ? R_WPS : R_STAY;
}

static int cont_ok(struct sscreen *s, int row)
{
    if (s->tag[row] == C_EMPTY)
        return R_STAY;
    if (s->tag[row] == C_MUSIC)
        return sonora_resume_music() ? R_WPS : R_STAY;
    return play_progress(sonora_progress_at(cont_rows[s->arg[row]]));
}

bool sonora_continue_available(void)
{
    int tmp[1];
    sonora_init();
    return sonora_progress_recent(SK_BOOK_C, SP_PLAYING, tmp, 1) ||
           sonora_progress_recent(SK_EPISODE_C, SP_PLAYING, tmp, 1) || sonora_music_saved();
}

int sonora_continue_screen(void)
{
    sonora_init();
    struct sscreen *s = push(str(LANG_SONORA_CONTINUE));
    if (!s)
        return GO_TO_ROOT;
    s->build = cont_build;
    s->name = cont_name;
    s->ok = cont_ok;
    push_current_activity(ACTIVITY_DATABASEBROWSER);
    int r = run(s);
    if (r == R_WPS)
        pop_current_activity_without_refresh();
    else
        pop_current_activity();
    return to_root(r);
}

/* ============================== Audiobooks ============================= */

enum { B_INPROG = 1, B_ALL, B_AUTHORS, B_FINISHED, B_SETTINGS, B_EMPTY, B_BOOK, B_AUTHOR };
enum { F_ALL = 0, F_INPROG, F_FINISHED, F_AUTHOR };

static char authors[64][64];
static int n_authors;

static int book_state(const struct sonora_book *b)
{
    struct sonora_progress *p = sonora_progress_find(b->path);
    return p ? p->state : SP_NEW;
}

static const char *book_row_name(struct sscreen *s, int row, char *buf, size_t len)
{
    struct sonora_book *b = sonora_book_at(s->arg[row]);
    if (!b)
        return "";
    struct sonora_progress *p = sonora_progress_find(b->path);
    char right[24];
    if (p && p->state == SP_PLAYING)
        snprintf(right, sizeof(right), "%d%%", percent(p));
    else if (p && p->state == SP_DONE)
        strlcpy(right, "\xe2\x9c\x93", sizeof(right));
    else
        sonora_fmt_time(right, sizeof(right), b->total_ms);
    return two(buf, len, b->title, right);
}

/* ---- the book page ---- */
enum { P_RESUME = 1, P_PLAY, P_START, P_CHAPTERS, P_BOOKMARKS, P_SPEED, P_MARK_DONE,
       P_MARK_NEW, P_AUTHOR, P_LENGTH, P_FILES };

static void page_build(struct sscreen *s)
{
    struct sonora_book *b = sonora_book_find(s->key);
    if (!b)
        return;
    int st_ = book_state(b);
    add(s, st_ == SP_PLAYING ? P_RESUME : P_PLAY, 0);
    if (st_ != SP_NEW)
        add(s, P_START, 0);
    add(s, P_CHAPTERS, 0);
    add(s, P_BOOKMARKS, 0);
    add(s, P_SPEED, 0);
    add(s, st_ == SP_DONE ? P_MARK_NEW : P_MARK_DONE, 0);
    if (b->author[0])
        add(s, P_AUTHOR, 0);
    add(s, P_LENGTH, 0);
    if (!b->single)
        add(s, P_FILES, 0);
}

static const char *page_name(struct sscreen *s, int row, char *buf, size_t len)
{
    struct sonora_book *b = sonora_book_find(s->key);
    struct sonora_progress *p = sonora_progress_find(s->key);
    char right[48] = "";
    if (!b)
        return "";
    switch (s->tag[row])
    {
        case P_RESUME:
            if (p)
                fmt_left(right, sizeof(right), p->done_ms, p->total_ms ? p->total_ms : b->total_ms);
            return two(buf, len, str(LANG_SONORA_RESUME), right);
        case P_PLAY:    return str(LANG_SONORA_PLAY);
        case P_START:   return str(LANG_SONORA_PLAY_START);
        case P_CHAPTERS: return str(LANG_SONORA_CHAPTERS);
        case P_BOOKMARKS:
        {
            int tmp[SONORA_MAX_BOOKMARKS];
            int n = sonora_bookmarks_for(s->key, tmp, SONORA_MAX_BOOKMARKS);
            snprintf(right, sizeof(right), "%d", n);
            return two(buf, len, str(LANG_SONORA_BOOKMARKS), n ? right : NULL);
        }
        case P_SPEED:
            fmt_speed(right, sizeof(right), sonora_item_speed(s->key, SK_BOOK));
            return two(buf, len, str(LANG_SONORA_SPEED), right);
        case P_MARK_DONE: return str(LANG_SONORA_MARK_FINISHED);
        case P_MARK_NEW:  return str(LANG_SONORA_MARK_UNPLAYED);
        case P_AUTHOR:    return two(buf, len, str(LANG_SONORA_AUTHOR), b->author);
        case P_LENGTH:
            sonora_fmt_time(right, sizeof(right), b->total_ms);
            return two(buf, len, str(LANG_SONORA_LENGTH), right);
        case P_FILES:
            snprintf(right, sizeof(right), "%d", b->nfiles);
            return two(buf, len, str(LANG_SONORA_FILES), right);
    }
    return "";
}

static int page_ok(struct sscreen *s, int row)
{
    struct sonora_book *b = sonora_book_find(s->key);
    if (!b)
        return R_BACK;
    switch (s->tag[row])
    {
        case P_RESUME:
        case P_PLAY:
            return sonora_play_book(b, SONORA_RESUME, NULL, 0) ? R_WPS : R_STAY;
        case P_START:
            return sonora_play_book(b, SONORA_FROM_START, NULL, 0) ? R_WPS : R_STAY;
        case P_CHAPTERS:  return chapters_for(s->key);
        case P_BOOKMARKS: return bookmarks_for(s->key, SK_BOOK);
        case P_SPEED:     return speed_screen_for(s->key, SK_BOOK);
        case P_MARK_DONE:
        {
            struct sonora_progress *p = sonora_progress_get(s->key, SK_BOOK_C);
            if (p && !p->total_ms)
                p->total_ms = b->total_ms;
            if (p && !p->title[0])
                strlcpy(p->title, b->title, sizeof(p->title));
            sonora_mark(s->key, SK_BOOK_C, SP_DONE);
            return R_STAY;
        }
        case P_MARK_NEW:
            sonora_mark(s->key, SK_BOOK_C, SP_NEW);
            return R_STAY;
    }
    return R_STAY;
}

static int book_page(struct sonora_book *b)
{
    struct sscreen *s = push(b->title);
    if (!s)
        return R_STAY;
    strlcpy(s->key, b->path, sizeof(s->key));
    s->kind = SK_BOOK;
    s->build = page_build;
    s->name = page_name;
    s->ok = page_ok;
    return run(s);
}

/* ---- lists of books ---- */
static void list_build(struct sscreen *s)
{
    for (int i = 0; i < sonora_books_count(); i++)
    {
        struct sonora_book *b = sonora_book_at(i);
        int st_ = book_state(b);
        if ((s->filter == F_INPROG && st_ != SP_PLAYING) ||
            (s->filter == F_FINISHED && st_ != SP_DONE) ||
            (s->filter == F_AUTHOR && strcmp(b->author, s->key)))
            continue;
        add(s, B_BOOK, i);
    }
    if (!s->n)
        add(s, B_EMPTY, 0);
}

static const char *list_name(struct sscreen *s, int row, char *buf, size_t len)
{
    if (s->tag[row] == B_EMPTY)
        return str(LANG_SONORA_NONE_HERE);
    return book_row_name(s, row, buf, len);
}

static int list_ok(struct sscreen *s, int row)
{
    if (s->tag[row] != B_BOOK)
        return R_STAY;
    struct sonora_book *b = sonora_book_at(s->arg[row]);
    return b ? book_page(b) : R_STAY;
}

static int list_ctx(struct sscreen *s, int row)
{   /* hold Centre on a book: play it right away */
    if (s->tag[row] != B_BOOK)
        return R_STAY;
    struct sonora_book *b = sonora_book_at(s->arg[row]);
    return b && sonora_play_book(b, SONORA_RESUME, NULL, 0) ? R_WPS : R_STAY;
}

static int book_list(const char *title, int filter, const char *author)
{
    struct sscreen *s = push(title);
    if (!s)
        return R_STAY;
    s->filter = filter;
    if (author)
        strlcpy(s->key, author, sizeof(s->key));
    s->build = list_build;
    s->name = list_name;
    s->ok = list_ok;
    s->ctx = list_ctx;
    return run(s);
}

static void authors_build(struct sscreen *s)
{
    n_authors = 0;
    for (int i = 0; i < sonora_books_count() && n_authors < (int)ARRAYLEN(authors); i++)
    {
        const char *a = sonora_book_at(i)->author;
        bool seen = false;
        for (int k = 0; k < n_authors && !seen; k++)
            seen = !strcmp(authors[k], a);
        if (!seen)
            strlcpy(authors[n_authors++], a, sizeof(authors[0]));
    }
    for (int k = 0; k < n_authors; k++)
        add(s, B_AUTHOR, k);
}

static const char *authors_name(struct sscreen *s, int row, char *buf, size_t len)
{
    (void)buf; (void)len;
    const char *a = authors[s->arg[row]];
    return a[0] ? a : (const char *)str(LANG_SONORA_UNKNOWN_AUTHOR);
}

static int authors_ok(struct sscreen *s, int row)
{
    const char *a = authors[s->arg[row]];
    return book_list(a[0] ? a : (const char *)str(LANG_SONORA_UNKNOWN_AUTHOR), F_AUTHOR, a);
}

static void books_build(struct sscreen *s)
{
    sonora_books_scan(true);
    if (!sonora_books_count())
        add(s, B_EMPTY, 0);
    else
    {
        int inprog = 0;
        for (int i = 0; i < sonora_books_count(); i++)
            if (book_state(sonora_book_at(i)) == SP_PLAYING)
                inprog++;
        if (inprog)
            add(s, B_INPROG, inprog);
        add(s, B_ALL, sonora_books_count());
        add(s, B_AUTHORS, 0);
        add(s, B_FINISHED, 0);
    }
    add(s, B_SETTINGS, 0);
}

static const char *books_name(struct sscreen *s, int row, char *buf, size_t len)
{
    char n[12];
    snprintf(n, sizeof(n), "%d", s->arg[row]);
    switch (s->tag[row])
    {
        case B_EMPTY:
            snprintf(buf, len, str(LANG_SONORA_NO_BOOKS), sonora_books_root());
            return buf;
        case B_INPROG:   return two(buf, len, str(LANG_SONORA_IN_PROGRESS), n);
        case B_ALL:      return two(buf, len, str(LANG_SONORA_ALL_BOOKS), n);
        case B_AUTHORS:  return str(LANG_SONORA_AUTHORS);
        case B_FINISHED: return str(LANG_SONORA_FINISHED);
        case B_SETTINGS: return str(LANG_SONORA_SETTINGS);
    }
    return "";
}

static int books_ok(struct sscreen *s, int row)
{
    switch (s->tag[row])
    {
        case B_EMPTY:    sonora_library_invalidate(); return R_STAY; /* look again */
        case B_INPROG:   return book_list(str(LANG_SONORA_IN_PROGRESS), F_INPROG, NULL);
        case B_ALL:      return book_list(str(LANG_SONORA_ALL_BOOKS), F_ALL, NULL);
        case B_FINISHED: return book_list(str(LANG_SONORA_FINISHED), F_FINISHED, NULL);
        case B_AUTHORS:
        {
            struct sscreen *a = push(str(LANG_SONORA_AUTHORS));
            if (!a)
                return R_STAY;
            a->build = authors_build;
            a->name = authors_name;
            a->ok = authors_ok;
            return run(a);
        }
        case B_SETTINGS:
            sonora_settings_screen();
            return R_STAY;
    }
    return R_STAY;
}

int sonora_books_screen(void)
{
    sonora_init();
    sonora_library_invalidate(); /* new books copied over USB show up */
    struct sscreen *s = push(str(LANG_SONORA_AUDIOBOOKS));
    if (!s)
        return GO_TO_ROOT;
    s->build = books_build;
    s->name = books_name;
    s->ok = books_ok;
    push_current_activity(ACTIVITY_DATABASEBROWSER);
    int r = run(s);
    if (r == R_WPS)
        pop_current_activity_without_refresh();
    else
        pop_current_activity();
    return to_root(r);
}

/* =============================== Podcasts ============================== */

#define MAX_EPS 300
static struct sonora_episode eps[MAX_EPS];
static bool ep_titled[MAX_EPS];
static int n_eps;

enum { D_NEW = 1, D_SHOWS, D_DOWNLOADS, D_OPML, D_REFRESH, D_CLEANUP, D_SETTINGS, D_EMPTY,
       D_SHOW, D_EP, D_SHOW_SPEED, D_SHOW_KEEP, D_SHOW_REFRESH, D_DL };

static const char *ep_title(int i)
{
    if (!ep_titled[i])
    {
        sonora_episode_title(eps[i].path, eps[i].title, sizeof(eps[i].title));
        ep_titled[i] = true;
    }
    return eps[i].title;
}

static const char *ep_name(struct sscreen *s, int row, char *buf, size_t len)
{
    int i = s->arg[row];
    struct sonora_progress *p = sonora_progress_find(eps[i].path);
    char right[32] = "";
    if (p && p->state == SP_DONE)
        strlcpy(right, "\xe2\x9c\x93", sizeof(right));
    else if (p && p->state == SP_PLAYING)
        fmt_left(right, sizeof(right), p->done_ms, p->total_ms);
    else
        strlcpy(right, "\xe2\x80\xa2", sizeof(right)); /* new */
    return two(buf, len, ep_title(i), right);
}

static int ep_ok(struct sscreen *s, int row)
{
    return sonora_play_episode(eps[s->arg[row]].path, SONORA_RESUME) ? R_WPS : R_STAY;
}

static int episode_options(const char *path)
{
    struct sonora_progress *p = sonora_progress_find(path);
    bool done = p && p->state == SP_DONE;
    bool ours = sonora_download_may_delete(path, sonora_podcasts_root());
    const char *items[5];
    int act[5], n = 0;
    items[n] = str(LANG_SONORA_PLAY_START); act[n++] = 1;
    items[n] = str(done ? LANG_SONORA_MARK_UNPLAYED : LANG_SONORA_MARK_PLAYED); act[n++] = 2;
    items[n] = str(LANG_SONORA_BOOKMARKS); act[n++] = 3;
    items[n] = str(LANG_SONORA_DELETE_DOWNLOAD); act[n++] = 4;
    int pick = wheel_pick(str(LANG_SONORA_EPISODE), items, n, 0);
    if (pick < 0)
        return R_STAY;
    switch (act[pick])
    {
        case 1: return sonora_play_episode(path, SONORA_FROM_START) ? R_WPS : R_STAY;
        case 2: sonora_mark(path, SK_EPISODE_C, done ? SP_NEW : SP_DONE); return R_STAY;
        case 3: return bookmarks_for(path, SK_EPISODE);
        case 4:
            if (!ours)
            {   /* copied by the user, or changed since: never deleted */
                splash(HZ * 3, ID2P(LANG_SONORA_NOT_OURS));
                return R_STAY;
            }
            if (yesno_pop(ID2P(LANG_SONORA_DELETE_DOWNLOAD)) && remove(path) == 0)
            {
                sonora_download_removed(path);
                sonora_downloads_save();
                splash(HZ, ID2P(LANG_SONORA_DELETED));
            }
            return R_STAY;
    }
    return R_STAY;
}

static int ep_ctx(struct sscreen *s, int row)
{
    if (s->tag[row] != D_EP)
        return R_STAY;
    return episode_options(eps[s->arg[row]].path);
}

/* ---- one show ---- */
static const int keep_opts[] = { 0, 1, 3, 5, 10, 20 };

static void show_build(struct sscreen *s)
{
    struct sonora_show *sh = sonora_show_find(s->key);
    add(s, D_SHOW_SPEED, 0);
    add(s, D_SHOW_KEEP, 0);
    if (sh && sh->url[0])
        add(s, D_SHOW_REFRESH, 0);
    n_eps = sonora_show_episodes(s->key, eps, MAX_EPS);
    memset(ep_titled, 0, sizeof(ep_titled));
    for (int i = 0; i < n_eps; i++)
        add(s, D_EP, i);
    if (!n_eps)
        add(s, D_EMPTY, 0);
}

static const char *show_name(struct sscreen *s, int row, char *buf, size_t len)
{
    struct sonora_show *sh = sonora_show_find(s->key);
    char right[32];
    switch (s->tag[row])
    {
        case D_SHOW_SPEED:
            fmt_speed(right, sizeof(right), sh && sh->speed > 0 ? (uint32_t)sh->speed : PITCH_SPEED_100);
            return two(buf, len, str(LANG_SONORA_SPEED), right);
        case D_SHOW_KEEP:
            if (sh && sh->keep > 0)
                snprintf(right, sizeof(right), "%d", (int)sh->keep);
            else
                strlcpy(right, str(LANG_SONORA_ALL), sizeof(right));
            return two(buf, len, str(LANG_SONORA_KEEP), right);
        case D_SHOW_REFRESH: return str(LANG_SONORA_REFRESH);
        case D_EMPTY:        return str(LANG_SONORA_NOT_DOWNLOADED);
        case D_EP:           return ep_name(s, row, buf, len);
    }
    return "";
}

static int show_ok(struct sscreen *s, int row)
{
    struct sonora_show *sh = sonora_show_add(s->key);
    char path[MAX_PATH];
    switch (s->tag[row])
    {
        case D_SHOW_SPEED:
            /* any episode path of the show selects the show's speed */
            snprintf(path, sizeof(path), "%s/%s/x", sonora_podcasts_root(), s->key);
            return speed_screen_for(path, SK_EPISODE);
        case D_SHOW_KEEP:
            if (sh)
            {
                int i = 0;
                while (i < (int)ARRAYLEN(keep_opts) && keep_opts[i] != sh->keep) i++;
                sh->keep = keep_opts[(i + 1) % ARRAYLEN(keep_opts)];
                sonora_shows_save();
            }
            return R_STAY;
        case D_SHOW_REFRESH:
            sonora_refresh_screen();
            return R_STAY;
        case D_EP:
            return ep_ok(s, row);
    }
    return R_STAY;
}

static int show_page(const char *folder)
{
    struct sonora_show *sh = sonora_show_find(folder);
    struct sscreen *s = push(sh ? sh->title : folder);
    if (!s)
        return R_STAY;
    strlcpy(s->key, folder, sizeof(s->key));
    s->kind = SK_EPISODE;
    s->build = show_build;
    s->name = show_name;
    s->ok = show_ok;
    s->ctx = ep_ctx;
    return run(s);
}

/* ---- shows ---- */
static void shows_build(struct sscreen *s)
{
    sonora_shows_scan();
    for (int i = 0; i < sonora_shows_count(); i++)
        add(s, D_SHOW, i);
    if (!s->n)
        add(s, D_EMPTY, 0);
}

static const char *shows_name(struct sscreen *s, int row, char *buf, size_t len)
{
    if (s->tag[row] == D_EMPTY)
    {
        snprintf(buf, len, str(LANG_SONORA_NO_PODCASTS), sonora_podcasts_root());
        return buf;
    }
    struct sonora_show *sh = sonora_show_at(s->arg[row]);
    return sh ? sh->title : "";
}

static int shows_ok(struct sscreen *s, int row)
{
    if (s->tag[row] != D_SHOW)
        return R_STAY;
    struct sonora_show *sh = sonora_show_at(s->arg[row]);
    if (!sh)
        return R_STAY;
    char folder[96];
    strlcpy(folder, sh->folder, sizeof(folder));
    return show_page(folder);
}

/* ---- new episodes ---- */
static void new_build(struct sscreen *s)
{
    n_eps = sonora_new_episodes(eps, MAX_EPS);
    memset(ep_titled, 0, sizeof(ep_titled));
    for (int i = 0; i < n_eps && i < 100; i++)
        add(s, D_EP, i);
    if (!s->n)
        add(s, D_EMPTY, 0);
}

static const char *new_name(struct sscreen *s, int row, char *buf, size_t len)
{
    if (s->tag[row] == D_EMPTY)
        return str(LANG_SONORA_NONE_HERE);
    return ep_name(s, row, buf, len);
}

/* ---- downloads (the manifest) ---- */
static void dl_build(struct sscreen *s)
{
    for (int i = 0; i < sonora_downloads_count(); i++)
        if (sonora_download_at(i)->size >= 0)
            add(s, D_DL, i);
    if (!s->n)
        add(s, D_EMPTY, 0);
}

static const char *dl_name(struct sscreen *s, int row, char *buf, size_t len)
{
    if (s->tag[row] == D_EMPTY)
        return str(LANG_SONORA_NONE_HERE);
    struct sonora_download *d = sonora_download_at(s->arg[row]);
    char t[96], right[24];
    sonora_basename_noext(d->path, t, sizeof(t));
    if (d->size >= 1024 * 1024)
        snprintf(right, sizeof(right), "%lld MB", d->size / (1024 * 1024));
    else
        snprintf(right, sizeof(right), "%lld KB", (d->size + 1023) / 1024);
    return two(buf, len, t, right);
}

static int dl_ok(struct sscreen *s, int row)
{
    if (s->tag[row] != D_DL)
        return R_STAY;
    char path[MAX_PATH];
    strlcpy(path, sonora_download_at(s->arg[row])->path, sizeof(path));
    return episode_options(path);
}

/* ---- OPML ---- */
static int opml_added;
static void opml_feed(const char *title, const char *url, void *ctx)
{
    (void)ctx;
    if (sonora_show_find_url(url))
        return;
    char folder[96];
    sonora_safe_name(title, folder, sizeof(folder));
    struct sonora_show *sh = sonora_show_find(folder);
    if (sh && sh->url[0])
        return; /* a different feed with the same name: keep the first */
    sh = sonora_show_add(folder);
    if (!sh)
        return;
    sonora_clean(sh->title, title, sizeof(sh->title));
    sonora_clean(sh->url, url, sizeof(sh->url));
    opml_added++;
}

static bool find_opml(const char *dir, char *out, size_t len)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(d)))
        if (sonora_ext_is(e->d_name, "opml") || sonora_ext_is(e->d_name, "xml"))
        {
            snprintf(out, len, "%s/%s", dir, e->d_name);
            found = sonora_ext_is(e->d_name, "opml");
        }
    closedir(d);
    return found;
}

static int opml_import(void)
{
    char path[MAX_PATH];
    if (!find_opml(sonora_podcasts_root(), path, sizeof(path)) &&
        !find_opml(sonora_data_dir(), path, sizeof(path)))
    {
        splashf(HZ * 3, str(LANG_SONORA_NO_OPML), sonora_podcasts_root());
        return R_STAY;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return R_STAY;
    long size = ffilesize(fd);
    if (size <= 0 || size > 1024 * 1024)
    {
        close(fd);
        return R_STAY;
    }
    char *buf = malloc(size + 1);
    if (!buf)
    {
        close(fd);
        return R_STAY;
    }
    long got = read(fd, buf, size);
    close(fd);
    opml_added = 0;
    if (got > 0)
    {
        buf[got] = '\0';
        sonora_opml_parse(buf, got, opml_feed, NULL);
    }
    free(buf);
    sonora_shows_save();
    splashf(HZ * 2, str(LANG_SONORA_IMPORTED), opml_added);
    return R_STAY;
}

/* ---- podcasts main ---- */
static void pods_build(struct sscreen *s)
{
    sonora_shows_scan();
    int nnew = sonora_new_episodes(eps, MAX_EPS);
    add(s, D_NEW, nnew);
    add(s, D_SHOWS, sonora_shows_count());
    int ndl = sonora_downloads_present();
    if (ndl)
        add(s, D_DOWNLOADS, ndl);
    add(s, D_OPML, 0);
    add(s, D_REFRESH, 0);
    if (ndl)
        add(s, D_CLEANUP, 0);
    add(s, D_SETTINGS, 0);
}

static const char *pods_name(struct sscreen *s, int row, char *buf, size_t len)
{
    char n[12];
    snprintf(n, sizeof(n), "%d", s->arg[row]);
    switch (s->tag[row])
    {
        case D_NEW:       return two(buf, len, str(LANG_SONORA_NEW_EPISODES), s->arg[row] ? n : NULL);
        case D_SHOWS:     return two(buf, len, str(LANG_SONORA_SHOWS), n);
        case D_DOWNLOADS: return two(buf, len, str(LANG_SONORA_DOWNLOADS), n);
        case D_OPML:      return str(LANG_SONORA_IMPORT_OPML);
        case D_REFRESH:
            return two(buf, len, str(LANG_SONORA_REFRESH),
                       global_settings.sonora_network ? NULL : str(LANG_OFF));
        case D_CLEANUP:   return str(LANG_SONORA_CLEANUP);
        case D_SETTINGS:  return str(LANG_SONORA_SETTINGS);
    }
    return "";
}

static int pods_ok(struct sscreen *s, int row)
{
    struct sscreen *c;
    switch (s->tag[row])
    {
        case D_NEW:
            if (!(c = push(str(LANG_SONORA_NEW_EPISODES))))
                return R_STAY;
            c->filter = 1;
            c->build = new_build;
            c->name = new_name;
            c->ok = ep_ok;
            c->ctx = ep_ctx;
            return run(c);
        case D_SHOWS:
            if (!(c = push(str(LANG_SONORA_SHOWS))))
                return R_STAY;
            c->build = shows_build;
            c->name = shows_name;
            c->ok = shows_ok;
            return run(c);
        case D_DOWNLOADS:
            if (!(c = push(str(LANG_SONORA_DOWNLOADS))))
                return R_STAY;
            c->build = dl_build;
            c->name = dl_name;
            c->ok = dl_ok;
            return run(c);
        case D_OPML:     return opml_import();
        case D_REFRESH:  sonora_refresh_screen(); return R_STAY;
        case D_CLEANUP:  sonora_cleanup_downloads(true); return R_STAY;
        case D_SETTINGS: sonora_settings_screen(); return R_STAY;
    }
    return R_STAY;
}

int sonora_podcasts_screen(void)
{
    sonora_init();
    struct sscreen *s = push(str(LANG_SONORA_PODCASTS));
    if (!s)
        return GO_TO_ROOT;
    s->build = pods_build;
    s->name = pods_name;
    s->ok = pods_ok;
    push_current_activity(ACTIVITY_DATABASEBROWSER);
    int r = run(s);
    if (r == R_WPS)
        pop_current_activity_without_refresh();
    else
        pop_current_activity();
    return to_root(r);
}

/* =============================== settings ============================== */

enum { S_BOOKS_DIR = 1, S_PODS_DIR, S_REWIND, S_BOOK_BACK, S_BOOK_FWD, S_POD_BACK,
       S_POD_FWD, S_CHECKPOINT, S_AUTONEXT, S_NETWORK };

static void set_build(struct sscreen *s)
{
    for (int t = S_BOOKS_DIR; t <= S_NETWORK; t++)
        add(s, t, 0);
}

static const char *last_part(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

static const char *secs(char *buf, size_t len, int v)
{
    snprintf(buf, len, "%d s", v);
    return buf;
}

static const char *set_name(struct sscreen *s, int row, char *buf, size_t len)
{
    char v[MAX_PATH];
    switch (s->tag[row])
    {
        case S_BOOKS_DIR:
            return two(buf, len, str(LANG_SONORA_BOOKS_FOLDER), last_part(sonora_books_root()));
        case S_PODS_DIR:
            return two(buf, len, str(LANG_SONORA_PODCASTS_FOLDER), last_part(sonora_podcasts_root()));
        case S_REWIND:
            return two(buf, len, str(LANG_SONORA_REWIND),
                       global_settings.sonora_rewind ? secs(v, sizeof(v), global_settings.sonora_rewind)
                                                     : (const char *)str(LANG_OFF));
        case S_BOOK_BACK:
            return two(buf, len, str(LANG_SONORA_BOOK_SKIP_BACK), secs(v, sizeof(v), global_settings.sonora_book_back));
        case S_BOOK_FWD:
            return two(buf, len, str(LANG_SONORA_BOOK_SKIP_FWD), secs(v, sizeof(v), global_settings.sonora_book_fwd));
        case S_POD_BACK:
            return two(buf, len, str(LANG_SONORA_POD_SKIP_BACK), secs(v, sizeof(v), global_settings.sonora_pod_back));
        case S_POD_FWD:
            return two(buf, len, str(LANG_SONORA_POD_SKIP_FWD), secs(v, sizeof(v), global_settings.sonora_pod_fwd));
        case S_CHECKPOINT:
            return two(buf, len, str(LANG_SONORA_CHECKPOINT), secs(v, sizeof(v), global_settings.sonora_checkpoint));
        case S_AUTONEXT:
            return two(buf, len, str(LANG_SONORA_AUTONEXT),
                       str(global_settings.sonora_autonext ? LANG_ON : LANG_OFF));
        case S_NETWORK:
            return two(buf, len, str(LANG_SONORA_NETWORK),
                       str(global_settings.sonora_network ? LANG_ON : LANG_OFF));
    }
    return "";
}

static char folder_names[48][64];

static void pick_folder(char *setting, int lang)
{
    const char *items[49];
    char home[MAX_PATH];
#ifdef HAVE_SPECIAL_DIRS
    const char *real = handle_special_dirs(HOME_DIR, 0, home, sizeof(home));
    if (real != home)
        strlcpy(home, real, sizeof(home));
#else
    strlcpy(home, HOME_DIR, sizeof(home));
#endif
    int n = 0;
    items[n] = str(LANG_SONORA_DEFAULT_FOLDER);
    n++;
    DIR *d = opendir(home);
    if (d)
    {
        struct dirent *e;
        while ((e = readdir(d)) && n < 49)
        {
            struct dirinfo info = dir_get_info(d, e);
            if ((info.attribute & ATTR_DIRECTORY) && e->d_name[0] != '.')
            {
                strlcpy(folder_names[n - 1], e->d_name, sizeof(folder_names[0]));
                items[n] = folder_names[n - 1];
                n++;
            }
        }
        closedir(d);
    }
    int pick = wheel_pick(str(lang), items, n, 0);
    if (pick < 0)
        return;
    if (pick == 0)
        setting[0] = '\0';
    else
        snprintf(setting, MAX_PATH, "%s/%s", home, items[pick]);
    settings_save();
    sonora_library_invalidate();
    sonora_reload_roots();
}

static int set_ok(struct sscreen *s, int row)
{
    switch (s->tag[row])
    {
        case S_BOOKS_DIR:
            pick_folder(global_settings.sonora_books_dir, LANG_SONORA_BOOKS_FOLDER);
            break;
        case S_PODS_DIR:
            pick_folder(global_settings.sonora_podcasts_dir, LANG_SONORA_PODCASTS_FOLDER);
            break;
        case S_REWIND:
            set_int(str(LANG_SONORA_REWIND), "s", UNIT_SEC, &global_settings.sonora_rewind,
                    NULL, 5, 0, 60, NULL);
            break;
        case S_BOOK_BACK:
            set_int(str(LANG_SONORA_BOOK_SKIP_BACK), "s", UNIT_SEC, &global_settings.sonora_book_back,
                    NULL, 5, 5, 120, NULL);
            break;
        case S_BOOK_FWD:
            set_int(str(LANG_SONORA_BOOK_SKIP_FWD), "s", UNIT_SEC, &global_settings.sonora_book_fwd,
                    NULL, 5, 5, 120, NULL);
            break;
        case S_POD_BACK:
            set_int(str(LANG_SONORA_POD_SKIP_BACK), "s", UNIT_SEC, &global_settings.sonora_pod_back,
                    NULL, 5, 5, 120, NULL);
            break;
        case S_POD_FWD:
            set_int(str(LANG_SONORA_POD_SKIP_FWD), "s", UNIT_SEC, &global_settings.sonora_pod_fwd,
                    NULL, 5, 5, 120, NULL);
            break;
        case S_CHECKPOINT:
            set_int(str(LANG_SONORA_CHECKPOINT), "s", UNIT_SEC, &global_settings.sonora_checkpoint,
                    NULL, 15, 15, 300, NULL);
            break;
        case S_AUTONEXT:
            global_settings.sonora_autonext = !global_settings.sonora_autonext;
            break;
        case S_NETWORK:
            if (!global_settings.sonora_network && !sonora_net_supported())
            {
                splash(HZ * 3, ID2P(LANG_SONORA_NO_WIFI));
                break;
            }
            global_settings.sonora_network = !global_settings.sonora_network;
            break;
    }
    settings_save();
    return R_STAY;
}

int sonora_settings_screen(void)
{
    sonora_init();
    struct sscreen *s = push(str(LANG_SONORA_SETTINGS));
    if (!s)
        return 0;
    s->build = set_build;
    s->name = set_name;
    s->ok = set_ok;
    run(s);
    return 0;
}
