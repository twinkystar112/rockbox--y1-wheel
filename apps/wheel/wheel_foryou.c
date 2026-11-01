/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - "For You", the first thing in the main menu.
 *
 * Continue listening, today's mix and a handful of songs picked for today,
 * "similar to" the last song, recent albums, moods, a mixtape of N minutes,
 * forgotten gems, never played, recently added, favourites, decades, a
 * smart list builder and your stats. All from your own library and
 * listening history: nothing leaves the player.
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
#include "settings.h"
#include "audio.h"
#include "playlist.h"
#include "root_menu.h"
#include "string-extra.h"
#include "timefuncs.h"
#include "misc.h"
#include "wheel_library.h"
#include "wheel_listview.h"
#include "wheel_stats.h"
#include "wheel_screens.h"

enum fy_row {
    FY_CONTINUE, FY_DAILY, FY_SIMILAR, FY_ALBUMS, FY_MOODS, FY_MIXTAPE,
    FY_FORGOTTEN, FY_NEVER, FY_RECENT, FY_FAVOURITES, FY_DECADES, FY_SMART,
    FY_STATS, FY_PICKS_HEADER, FY_PICK, /* FY_PICK repeats */
};

#define N_PICKS 6
WHEEL_LIST_DEFINE(fy_daily, 40);

static struct {
    int rows[FY_PICK + N_PICKS];
    int n_rows;
    char last_path[MAX_PATH];
    char last_title[96];
    bool have_last;
    int result;
} fy;

static const int mood_lang[MOOD_COUNT] = {
    LANG_WHEEL_MOOD_ENERGY, LANG_WHEEL_MOOD_CALM, LANG_WHEEL_MOOD_PARTY,
    LANG_WHEEL_MOOD_FOCUS, LANG_WHEEL_MOOD_MELANCHOLY,
};

static void build_rows(void)
{
    fy.n_rows = 0;
    fy.have_last = wheel_history_last(fy.last_path, sizeof(fy.last_path),
                                      fy.last_title, sizeof(fy.last_title));
    fy.rows[fy.n_rows++] = FY_CONTINUE;
    fy.rows[fy.n_rows++] = FY_DAILY;
    if (fy.have_last)
        fy.rows[fy.n_rows++] = FY_SIMILAR;
    fy.rows[fy.n_rows++] = FY_ALBUMS;
    fy.rows[fy.n_rows++] = FY_MOODS;
    fy.rows[fy.n_rows++] = FY_MIXTAPE;
    fy.rows[fy.n_rows++] = FY_FORGOTTEN;
    fy.rows[fy.n_rows++] = FY_NEVER;
    fy.rows[fy.n_rows++] = FY_RECENT;
    fy.rows[fy.n_rows++] = FY_FAVOURITES;
    fy.rows[fy.n_rows++] = FY_DECADES;
    fy.rows[fy.n_rows++] = FY_SMART;
    fy.rows[fy.n_rows++] = FY_STATS;
    if (fy_daily.n > 0)
    {
        fy.rows[fy.n_rows++] = FY_PICKS_HEADER;
        for (int i = 0; i < N_PICKS && i < fy_daily.n; i++)
            fy.rows[fy.n_rows++] = FY_PICK + i;
    }
}

static const char *fy_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    int row = fy.rows[item];
    if (row >= FY_PICK)
    {
        struct wheel_track *t = &fy_daily.t[row - FY_PICK];
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s",
                 t->title[0] ? t->title : strrchr(t->path, '/') + 1, t->artist);
        return buf;
    }
    switch (row)
    {
        case FY_CONTINUE:
        {
            struct mp3entry *id3 = audio_current_track();
            const char *what = NULL;
            if ((audio_status() & AUDIO_STATUS_PLAY) && id3 && id3->title)
                what = id3->title;
            else if (fy.have_last)
                what = fy.last_title;
            if (what)
                snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_CONTINUE), what);
            else
                strlcpy(buf, str(LANG_WHEEL_CONTINUE), len);
            return buf;
        }
        case FY_DAILY:      return str(LANG_WHEEL_DAILY_MIX);
        case FY_SIMILAR:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_SIMILAR_TO), fy.last_title);
            return buf;
        case FY_ALBUMS:     return str(LANG_WHEEL_RECENT_ALBUMS);
        case FY_MOODS:      return str(LANG_WHEEL_MOODS);
        case FY_MIXTAPE:    return str(LANG_WHEEL_MIXTAPE);
        case FY_FORGOTTEN:  return str(LANG_WHEEL_FORGOTTEN);
        case FY_NEVER:      return str(LANG_WHEEL_NEVER_PLAYED);
        case FY_RECENT:     return str(LANG_WHEEL_RECENTLY_ADDED);
        case FY_FAVOURITES: return str(LANG_WHEEL_FAVOURITES);
        case FY_DECADES:    return str(LANG_WHEEL_DECADES);
        case FY_SMART:      return str(LANG_WHEEL_SMART_LIST);
        case FY_STATS:      return str(LANG_WHEEL_STATS);
        case FY_PICKS_HEADER:
            snprintf(buf, len, "— %s —", str(LANG_WHEEL_PICKS_TODAY));
            return buf;
    }
    return "";
}

/* rows that open something get the chevron; songs and the header don't */
static bool fy_get_nav(int item, void *data)
{
    (void)data;
    int row = fy.rows[item];
    return row != FY_CONTINUE && row != FY_PICKS_HEADER && row < FY_PICK;
}

static int show_mix(struct wheel_list *l, const char *title, const char *subtitle, bool ok)
{
    if (!ok || l->n == 0)
    {
        splash(HZ * 2, wheel_library_count() > 0 ? ID2P(LANG_WHEEL_LIST_EMPTY)
                                                 : ID2P(LANG_WHEEL_NEED_LIBRARY));
        return 0;
    }
    struct wheel_lv lv = { .title = title, .subtitle = subtitle, .kind = LV_MIX, .list = l };
    return wheel_listview_show(&lv);
}

static int open_rule(enum wheel_rule_kind kind, int lang)
{
    struct wheel_rule r = { .kind = kind, .family = -1 };
    struct wheel_list *l = wheel_lv_list();
    splash(0, ID2P(LANG_WHEEL_MAKING_MIX));
    bool ok = wheel_mix_rule(&r, l);
    return show_mix(l, str(lang), str(LANG_WHEEL_FOR_YOU), ok);
}

static int pick_mood(int title_lang)
{
    const char *items[MOOD_COUNT];
    for (int i = 0; i < MOOD_COUNT; i++)
        items[i] = str(mood_lang[i]);
    return wheel_pick(str(title_lang), items, MOOD_COUNT, 0);
}

static int recent_albums(void)
{
    static char paths[12][MAX_PATH];
    static char names[12][96];
    int n = wheel_history_recent_albums(paths, names, 12);
    if (n == 0)
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_STATS_EMPTY));
        return 0;
    }
    const char *items[12];
    for (int i = 0; i < n; i++)
        items[i] = names[i];
    int sel = 0;
    while (1)
    {
        sel = wheel_pick(str(LANG_WHEEL_RECENT_ALBUMS), items, n, sel);
        if (sel < 0)
            return 0;
        char title[96];
        strlcpy(title, names[sel], sizeof(title));
        char *dot = strstr(title, " · ");
        const char *artist = dot ? dot + strlen(" · ") : NULL;
        if (dot) *dot = '\0';
        int r = wheel_open_folder(paths[sel], title, artist);
        if (r)
            return r;
    }
}

static int decades(void)
{
    static int dec[20];
    static char names[20][16];
    const char *items[20];
    splash(0, ID2P(LANG_WAIT));
    int n = wheel_library_decades(dec, 20);
    if (!n)
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_NEED_LIBRARY));
        return 0;
    }
    for (int i = 0; i < n; i++)
    {
        snprintf(names[i], sizeof(names[i]), "%ds", dec[i]);
        items[i] = names[i];
    }
    int sel = 0;
    while ((sel = wheel_pick(str(LANG_WHEEL_DECADES), items, n, sel)) >= 0)
    {
        struct wheel_rule r = { .kind = RULE_ANY, .family = -1, .decade = dec[sel] };
        struct wheel_list *l = wheel_lv_list();
        splash(0, ID2P(LANG_WHEEL_MAKING_MIX));
        int ret = show_mix(l, names[sel], str(LANG_WHEEL_DECADES), wheel_mix_rule(&r, l));
        if (ret)
            return ret;
    }
    return 0;
}

/* --------------------------- smart list builder --------------------------- */

enum { SL_GENRE, SL_DECADE, SL_LENGTH, SL_KIND, SL_CREATE, SL_COUNT };
static struct {
    int fam_idx, dec_idx, len_idx, kind_idx;
    int fams[16], n_fams;
    int decs[20], n_decs;
} sl;
static const int sl_kinds[] = { RULE_ANY, RULE_FAVOURITES, RULE_NEVER_PLAYED, RULE_FORGOTTEN, RULE_RECENT };
static const int sl_kind_lang[] = { LANG_WHEEL_ANY, LANG_WHEEL_FAVOURITES, LANG_WHEEL_NEVER_PLAYED,
                                    LANG_WHEEL_FORGOTTEN, LANG_WHEEL_RECENTLY_ADDED };
static const int sl_len_lang[] = { LANG_WHEEL_ANY, LANG_WHEEL_SHORT_SONGS, LANG_WHEEL_LONG_SONGS };

static const char *sl_value(int row, char *buf, size_t len)
{
    switch (row)
    {
        case SL_GENRE:
            return sl.fam_idx == 0 ? (const char *)str(LANG_WHEEL_ANY) : wheel_family_name(sl.fams[sl.fam_idx - 1]);
        case SL_DECADE:
            if (sl.dec_idx == 0) return str(LANG_WHEEL_ANY);
            snprintf(buf, len, "%ds", sl.decs[sl.dec_idx - 1]);
            return buf;
        case SL_LENGTH: return str(sl_len_lang[sl.len_idx]);
        case SL_KIND:   return str(sl_kind_lang[sl.kind_idx]);
    }
    return "";
}

static const char *sl_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    static const int lbl[SL_COUNT] = { LANG_WHEEL_GENRE, LANG_WHEEL_DECADE, LANG_WHEEL_LENGTH,
                                       LANG_WHEEL_WHICH_SONGS, LANG_WHEEL_CREATE_LIST };
    if (item == SL_CREATE)
        return str(lbl[item]);
    char v[40];
    snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(lbl[item]), sl_value(item, v, sizeof(v)));
    return buf;
}

static int sl_result;
static int sl_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
    {
        sl_result = GO_TO_ROOT;
        return ACTION_STD_CANCEL;
    }
    if (action != ACTION_STD_OK)
        return action;
    switch (gui_synclist_get_sel_pos(lists))
    {
        case SL_GENRE:  sl.fam_idx = (sl.fam_idx + 1) % (sl.n_fams + 1); break;
        case SL_DECADE: sl.dec_idx = (sl.dec_idx + 1) % (sl.n_decs + 1); break;
        case SL_LENGTH: sl.len_idx = (sl.len_idx + 1) % 3; break;
        case SL_KIND:   sl.kind_idx = (sl.kind_idx + 1) % (int)ARRAYLEN(sl_kinds); break;
        case SL_CREATE:
        {
            struct wheel_rule r = {
                .kind = sl_kinds[sl.kind_idx],
                .family = sl.fam_idx ? sl.fams[sl.fam_idx - 1] : -1,
                .decade = sl.dec_idx ? sl.decs[sl.dec_idx - 1] : 0,
                .max_len_s = sl.len_idx == 1 ? 210 : 0,
                .min_len_s = sl.len_idx == 2 ? 360 : 0,
            };
            static char title[96];
            char v[40];
            /* name it after what was picked: "Rock · 90s · Favourites" */
            title[0] = '\0';
            for (int row = SL_GENRE; row <= SL_KIND; row++)
            {
                bool any = (row == SL_GENRE && !sl.fam_idx) || (row == SL_DECADE && !sl.dec_idx) ||
                           (row == SL_LENGTH && !sl.len_idx) || (row == SL_KIND && !sl.kind_idx);
                if (any) continue;
                if (title[0]) strlcat(title, " · ", sizeof(title));
                strlcat(title, sl_value(row, v, sizeof(v)), sizeof(title));
            }
            if (!title[0]) strlcpy(title, str(LANG_WHEEL_SMART_LIST), sizeof(title));
            struct wheel_list *l = wheel_lv_list();
            splash(0, ID2P(LANG_WHEEL_MAKING_MIX));
            sl_result = show_mix(l, title, str(LANG_WHEEL_SMART_LIST), wheel_mix_rule(&r, l));
            if (sl_result)
                return ACTION_STD_CANCEL;
            gui_synclist_set_title(lists, str(LANG_WHEEL_SMART_LIST), Icon_NOICON);
            break;
        }
    }
    return ACTION_REDRAW;
}

static int smart_list(void)
{
    splash(0, ID2P(LANG_WAIT));
    sl.n_fams = wheel_library_families(sl.fams, 16);
    sl.n_decs = wheel_library_decades(sl.decs, 20);
    if (!sl.n_fams && !sl.n_decs && !wheel_library_count())
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_NEED_LIBRARY));
        return 0;
    }
    sl.fam_idx = MIN(sl.fam_idx, sl.n_fams);
    sl.dec_idx = MIN(sl.dec_idx, sl.n_decs);
    struct simplelist_info info;
    simplelist_info_init(&info, str(LANG_WHEEL_SMART_LIST), SL_COUNT, NULL);
    info.get_name = sl_get_name;
    info.action_callback = sl_action;
    sl_result = 0;
    simplelist_show_list(&info);
    return sl_result;
}

static int mixtape(void)
{
    int mood = pick_mood(LANG_WHEEL_MIXTAPE);
    if (mood < 0)
        return 0;
    static const int mins[] = { 20, 30, 45, 60, 90 };
    static char names[5][16];
    const char *items[5];
    for (int i = 0; i < 5; i++)
    {
        snprintf(names[i], sizeof(names[i]), "%d min", mins[i]);
        items[i] = names[i];
    }
    int m = wheel_pick(str(LANG_WHEEL_LENGTH), items, 5, 1);
    if (m < 0)
        return 0;
    struct wheel_list *l = wheel_lv_list();
    splash(0, ID2P(LANG_WHEEL_MAKING_MIX));
    bool ok = wheel_mix_mood(mood, mins[m] * 60000UL, l);
    static char title[64];
    snprintf(title, sizeof(title), "%s · %d min", str(mood_lang[mood]), mins[m]);
    return show_mix(l, title, str(LANG_WHEEL_MIXTAPE), ok);
}

static int fy_open(int row)
{
    if (row >= FY_PICK)
    {   /* play today's mix from that song */
        return wheel_play_list(&fy_daily, row - FY_PICK, false) ? GO_TO_WPS : 0;
    }
    switch (row)
    {
        case FY_CONTINUE:
            if (audio_status() & AUDIO_STATUS_PLAY)
                return GO_TO_WPS;
            return GO_TO_WPS; /* the root loop resumes the last playlist */
        case FY_DAILY:
            return show_mix(&fy_daily, str(LANG_WHEEL_DAILY_MIX), str(LANG_WHEEL_FOR_YOU),
                            fy_daily.n > 0);
        case FY_SIMILAR:
        {
            struct wheel_list *l = wheel_lv_list();
            splash(0, ID2P(LANG_WHEEL_MAKING_MIX));
            return show_mix(l, fy.last_title, str(LANG_WHEEL_SIMILAR),
                            wheel_mix_similar(fy.last_path, l));
        }
        case FY_ALBUMS: return recent_albums();
        case FY_MOODS:
        {
            int m = 0;
            while ((m = pick_mood(LANG_WHEEL_MOODS)) >= 0)
            {
                struct wheel_list *l = wheel_lv_list();
                splash(0, ID2P(LANG_WHEEL_MAKING_MIX));
                int r = show_mix(l, str(mood_lang[m]), str(LANG_WHEEL_MOODS),
                                 wheel_mix_mood(m, 0, l));
                if (r) return r;
            }
            return 0;
        }
        case FY_MIXTAPE:    return mixtape();
        case FY_FORGOTTEN:  return open_rule(RULE_FORGOTTEN, LANG_WHEEL_FORGOTTEN);
        case FY_NEVER:      return open_rule(RULE_NEVER_PLAYED, LANG_WHEEL_NEVER_PLAYED);
        case FY_RECENT:     return open_rule(RULE_RECENT, LANG_WHEEL_RECENTLY_ADDED);
        case FY_FAVOURITES: return open_rule(RULE_FAVOURITES, LANG_WHEEL_FAVOURITES);
        case FY_DECADES:    return decades();
        case FY_SMART:      return smart_list();
        case FY_STATS:      return wheel_stats_screen();
    }
    return 0;
}

static int fy_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, fy_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    int item = gui_synclist_get_sel_pos(lists);
    if (item < 0 || item >= fy.n_rows || fy.rows[item] == FY_PICKS_HEADER)
        return ACTION_REDRAW;
    fy.result = fy_open(fy.rows[item]);
    if (fy.result == GO_TO_PREVIOUS)
        fy.result = 0;
    if (fy.result)
        return ACTION_STD_CANCEL;
    build_rows();
    gui_synclist_set_nb_items(lists, fy.n_rows);
    gui_synclist_set_title(lists, str(LANG_WHEEL_FOR_YOU), Icon_NOICON);
    return ACTION_REDRAW;
}

int wheel_foryou_screen(void)
{
    static int last_day = -1;
    struct tm *tm = get_time();
    int day = tm ? tm->tm_year * 400 + tm->tm_yday : 0;
    if (day != last_day || fy_daily.n == 0)
    {   /* today's picks: the same all day */
        splash(0, ID2P(LANG_WHEEL_PREPARING_PICKS));
        fy_daily.n = 0;
        wheel_mix_daily(&fy_daily);
        last_day = day;
    }
    build_rows();
    fy.result = 0;
    struct simplelist_info info;
    simplelist_info_init(&info, str(LANG_WHEEL_FOR_YOU), fy.n_rows, NULL);
    info.get_name = fy_get_name;
    info.action_callback = fy_action;
    push_current_activity(ACTIVITY_DATABASEBROWSER);
    simplelist_show_list(&info);
    if (fy.result)
        pop_current_activity_without_refresh();
    else
        pop_current_activity();
    return fy.result;
}
