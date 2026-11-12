/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - mixes made from your own library (no internet):
 *
 *   "Similar to this" (Genius-style): genre, family of genres, artist,
 *   era and how much you play it, with variety rules so one artist
 *   doesn't take over.
 *   Moods: energy / calm / party / focus / melancholy, decades.
 *   Rules: never played, forgotten gems, recently added, favourites,
 *   short songs, and any combination of genre family + decade + length.
 *   Mixtape: fill N minutes with a mood.
 *   Daily mix: your recent top artists plus music like them.
 *
 * Everything runs over a compact in-memory index of the database.
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
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "file.h"
#include "misc.h"
#include "splash.h"
#include "settings.h"
#include "timefuncs.h"
#include "string-extra.h"
#ifdef HAVE_TAGCACHE
#include "tagcache.h"
#endif
#include "metadata.h"
#include "wheel_stats.h"
#include "wheel_library.h"

#ifdef HAVE_TAGCACHE

#define MAX_TRACKS 12000

struct trk {
    int32_t idx;
    uint32_t artist, album, genre;  /* hashes */
    uint32_t lastplayed;            /* database play serial */
    uint32_t len_ms;
    uint16_t year;
    uint16_t plays;
    uint16_t commit;
    uint8_t  family;
    uint8_t  rating;
};

static struct trk lib[MAX_TRACKS];
static int n_lib;
static uint32_t max_serial;
static uint16_t max_commit;
static long built_tick = -1;

/* ------------------------- genre families ------------------------- */

enum family {
    FAM_OTHER = 0, FAM_ROCK, FAM_HARD, FAM_ELECTRONIC, FAM_DANCE, FAM_HIPHOP,
    FAM_POP, FAM_LATIN, FAM_JAZZ, FAM_CLASSICAL, FAM_AMBIENT, FAM_FOLK,
    FAM_INDIE, FAM_SOUNDTRACK, FAM_SOUL, FAM_COUNT
};

static const int family_names[FAM_COUNT] = {
    LANG_WHEEL_FAM_OTHER, LANG_WHEEL_FAM_ROCK, LANG_WHEEL_FAM_HARD, LANG_WHEEL_FAM_ELECTRONIC,
    LANG_WHEEL_FAM_DANCE, LANG_WHEEL_FAM_HIPHOP, LANG_WHEEL_FAM_POP, LANG_WHEEL_FAM_LATIN,
    LANG_WHEEL_FAM_JAZZ, LANG_WHEEL_FAM_CLASSICAL, LANG_WHEEL_FAM_AMBIENT, LANG_WHEEL_FAM_FOLK,
    LANG_WHEEL_FAM_INDIE, LANG_WHEEL_FAM_SOUNDTRACK, LANG_WHEEL_FAM_SOUL,
};

int wheel_family_count(void) { return FAM_COUNT; }
const char *wheel_family_name(int fam)
{
    return (fam >= 0 && fam < FAM_COUNT) ? (const char *)str(family_names[fam]) : "";
}

static const struct { const char *kw; uint8_t fam; } genre_kw[] = {
    /* the more specific words first */
    { "post-rock", FAM_AMBIENT }, { "post rock", FAM_AMBIENT },
    { "dream pop", FAM_INDIE }, { "shoegaze", FAM_INDIE }, { "synthpop", FAM_POP },
    { "lo-fi", FAM_AMBIENT }, { "lofi", FAM_AMBIENT }, { "chill", FAM_AMBIENT },
    { "trip hop", FAM_ELECTRONIC }, { "trip-hop", FAM_ELECTRONIC },
    { "hip hop", FAM_HIPHOP }, { "hip-hop", FAM_HIPHOP }, { "rap", FAM_HIPHOP },
    { "trap", FAM_HIPHOP }, { "grime", FAM_HIPHOP },
    { "reggaeton", FAM_LATIN }, { "latin", FAM_LATIN }, { "salsa", FAM_LATIN },
    { "bachata", FAM_LATIN }, { "cumbia", FAM_LATIN }, { "flamenco", FAM_LATIN },
    { "bossa", FAM_LATIN }, { "samba", FAM_LATIN }, { "urbano", FAM_LATIN },
    { "metal", FAM_HARD }, { "punk", FAM_HARD }, { "hardcore", FAM_HARD },
    { "grunge", FAM_HARD }, { "hard rock", FAM_HARD },
    { "house", FAM_DANCE }, { "techno", FAM_DANCE }, { "trance", FAM_DANCE },
    { "edm", FAM_DANCE }, { "dance", FAM_DANCE }, { "disco", FAM_DANCE },
    { "drum", FAM_DANCE }, { "dubstep", FAM_DANCE }, { "garage", FAM_DANCE },
    { "electro", FAM_ELECTRONIC }, { "synth", FAM_ELECTRONIC }, { "idm", FAM_ELECTRONIC },
    { "ambient", FAM_AMBIENT }, { "new age", FAM_AMBIENT }, { "downtempo", FAM_AMBIENT },
    { "classical", FAM_CLASSICAL }, { "clasica", FAM_CLASSICAL }, { "baroque", FAM_CLASSICAL },
    { "opera", FAM_CLASSICAL }, { "piano", FAM_CLASSICAL }, { "orchestr", FAM_CLASSICAL },
    { "jazz", FAM_JAZZ }, { "swing", FAM_JAZZ }, { "bebop", FAM_JAZZ },
    { "soundtrack", FAM_SOUNDTRACK }, { "score", FAM_SOUNDTRACK }, { "ost", FAM_SOUNDTRACK },
    { "folk", FAM_FOLK }, { "acoustic", FAM_FOLK }, { "country", FAM_FOLK },
    { "singer", FAM_FOLK }, { "americana", FAM_FOLK },
    { "indie", FAM_INDIE }, { "alternative", FAM_INDIE }, { "emo", FAM_INDIE },
    { "soul", FAM_SOUL }, { "funk", FAM_SOUL }, { "r&b", FAM_SOUL }, { "rnb", FAM_SOUL },
    { "blues", FAM_SOUL }, { "gospel", FAM_SOUL }, { "motown", FAM_SOUL },
    { "k-pop", FAM_POP }, { "pop", FAM_POP },
    { "rock", FAM_ROCK },
};

static uint8_t family_of(const char *genre)
{
    char g[64];
    int i = 0;
    for (; genre[i] && i < 63; i++)
        g[i] = (genre[i] >= 'A' && genre[i] <= 'Z') ? genre[i] + 32 : genre[i];
    g[i] = 0;
    if (!g[0]) return FAM_OTHER;
    for (unsigned k = 0; k < ARRAYLEN(genre_kw); k++)
        if (strstr(g, genre_kw[k].kw))
            return genre_kw[k].fam;
    return FAM_OTHER;
}

/* how close two families feel (0..2) */
static int family_affinity(int a, int b)
{
    if (a == b) return 2;
    static const uint8_t near[][2] = {
        { FAM_ROCK, FAM_HARD }, { FAM_ROCK, FAM_INDIE }, { FAM_ELECTRONIC, FAM_DANCE },
        { FAM_ELECTRONIC, FAM_AMBIENT }, { FAM_POP, FAM_DANCE }, { FAM_POP, FAM_INDIE },
        { FAM_HIPHOP, FAM_SOUL }, { FAM_LATIN, FAM_DANCE }, { FAM_LATIN, FAM_POP },
        { FAM_JAZZ, FAM_SOUL }, { FAM_CLASSICAL, FAM_SOUNDTRACK }, { FAM_AMBIENT, FAM_SOUNDTRACK },
        { FAM_FOLK, FAM_INDIE }, { FAM_FOLK, FAM_SOUL }, { FAM_HIPHOP, FAM_POP },
    };
    for (unsigned i = 0; i < ARRAYLEN(near); i++)
        if ((near[i][0] == a && near[i][1] == b) || (near[i][0] == b && near[i][1] == a))
            return 1;
    return 0;
}

/* moods = sets of families */
static bool mood_has(enum wheel_mood m, int fam)
{
    switch (m)
    {
        case MOOD_ENERGY: return fam == FAM_ROCK || fam == FAM_HARD || fam == FAM_DANCE ||
                                 fam == FAM_HIPHOP || fam == FAM_ELECTRONIC || fam == FAM_LATIN;
        case MOOD_CALM:   return fam == FAM_AMBIENT || fam == FAM_FOLK || fam == FAM_JAZZ ||
                                 fam == FAM_CLASSICAL || fam == FAM_SOUNDTRACK;
        case MOOD_PARTY:  return fam == FAM_DANCE || fam == FAM_POP || fam == FAM_LATIN ||
                                 fam == FAM_HIPHOP || fam == FAM_SOUL;
        case MOOD_FOCUS:  return fam == FAM_AMBIENT || fam == FAM_CLASSICAL ||
                                 fam == FAM_SOUNDTRACK || fam == FAM_JAZZ || fam == FAM_ELECTRONIC;
        case MOOD_MELANCHOLY: return fam == FAM_INDIE || fam == FAM_FOLK || fam == FAM_SOUL ||
                                 fam == FAM_AMBIENT;
        default: return true;
    }
}

static uint32_t hash_ci(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
    {
        unsigned char c = *s++;
        if (c >= 'A' && c <= 'Z') c += 32;
        h = (h ^ c) * 16777619u;
    }
    return h ? h : 1;
}

/* --------------------------- the index --------------------------- */

#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
/* the index from the database read in big blocks (see tagcache_walk) */
static bool walk_add(int idx_id, const int32_t *val, const char *const *s, void *data)
{
    (void)data;
    if (n_lib >= MAX_TRACKS)
        return false;
    struct trk *t = &lib[n_lib];
    t->idx = idx_id;
    t->year = val[tag_year];
    t->plays = MIN(val[tag_playcount], 65535);
    t->lastplayed = val[tag_lastplayed];
    t->len_ms = val[tag_length];
    t->commit = val[tag_commitid];
    t->rating = val[tag_rating];
    /* s: albumartist, artist, album, genre */
    const char *a = (!s[0][0] || !strcmp(s[0], "<Untagged>")) ? s[1] : s[0];
    t->artist = hash_ci(a);
    t->album = hash_ci(s[2]) ^ t->artist;
    const char *g = strcmp(s[3], "<Untagged>") ? s[3] : "";
    t->genre = hash_ci(g);
    t->family = family_of(g);
    if (t->lastplayed > max_serial) max_serial = t->lastplayed;
    if (t->commit > max_commit) max_commit = t->commit;
    n_lib++;
    return true;
}
#endif

static bool build_index(void)
{
    if (built_tick >= 0 && TIME_BEFORE(current_tick, built_tick + HZ * 600))
        return n_lib > 0; /* fresh enough */
    struct tagcache_stat *st = tagcache_get_stat();
    if (!st->ready)
        return false;
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    {
        static const int tags[] = { tag_albumartist, tag_artist, tag_album, tag_genre };
        n_lib = 0; max_serial = 0; max_commit = 0;
        if (tagcache_walk(tags, 4, walk_add, NULL))
        {
            built_tick = current_tick;
            return n_lib > 0;
        }
        /* fall back to the search below */
    }
#endif
    struct tagcache_search tcs;
    char buf[MAX_PATH];
    n_lib = 0; max_serial = 0; max_commit = 0;
    if (!tagcache_search(&tcs, tag_filename))
        return false;
    while (n_lib < MAX_TRACKS && tagcache_get_next(&tcs, buf, sizeof(buf)))
    {
        struct trk *t = &lib[n_lib];
        t->idx = tcs.idx_id;
        t->year = tagcache_get_numeric(&tcs, tag_year);
        t->plays = MIN(tagcache_get_numeric(&tcs, tag_playcount), 65535);
        t->lastplayed = tagcache_get_numeric(&tcs, tag_lastplayed);
        t->len_ms = tagcache_get_numeric(&tcs, tag_length);
        t->commit = tagcache_get_numeric(&tcs, tag_commitid);
        t->rating = tagcache_get_numeric(&tcs, tag_rating);
        if (!tagcache_retrieve(&tcs, tcs.idx_id, tag_albumartist, buf, sizeof(buf)) ||
            !buf[0] || !strcmp(buf, "<Untagged>"))
            tagcache_retrieve(&tcs, tcs.idx_id, tag_artist, buf, sizeof(buf));
        t->artist = hash_ci(buf);
        tagcache_retrieve(&tcs, tcs.idx_id, tag_album, buf, sizeof(buf));
        t->album = hash_ci(buf) ^ t->artist;
        if (!tagcache_retrieve(&tcs, tcs.idx_id, tag_genre, buf, sizeof(buf)) ||
            !strcmp(buf, "<Untagged>"))
            buf[0] = 0;
        t->genre = hash_ci(buf);
        t->family = family_of(buf);
        if (t->lastplayed > max_serial) max_serial = t->lastplayed;
        if (t->commit > max_commit) max_commit = t->commit;
        n_lib++;
        if ((n_lib & 255) == 0)
            yield();
    }
    tagcache_search_finish(&tcs);
    built_tick = current_tick;
    return n_lib > 0;
}

void wheel_library_invalidate(void)
{
    built_tick = -1;
}

static int find_by_path(const char *path)
{
    struct tagcache_search tcs;
    char buf[MAX_PATH];
    int found = -1;
    if (!tagcache_search(&tcs, tag_filename))
        return -1;
    int i = 0;
    while (tagcache_get_next(&tcs, buf, sizeof(buf)))
    {
        if (!strcmp(buf, path))
        {
            for (int k = 0; k < n_lib; k++)
                if (lib[k].idx == tcs.idx_id) { found = k; break; }
            break;
        }
        if ((++i & 255) == 0) yield();
    }
    tagcache_search_finish(&tcs);
    return found;
}

/* ---------------------- filling the output list ---------------------- */

static void out_add(struct wheel_list *out, int k)
{
    if (out->n >= out->max)
        return;
    struct tagcache_search tcs;
    struct wheel_track *w = &out->t[out->n];
    if (!tagcache_search(&tcs, tag_filename))
        return;
    bool ok = tagcache_retrieve(&tcs, lib[k].idx, tag_filename, w->path, sizeof(w->path));
    if (ok)
    {
        if (!tagcache_retrieve(&tcs, lib[k].idx, tag_title, w->title, sizeof(w->title)))
            w->title[0] = 0;
        if (!tagcache_retrieve(&tcs, lib[k].idx, tag_artist, w->artist, sizeof(w->artist)))
            w->artist[0] = 0;
        w->len_ms = lib[k].len_ms;
        w->meta = true;
        out->n++;
        out->total = out->n;
    }
    tagcache_search_finish(&tcs);
}

/* candidate scoring buffer */
static struct { int k; int score; } cand[MAX_TRACKS];
static int n_cand;

static void cand_sort(void)
{
    /* insertion-ish: shell sort, descending by score */
    for (int gap = n_cand / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n_cand; i++)
        {
            typeof(cand[0]) tmp = cand[i];
            int j = i;
            while (j >= gap && cand[j - gap].score < tmp.score)
            {
                cand[j] = cand[j - gap];
                j -= gap;
            }
            cand[j] = tmp;
        }
}

/* take the best, max per_artist songs per artist, max per_album per album */
static void take_varied(struct wheel_list *out, int want, int per_artist, int per_album,
                        unsigned long max_ms)
{
    static uint32_t a_hash[64], b_hash[128];
    static uint8_t a_cnt[64], b_cnt[128];
    int na = 0, nb = 0;
    unsigned long total = 0;
    for (int i = 0; i < n_cand && out->n < want; i++)
    {
        struct trk *t = &lib[cand[i].k];
        int ai, bi;
        for (ai = 0; ai < na && a_hash[ai] != t->artist; ai++);
        for (bi = 0; bi < nb && b_hash[bi] != t->album; bi++);
        if (ai < na && a_cnt[ai] >= per_artist) continue;
        if (bi < nb && b_cnt[bi] >= per_album) continue;
        if (max_ms && total + t->len_ms > max_ms + 90000) continue; /* +1.5 min slack */
        if (ai == na && na < 64) { a_hash[na] = t->artist; a_cnt[na++] = 0; }
        if (bi == nb && nb < 128) { b_hash[nb] = t->album; b_cnt[nb++] = 0; }
        if (ai < 64) a_cnt[ai]++;
        if (bi < 128) b_cnt[bi]++;
        out_add(out, cand[i].k);
        total += t->len_ms;
        if (max_ms && total >= max_ms) break;
    }
    /* spread the same artist apart: simple interleave by swapping neighbours */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 1; i + 1 < out->n; i++)
            if (!strcmp(out->t[i].artist, out->t[i - 1].artist))
            {
                struct wheel_track tmp = out->t[i];
                out->t[i] = out->t[i + 1];
                out->t[i + 1] = tmp;
            }
}

static unsigned rnd(void) { return rand() & 0x7fff; }

/* ------------------------------ mixes ------------------------------ */

bool wheel_mix_similar(const char *seed_path, struct wheel_list *out)
{
    out->n = 0;
    if (!build_index())
        return false;
    int s = find_by_path(seed_path);
    if (s < 0)
        return false;
    struct trk *seed = &lib[s];
    srand(current_tick);
    n_cand = 0;
    for (int k = 0; k < n_lib; k++)
    {
        if (k == s) continue;
        struct trk *t = &lib[k];
        int sc = 0;
        if (t->genre == seed->genre && seed->genre != hash_ci("")) sc += 40;
        sc += family_affinity(t->family, seed->family) * 18;
        if (t->artist == seed->artist) sc += 22;
        if (seed->year && t->year)
        {
            int d = abs((int)t->year - (int)seed->year);
            sc += d <= 3 ? 16 : d <= 8 ? 8 : d <= 15 ? 2 : 0;
        }
        if (t->plays > 2) sc += 6;
        if (t->rating >= 7) sc += 6;
        sc += rnd() % 10;
        if (sc < 20) continue;
        cand[n_cand].k = k;
        cand[n_cand++].score = sc;
    }
    cand_sort();
    out_add(out, s); /* the seed starts the mix */
    take_varied(out, 30, 3, 2, 0);
    return out->n > 1;
}

bool wheel_mix_mood(enum wheel_mood mood, unsigned long max_ms, struct wheel_list *out)
{
    out->n = 0;
    if (!build_index())
        return false;
    srand(current_tick);
    n_cand = 0;
    for (int k = 0; k < n_lib; k++)
    {
        struct trk *t = &lib[k];
        if (!mood_has(mood, t->family)) continue;
        int sc = 10 + rnd() % 30;
        if (t->plays) sc += MIN(t->plays, 10);
        if (t->rating >= 7) sc += 8;
        cand[n_cand].k = k;
        cand[n_cand++].score = sc;
    }
    cand_sort();
    take_varied(out, max_ms ? out->max : 50, 3, 2, max_ms);
    return out->n > 0;
}

bool wheel_mix_rule(const struct wheel_rule *r, struct wheel_list *out)
{
    out->n = 0;
    if (!build_index())
        return false;
    srand(current_tick);
    n_cand = 0;
    uint32_t gem_gap = MAX(200u, (unsigned)n_lib / 3);
    for (int k = 0; k < n_lib; k++)
    {
        struct trk *t = &lib[k];
        if (r->family >= 0 && t->family != r->family) continue;
        if (r->decade && (t->year < r->decade || t->year > r->decade + 9)) continue;
        if (r->max_len_s && t->len_ms > (unsigned)r->max_len_s * 1000) continue;
        if (r->min_len_s && t->len_ms < (unsigned)r->min_len_s * 1000) continue;
        int sc = rnd() % 20;
        switch (r->kind)
        {
            case RULE_NEVER_PLAYED:
                if (t->plays) continue;
                break;
            case RULE_FORGOTTEN:
                if (t->plays < 2 || max_serial - t->lastplayed < gem_gap) continue;
                sc += MIN(t->plays, 30) * 3;            /* loved once ... */
                sc += (max_serial - t->lastplayed) / 50; /* ... and long ago */
                break;
            case RULE_RECENT:
                if (max_commit > 2 && t->commit + 1 < max_commit) continue;
                sc += t->commit * 100;
                break;
            case RULE_FAVOURITES:
                if (t->plays < 3 && t->rating < 7) continue;
                sc += t->plays * 4 + t->rating * 5;
                break;
            default:
                break;
        }
        cand[n_cand].k = k;
        cand[n_cand++].score = sc;
    }
    cand_sort();
    int per_artist = r->kind == RULE_RECENT ? 50 : 4;
    int per_album = r->kind == RULE_RECENT ? 50 : 3;
    take_varied(out, r->kind == RULE_RECENT ? 80 : 50, per_artist, per_album, 0);
    return out->n > 0;
}

/* your most played artists of the last month, and music like them */
bool wheel_mix_daily(struct wheel_list *out)
{
    out->n = 0;
    if (!build_index())
        return false;
    /* top artists by recent history */
    static uint32_t top_a[5];
    static unsigned top_c[5];
    int n_top = 0;
    char line[MAX_PATH + 512];
    char *f[9];
    int fd = open(WHEEL_HISTORY_FILE, O_RDONLY);
    if (fd >= 0)
    {
        long size = ffilesize(fd);
        if (size > 128 * 1024) { lseek(fd, size - 128 * 1024, SEEK_SET); read_line(fd, line, sizeof(line)); }
        static uint32_t ah[256]; static unsigned ac[256];
        int na = 0;
        while (read_line(fd, line, sizeof(line)) > 0)
        {
            if (!wheel_history_split(line, f) || f[4][0] != 'L') continue;
            uint32_t h = hash_ci(f[5]);
            int i;
            for (i = 0; i < na && ah[i] != h; i++);
            if (i == na) { if (na >= 256) continue; ah[na] = h; ac[na++] = 0; }
            ac[i]++;
        }
        close(fd);
        for (int i = 0; i < na; i++)
        {
            int pos = n_top;
            while (pos > 0 && top_c[pos - 1] < ac[i]) pos--;
            if (pos >= 5) continue;
            int last = MIN(n_top, 4);
            memmove(&top_a[pos + 1], &top_a[pos], (last - pos) * sizeof(top_a[0]));
            memmove(&top_c[pos + 1], &top_c[pos], (last - pos) * sizeof(top_c[0]));
            top_a[pos] = ah[i]; top_c[pos] = ac[i];
            if (n_top < 5) n_top++;
        }
    }
    /* same mix all day: seed the dice with the date */
    struct tm *tm = get_time();
    srand(tm ? (tm->tm_year * 1000 + tm->tm_yday) : 1);
    /* families those artists play */
    int fam_w[FAM_COUNT] = { 0 };
    for (int k = 0; k < n_lib; k++)
        for (int a = 0; a < n_top; a++)
            if (lib[k].artist == top_a[a]) fam_w[lib[k].family] += 5 - a;
    n_cand = 0;
    for (int k = 0; k < n_lib; k++)
    {
        struct trk *t = &lib[k];
        int sc = rnd() % 25;
        for (int a = 0; a < n_top; a++)
            if (t->artist == top_a[a]) sc += 30 - a * 4;
        sc += MIN(fam_w[t->family], 40);
        if (!n_top) sc += MIN(t->plays, 20);
        cand[n_cand].k = k;
        cand[n_cand++].score = sc;
    }
    cand_sort();
    take_varied(out, 40, 4, 2, 0);
    return out->n > 0;
}

int wheel_library_count(void)
{
    build_index();
    return n_lib;
}

/* decades that have at least a few songs, newest first */
int wheel_library_decades(int *out, int max)
{
    if (!build_index())
        return 0;
    int counts[20] = { 0 }; /* 1900..2090 */
    for (int k = 0; k < n_lib; k++)
        if (lib[k].year >= 1900 && lib[k].year < 2100)
            counts[(lib[k].year - 1900) / 10]++;
    int n = 0;
    for (int i = 19; i >= 0 && n < max; i--)
        if (counts[i] >= 3)
            out[n++] = 1900 + i * 10;
    return n;
}

/* genre families present in the library */
int wheel_library_families(int *out, int max)
{
    if (!build_index())
        return 0;
    int counts[FAM_COUNT] = { 0 };
    for (int k = 0; k < n_lib; k++)
        counts[lib[k].family]++;
    int n = 0;
    for (int f = 1; f < FAM_COUNT && n < max; f++)
        if (counts[f] >= 3)
            out[n++] = f;
    if (counts[0] >= 3 && n < max)
        out[n++] = 0;
    return n;
}

/* one pass over the database for every track in the list */
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
enum { FM_HS = 4096 };
static bool fill_meta_one(int idx_id, const int32_t *val, const char *const *s, void *data)
{
    (void)idx_id;
    struct fm { struct wheel_list *l; int16_t *slot; int missing; } *fm = data;
    unsigned h = hash_ci(s[0]) & (FM_HS - 1);
    while (fm->slot[h] >= 0)
    {
        struct wheel_track *t = &fm->l->t[fm->slot[h]];
        if (!t->meta && !strcasecmp(t->path, s[0]))
        {
            strlcpy(t->title, strcmp(s[1], "<Untagged>") ? s[1] : "", sizeof(t->title));
            strlcpy(t->artist, strcmp(s[2], "<Untagged>") ? s[2] : "", sizeof(t->artist));
            t->len_ms = val[tag_length];
            t->meta = true;
            fm->missing--;
        }
        h = (h + 1) & (FM_HS - 1);
    }
    return fm->missing > 0;
}
#endif

void wheel_list_fill_meta(struct wheel_list *l)
{
    if (l->n <= 0 || !tagcache_get_stat()->ready)
        return;
    enum { HS = 4096 };
    static int16_t slot[HS];
    memset(slot, -1, sizeof(slot));
    int missing = 0;
    for (int i = 0; i < l->n; i++)
    {
        if (l->t[i].meta) continue;
        unsigned h = hash_ci(l->t[i].path) & (HS - 1);
        while (slot[h] >= 0) h = (h + 1) & (HS - 1);
        slot[h] = i;
        missing++;
    }
    if (!missing)
        return;
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
    {
        static const int tags[] = { tag_filename, tag_title, tag_artist };
        struct fm { struct wheel_list *l; int16_t *slot; int missing; } fm = { l, slot, missing };
        if (tagcache_walk(tags, 3, fill_meta_one, &fm))
            return;
    }
#endif
    struct tagcache_search tcs;
    char buf[MAX_PATH];
    if (!tagcache_search(&tcs, tag_filename))
        return;
    int seen = 0;
    while (missing > 0 && tagcache_get_next(&tcs, buf, sizeof(buf)))
    {
        if ((++seen & 255) == 0) yield();
        unsigned h = hash_ci(buf) & (HS - 1);
        while (slot[h] >= 0)
        {
            struct wheel_track *t = &l->t[slot[h]];
            if (!t->meta && !strcasecmp(t->path, buf))
            {
                if (!tagcache_retrieve(&tcs, tcs.idx_id, tag_title, t->title, sizeof(t->title)) ||
                    !strcmp(t->title, "<Untagged>"))
                    t->title[0] = 0;
                if (!tagcache_retrieve(&tcs, tcs.idx_id, tag_artist, t->artist, sizeof(t->artist)) ||
                    !strcmp(t->artist, "<Untagged>"))
                    t->artist[0] = 0;
                t->len_ms = tagcache_get_numeric(&tcs, tag_length);
                t->meta = true;
                missing--;
            }
            h = (h + 1) & (HS - 1);
        }
    }
    tagcache_search_finish(&tcs);
}

#else /* !HAVE_TAGCACHE */
int wheel_library_decades(int *out, int max) { (void)out; (void)max; return 0; }
int wheel_library_families(int *out, int max) { (void)out; (void)max; return 0; }
void wheel_list_fill_meta(struct wheel_list *l) { (void)l; }
int wheel_family_count(void) { return 0; }
const char *wheel_family_name(int fam) { (void)fam; return ""; }
bool wheel_mix_similar(const char *p, struct wheel_list *o) { (void)p; o->n = 0; return false; }
bool wheel_mix_mood(enum wheel_mood m, unsigned long ms, struct wheel_list *o) { (void)m; (void)ms; o->n = 0; return false; }
bool wheel_mix_rule(const struct wheel_rule *r, struct wheel_list *o) { (void)r; o->n = 0; return false; }
bool wheel_mix_daily(struct wheel_list *o) { o->n = 0; return false; }
int  wheel_library_count(void) { return 0; }
void wheel_library_invalidate(void) { }
#endif

/* fallback for files the database doesn't know (yet) */
void wheel_track_fill_meta(struct wheel_track *t)
{
    static struct mp3entry id3;
    if (t->meta)
        return;
    t->meta = true; /* even on failure: don't retry every frame */
    int fd = open(t->path, O_RDONLY);
    if (fd < 0)
        return;
    if (get_metadata(&id3, fd, t->path))
    {
        strlcpy(t->title, id3.title ? id3.title : "", sizeof(t->title));
        strlcpy(t->artist, id3.artist ? id3.artist : (id3.albumartist ? id3.albumartist : ""),
                sizeof(t->artist));
        t->len_ms = id3.length;
    }
    close(fd);
}
