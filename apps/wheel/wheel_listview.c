/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - the playlist view.
 *
 * One screen for mixes, playlist files and the play queue: a header with
 * the cover (or a 2x2 mosaic of the first albums), the name, how many
 * songs and how long, Play / Shuffle / Save buttons, then every track
 * with its artist and length. The header folds away while you scroll.
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
#include "action.h"
#include "list.h"
#include "file.h"
#include "dir.h"
#include "font.h"
#include "misc.h"
#include "splash.h"
#include "settings.h"
#include "playlist.h"
#include "audio.h"
#include "metadata.h"
#include "viewport.h"
#include "skin_engine/skin_engine.h"
#include "wps.h"
#include "screens.h"
#include "keyboard.h"
#include "root_menu.h"
#include "rbpaths.h"
#include "string-extra.h"
#include "pathfuncs.h"
#include "yesno.h"
#include "filetypes.h"
#include "wheel_gel.h"
#include "wheel_connect.h"
#include "wheel_art.h"
#include "wheel_library.h"
#include "wheel_listview.h"
#include "wheel_typo.h"
#include "wheel_screens.h"
#include "wheel_flow.h"

#define W LCD_WIDTH
#define H LCD_HEIGHT

#define HDR_FULL   150
#define HDR_SMALL  56
/* two-line rows: 48 px at 100 %, the typography line spacing scales them */
static int ROW_H = 48;
static int row_gap = 21;   /* title -> artist */
static void update_row_metrics(void)
{
    int pct = typo_active()->list_pct;
    if (pct < 80 || pct > 150) pct = 100;
    ROW_H = (48 * pct + 50) / 100;
    row_gap = MAX(20, (21 * pct + 50) / 100);
}
#define COVER      112
#define DURATION_CACHE ROCKBOX_DIR "/wheel-playlists.tsv"

WHEEL_LIST_DEFINE(lv_main, 1000);
WHEEL_LIST_DEFINE(lv_nested, 120);

WHEEL_ART_INIT(art_big, COVER);
WHEEL_ART_INIT(art_small, 40);
WHEEL_ART_INIT(art_m0, COVER / 2);
WHEEL_ART_INIT(art_m1, COVER / 2);
WHEEL_ART_INIT(art_m2, COVER / 2);
WHEEL_ART_INIT(art_m3, COVER / 2);
static struct wheel_art *mosaic[4] = { &art_m0, &art_m1, &art_m2, &art_m3 };

static int f_title = -1, f_med = -1, f_reg = -1, f_semi = -1, f_icon_big = -1, f_icon = -1;

struct wheel_list *wheel_lv_list(void) { lv_main.n = lv_main.total = 0; return &lv_main; }

/* ============================== helpers ============================== */

static int mixc(unsigned a, unsigned b, int pct)
{
    return LCD_RGBPACK((RGB_UNPACK_RED(a) * pct + RGB_UNPACK_RED(b) * (100 - pct)) / 100,
                       (RGB_UNPACK_GREEN(a) * pct + RGB_UNPACK_GREEN(b) * (100 - pct)) / 100,
                       (RGB_UNPACK_BLUE(a) * pct + RGB_UNPACK_BLUE(b) * (100 - pct)) / 100);
}

static void load_fonts(void)
{
    update_row_metrics();
    /* typography profile: titles, artist/album, details */
    f_title = typo_font(TU_DISPLAY_NP);
    f_semi  = typo_font(TU_SECONDARY);
    f_med   = typo_font(TU_META);
    f_reg   = typo_font(TU_META);
    /* "O" is a music note in the Wheel icon fonts */
    if (f_icon_big < 0) f_icon_big = font_load(FONT_DIR "/110-Wheel-Icons.fnt");
    if (f_icon < 0)     f_icon     = font_load(FONT_DIR "/34-Wheel-Icons.fnt");
}

static int fnt(int f) { return f >= 0 ? f : FONT_UI; }

/* draw s at x,y in font f, cut with an ellipsis to fit max_w */
static int text_fit(struct screen *d, struct viewport *vp, int f, unsigned col,
                    int x, int y, int max_w, const char *s)
{
    char buf[200];
    int w, h;
    vp->font = fnt(f);
    strlcpy(buf, s, sizeof(buf));
    d->getstringsize(buf, &w, &h);
    if (w > max_w)
    {
        size_t len = strlen(buf);
        while (len > 0)
        {
            do { len--; } while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
            strcpy(&buf[len], "…");
            d->getstringsize(buf, &w, &h);
            if (w <= max_w) break;
        }
    }
    d->set_foreground(col);
    d->putsxy(x, y, buf);
    return w;
}

static int text_w(struct screen *d, struct viewport *vp, int f, const char *s)
{
    int w, h;
    vp->font = fnt(f);
    d->getstringsize(s, &w, &h);
    return w;
}

/* the page follows the theme: light or dark from its background colour */
static bool page_light(void)
{
    unsigned bg = global_settings.bg_color;
    return RGB_UNPACK_RED(bg) * 3 + RGB_UNPACK_GREEN(bg) * 6 + RGB_UNPACK_BLUE(bg) > 128 * 10;
}

/* colour of row y of the header band: the cover's tint fading into the
 * page - deep with white text when dark, a pastel with dark text when light */
static unsigned band_colour(unsigned tint, int y, int hh, bool full)
{
    unsigned bg = global_settings.bg_color;
    int pct;
    if (page_light())
        pct = full ? 42 - y * 34 / hh : 34;
    else
        pct = full ? 100 - y * 70 / hh : 100;
    return mixc(tint, bg, pct);
}

/* a raised glossy button: rounded corners, a thin rim, a bright top half
 * fading down to a waterline, the colour deepest just under it and lit
 * again towards the bottom */
static unsigned button_row(unsigned base, int k, int h)
{
    unsigned white = LCD_RGBPACK(255, 255, 255), black = LCD_RGBPACK(0, 0, 0);
    int mid = h / 2;
    if (k <= 0)
        return mixc(white, base, 72);              /* the lit upper edge */
    if (k < mid)
        return mixc(white, base, 50 - 26 * (k - 1) / MAX(1, mid - 1));
    int t = (k - mid) * 100 / MAX(1, h - 1 - mid);
    return mixc(mixc(black, base, 10), mixc(white, base, 16), 100 - t);
}

static void gel_button(struct screen *d, int x, int y, int w, int h, unsigned base,
                       unsigned rim, unsigned tint, int hh, bool full)
{
    const int r = 8;
    for (int i = 0; i < h; i++)
    {
        int dy = i < r ? r - i : (i >= h - r ? i - (h - r) + 1 : 0);
        int inset = 0;
        if (dy)
        {   /* circle: inset = r - sqrt(r^2 - dy^2) */
            int k = r * r - dy * dy, s = 0;
            while ((s + 1) * (s + 1) <= k) s++;
            inset = r - s;
        }
        unsigned under = band_colour(tint, y + i, hh, full);
        unsigned body = (i == 0 || i == h - 1) ? rim : button_row(base, i - 1, h - 2);
        d->set_foreground(body);
        d->hline(x + inset + 1, x + w - 2 - inset, y + i);
        /* the rim at the sides, softened into the band at the corners */
        d->set_foreground(dy ? (unsigned)mixc(rim, under, 70) : rim);
        d->drawpixel(x + inset, y + i);
        d->drawpixel(x + w - 1 - inset, y + i);
        if (inset > 0)
        {
            d->set_foreground(mixc(rim, under, 30));
            d->drawpixel(x + inset - 1, y + i);
            d->drawpixel(x + w - inset, y + i);
        }
    }
}

void wheel_format_duration(unsigned long secs, char *buf, size_t len)
{
    unsigned long mins = (secs + 30) / 60;
    if (mins >= 60)
        snprintf(buf, len, "%lu h %lu min", mins / 60, mins % 60);
    else
        snprintf(buf, len, "%lu min", mins);
}

static void song_count(int n, char *buf, size_t len)
{
    snprintf(buf, len, "%d %s", n, str(n == 1 ? LANG_WHEEL_SONG : LANG_WHEEL_SONGS));
}

/* a short pop-up list; returns the picked index or -1 */
static const char * const *pick_items;
static const char *pick_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data; (void)buf; (void)len;
    return pick_items[item];
}
int wheel_pick(const char *title, const char * const *items, int n, int sel)
{
    struct simplelist_info info;
    pick_items = items;
    simplelist_info_init(&info, (char *)title, n, NULL);
    info.get_name = pick_get_name;
    info.selection = sel;
    simplelist_show_list(&info);
    return info.selection;
}

/* =========================== playing a list =========================== */

bool wheel_play_list(struct wheel_list *l, int start, bool shuffle)
{
    if (l->n <= 0)
        return false;
    long t0 = current_tick;
    if (playlist_create(NULL, NULL) < 0)
        return false;
    struct playlist_insert_context ctx;
    if (playlist_insert_context_create(NULL, &ctx, PLAYLIST_INSERT_LAST, false, false) < 0)
        return false;
    for (int i = 0; i < l->n; i++)
        playlist_insert_context_add(&ctx, l->t[i].path);
    playlist_insert_context_release(&ctx);
    if (playlist_amount() <= 0)
        return false;
    if (shuffle)
    {
        playlist_shuffle(current_tick, -1);
        start = 0;
    }
    playlist_start(MAX(0, MIN(start, playlist_amount() - 1)), 0, 0);
    wheel_flow_play_started();
    wheel_perf("play: make the playlist", current_tick - t0);
    return true;
}

static bool play_file(const char *file, int start, bool shuffle)
{
    char dir[MAX_PATH];
    strlcpy(dir, file, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (!slash)
        return false;
    *slash = '\0';
    if (playlist_create(dir[0] ? dir : "/", slash + 1) < 0 || playlist_amount() <= 0)
        return false;
    if (shuffle)
    {
        playlist_shuffle(current_tick, -1);
        start = 0;
    }
    playlist_start(MAX(0, MIN(start, playlist_amount() - 1)), 0, 0);
    wheel_flow_play_started();
    return true;
}

static void queue_tracks(struct wheel_list *l, int from, int count, bool next)
{
    if (!(audio_status() & AUDIO_STATUS_PLAY))
    {
        struct wheel_list one = { .n = count, .max = count, .total = count, .t = &l->t[from] };
        wheel_play_list(&one, 0, false);
        return;
    }
    int pos = next ? PLAYLIST_INSERT_FIRST : PLAYLIST_INSERT_LAST;
    if (next)
    {   /* insert in reverse so they keep their order */
        for (int i = from + count - 1; i >= from; i--)
            playlist_insert_track(NULL, l->t[i].path, pos, false, false);
    }
    else
        for (int i = from; i < from + count; i++)
            playlist_insert_track(NULL, l->t[i].path, pos, false, false);
    playlist_sync(NULL);
    splash(HZ, next ? ID2P(LANG_WHEEL_ADDED_NEXT) : ID2P(LANG_WHEEL_ADDED_QUEUE));
}

/* ============================ playlist files ============================ */

static bool is_playlist_name(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".m3u") || !strcasecmp(ext, ".m3u8"));
}

/* read a .m3u/.m3u8 into the list; returns the number of entries */
int wheel_load_playlist_file(const char *file, struct wheel_list *l)
{
    char line[MAX_PATH + 16], dir[MAX_PATH];
    l->n = l->total = 0;
    int fd = open_utf8(file, O_RDONLY);
    if (fd < 0)
        return 0;
    strlcpy(dir, file, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = '\0';
    while (read_line(fd, line, sizeof(line)) > 0)
    {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        size_t len = strlen(p);
        while (len && (p[len - 1] == '\r' || p[len - 1] == ' ')) p[--len] = '\0';
        for (char *q = p; *q; q++) if (*q == '\\') *q = '/';
        l->total++;
        if (l->n >= l->max) continue;
        struct wheel_track *t = &l->t[l->n++];
        if (p[0] == '/')
            strlcpy(t->path, p, sizeof(t->path));
        else
            snprintf(t->path, sizeof(t->path), "%s%s", dir, p);
        t->title[0] = t->artist[0] = '\0';
        t->len_ms = 0;
        t->meta = false;
    }
    close(fd);
    return l->total;
}

static bool write_playlist(const char *file, struct wheel_list *l)
{
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    fdprintf(fd, "#EXTM3U\n");
    for (int i = 0; i < l->n; i++)
    {
        if (l->t[i].meta && l->t[i].title[0])
            fdprintf(fd, "#EXTINF:%lu,%s - %s\n", l->t[i].len_ms / 1000,
                     l->t[i].artist, l->t[i].title);
        fdprintf(fd, "%s\n", l->t[i].path);
    }
    close(fd);
    return true;
}

static const char *catalog_dir(void)
{
    const char *dir = global_settings.playlist_catalog_dir;
    if (!dir[0])
        dir = PLAYLIST_CATALOG_DEFAULT_DIR;
    if (!dir_exists(dir))
        mkdir(dir);
    return dir;
}

/* ask for a name and save; returns true when saved */
static bool save_as(const char *suggest, struct wheel_list *l, bool from_queue)
{
    char name[64], path[MAX_PATH];
    strlcpy(name, suggest, sizeof(name));
    for (char *p = name; *p; p++)
        if (strchr("/\\:*?\"<>|", *p)) *p = '-';
    if (kbd_input(name, sizeof(name), NULL) < 0 || !name[0])
        return false;
    snprintf(path, sizeof(path), "%s/%s.m3u8", catalog_dir(), name);
    if (file_exists(path) && !yesno_pop(ID2P(LANG_WHEEL_REPLACE_PLAYLIST)))
        return false;
    bool ok = from_queue ? playlist_save(NULL, path) >= 0 : write_playlist(path, l);
    splash(HZ * 3 / 2, ok ? ID2P(LANG_WHEEL_PLAYLIST_SAVED) : ID2P(LANG_FAILED));
    return ok;
}

static long file_size(const char *file)
{
    int fd = open(file, O_RDONLY);
    if (fd < 0)
        return -1;
    long size = ffilesize(fd);
    close(fd);
    return size;
}

/* duration cache: path \t size \t count \t seconds */
bool wheel_playlist_cached(const char *file, int *count, unsigned long *secs)
{
    char line[MAX_PATH + 64];
    long fsize = file_size(file);
    if (fsize < 0)
        return false;
    int fd = open(DURATION_CACHE, O_RDONLY);
    if (fd < 0)
        return false;
    bool found = false;
    while (!found && read_line(fd, line, sizeof(line)) > 0)
    {
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        *t1 = '\0';
        if (strcmp(line, file)) continue;
        long size = strtol(t1 + 1, &t1, 10);
        if (size != fsize) break;
        *count = strtol(t1 + 1, &t1, 10);
        *secs = strtoul(t1 + 1, NULL, 10);
        found = true;
    }
    close(fd);
    return found;
}

static void cache_store(const char *file, int count, unsigned long secs)
{
    char line[MAX_PATH + 64];
    long fsize = file_size(file);
    if (fsize < 0)
        return;
    /* rewrite without the old entry (the file stays small) */
    int in = open(DURATION_CACHE, O_RDONLY);
    int out = open(DURATION_CACHE ".tmp", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0)
    {
        if (in >= 0) close(in);
        return;
    }
    if (in >= 0)
    {
        while (read_line(in, line, sizeof(line)) > 0)
        {
            size_t fl = strlen(file);
            if (!strncmp(line, file, fl) && line[fl] == '\t')
                continue;
            fdprintf(out, "%s\n", line);
        }
        close(in);
    }
    fdprintf(out, "%s\t%ld\t%d\t%lu\n", file, fsize, count, secs);
    close(out);
    rename(DURATION_CACHE ".tmp", DURATION_CACHE);
}

/* ============================= the screen ============================= */

enum btn { BTN_PLAY, BTN_SHUFFLE, BTN_SAVE, BTN_NOWPLAYING, BTN_COUNT };

static struct {
    struct wheel_lv *lv;
    struct wheel_list *l;
    int btns[BTN_COUNT];
    int n_btn;
    int sel;          /* 0..n_btn-1 buttons, then tracks */
    int top;          /* first visible track in the folded layout */
    int playing;      /* index of the current track, -1 if none */
    unsigned tint;
    bool have_art, have_mosaic;
    unsigned long secs;
    int known;        /* tracks with a known length */
    int fill_next;    /* background metadata cursor */
} v;

static void pick_covers(void)
{
    v.have_art = v.have_mosaic = false;
    if (v.l->n <= 0)
        return;
    if (v.lv->kind == LV_QUEUE)
    {
        struct mp3entry *id3 = audio_current_track();
        v.have_art = id3 && wheel_art_load(&art_big, id3->path);
        if (v.have_art)
            wheel_art_load(&art_small, id3->path);
        return;
    }
    /* four different albums make a mosaic; otherwise the first cover */
    char dirs[4][MAX_PATH];
    int found = 0;
    for (int i = 0; i < v.l->n && i < 60 && found < 4; i++)
    {
        char d[MAX_PATH];
        strlcpy(d, v.l->t[i].path, sizeof(d));
        char *s = strrchr(d, '/');
        if (s) *s = '\0';
        bool dup = false;
        for (int k = 0; k < found; k++)
            if (!strcmp(dirs[k], d)) dup = true;
        if (dup) continue;
        if (!wheel_art_load(mosaic[found], v.l->t[i].path)) continue;
        strcpy(dirs[found], d);
        found++;
    }
    if (found == 4)
        v.have_mosaic = true;
    else if (found >= 1)
        v.have_art = wheel_art_load(&art_big, mosaic[0]->path);
    if (found >= 1)
        wheel_art_load(&art_small, mosaic[0]->path);
}

static void recount(void)
{
    v.secs = 0;
    v.known = 0;
    for (int i = 0; i < v.l->n; i++)
        if (v.l->t[i].meta)
        {
            v.secs += v.l->t[i].len_ms / 1000;
            v.known++;
        }
}

static unsigned header_tint(void)
{
    if (v.have_mosaic) return mosaic[0]->tint;
    if (v.have_art) return art_big.tint;
    return mixc(global_settings.lse_color, LCD_RGBPACK(0, 0, 0), 70);
}

static void draw_header(struct screen *d, struct viewport *vp, bool full)
{
    unsigned white = LCD_RGBPACK(255, 255, 255);
    bool light = page_light();
    /* text on the band: white on the deep tint, the theme's on the pastel */
    unsigned ink = light ? (unsigned)global_settings.fg_color : white;
    unsigned ink2 = light ? mixc(global_settings.fg_color, global_settings.bg_color, 62)
                          : mixc(white, v.tint, 78);
    int hh = full ? HDR_FULL : HDR_SMALL;
    /* tinted band fading into the page colour */
    d->set_drawmode(DRMODE_SOLID);
    for (int y = 0; y < hh; y++)
    {
        d->set_foreground(band_colour(v.tint, y, hh, full));
        d->hline(0, W - 1, y);
    }
    int cs = full ? COVER : 40;
    int cx = full ? 20 : 12, cy = full ? 20 : 8;
    if (v.have_mosaic && full)
    {
        for (int i = 0; i < 4; i++)
            wheel_art_draw(mosaic[i], d, cx + (i % 2) * (cs / 2), cy + (i / 2) * (cs / 2), cs / 2);
    }
    else if ((v.have_art || v.have_mosaic) && full)
        wheel_art_draw(&art_big, d, cx, cy, cs);
    else if (v.have_art || v.have_mosaic)
    {   /* the folded header: a small copy of the cover */
        d->set_foreground(mixc(v.tint, white, 70));
        d->fillrect(cx, cy, cs, cs);
        wheel_art_draw(&art_small, d, cx, cy, cs);
    }
    else
    {
        d->set_foreground(mixc(global_settings.lss_color, v.tint, 80));
        d->fillrect(cx, cy, cs, cs);
        d->set_drawmode(DRMODE_FG);
        int f = full ? f_icon_big : f_icon;
        if (f >= 0)
        {
            int w, h;
            vp->font = f;
            d->getstringsize("O", &w, &h);
            d->set_foreground(white);
            d->putsxy(cx + (cs - w) / 2, cy + (cs - h) / 2, "O");
        }
    }
    d->set_drawmode(DRMODE_FG);

    char stats[64], dur[32];
    song_count(v.l->total, stats, sizeof(stats));
    if (v.known > 0)
    {
        wheel_format_duration(v.secs, dur, sizeof(dur));
        size_t sl = strlen(stats);
        snprintf(stats + sl, sizeof(stats) - sl, " · %s%s",
                 v.known < v.l->total ? "+" : "", dur);
    }
    if (full)
    {
        int tx = cx + cs + 16, tw = W - tx - 16;
        if (v.lv->subtitle)
            text_fit(d, vp, f_med, ink2, tx, 24, tw, v.lv->subtitle);
        text_fit(d, vp, f_title, ink, tx, 46, tw, v.lv->title);
        text_fit(d, vp, f_reg, ink2, tx, 82, tw, stats);
        /* buttons: the one the wheel is on in the accent colour, the others
         * pearl grey (or smoke when dark) */
        unsigned black = LCD_RGBPACK(0, 0, 0);
        int bx = tx;
        for (int i = 0; i < v.n_btn; i++)
        {
            static const int lbl[BTN_COUNT] = { LANG_WHEEL_PLAY, LANG_WHEEL_SHUFFLE,
                                                LANG_WHEEL_SAVE, LANG_WHEEL_NOW_PLAYING };
            const char *s = str(lbl[v.btns[i]]);
            int w = text_w(d, vp, f_med, s) + 28;
            if (bx + w > W - 8) break;
            bool on = v.sel == i;
            unsigned base, rim, label;
            if (on)
            {
                base = global_settings.lse_color;
                rim = mixc(black, base, 45);
                label = global_settings.lst_color;
            }
            else if (light)
            {
                base = mixc(LCD_RGBPACK(0xe4, 0xe4, 0xea), v.tint, 93);
                rim = LCD_RGBPACK(0x98, 0x98, 0xa2);
                label = LCD_RGBPACK(0x1c, 0x1c, 0x1f);
            }
            else
            {
                base = mixc(LCD_RGBPACK(0x3c, 0x3c, 0x43), v.tint, 85);
                rim = LCD_RGBPACK(0x0e, 0x0e, 0x11);
                label = LCD_RGBPACK(0xf2, 0xf2, 0xf5);
            }
            d->set_drawmode(DRMODE_SOLID);
            gel_button(d, bx, 112, w, 30, base, rim, v.tint, hh, full);
            d->set_drawmode(DRMODE_FG);
            text_fit(d, vp, f_med, label, bx + 14, 117, w, s);
            bx += w + 8;
        }
    }
    else
    {
        int tx = cx + cs + 12, tw = W - tx - 16;
        text_fit(d, vp, f_semi, ink, tx, 6, tw, v.lv->title);
        text_fit(d, vp, f_reg, ink2, tx, 31, tw, stats);
    }
}

static void draw_eq_bars(struct screen *d, int x, int y, unsigned col)
{
    static const int hgt[3][3] = { { 6, 12, 9 }, { 11, 5, 13 }, { 8, 13, 5 } };
    int phase = (current_tick / (HZ / 4)) % 3;
    bool playing = (audio_status() & (AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE)) == AUDIO_STATUS_PLAY;
    d->set_drawmode(DRMODE_SOLID);
    d->set_foreground(col);
    for (int b = 0; b < 3; b++)
    {
        int h = playing ? hgt[phase][b] : 4;
        d->fillrect(x + b * 5, y + 14 - h, 3, h);
    }
}

static void draw_row(struct screen *d, struct viewport *vp, int i, int y, bool sel)
{
    struct wheel_track *t = &v.l->t[i];
    unsigned bg = global_settings.bg_color, fg = global_settings.fg_color;
    unsigned dim = mixc(fg, bg, 55);
    if (sel && wheel_aero_on())
    {
        d->set_drawmode(DRMODE_SOLID);
        wheel_selection(d, 0, y, W, ROW_H);
        fg = dim = global_settings.lst_color;
    }
    else if (sel)
    {
        d->set_drawmode(DRMODE_SOLID);
        for (int k = 0; k < ROW_H; k++)
        {
            d->set_foreground(mixc(global_settings.lse_color, global_settings.lss_color,
                                   k * 100 / ROW_H));
            d->hline(0, W - 1, y + k);
        }
        fg = dim = global_settings.lst_color;
    }
    if (!t->meta)
        wheel_track_fill_meta(t); /* only rows on screen; cheap enough */
    d->set_drawmode(DRMODE_FG);
    char num[12], dur[12];
    if (i == v.playing)
        draw_eq_bars(d, 18, y + ROW_H / 2 - 9, sel ? fg : (unsigned)global_settings.lse_color);
    else
    {
        snprintf(num, sizeof(num), "%d", i + 1);
        int w = text_w(d, vp, f_reg, num);
        vp->font = fnt(f_reg);
        d->set_foreground(dim);
        d->putsxy(40 - w, y + (ROW_H - 22) / 2 + 1, num);
    }
    int right = W - 16;
    if (t->len_ms)
    {
        unsigned s = t->len_ms / 1000;
        snprintf(dur, sizeof(dur), "%u:%02u", s / 60, s % 60);
        int w = text_w(d, vp, f_reg, dur);
        d->set_drawmode(DRMODE_FG);
        vp->font = fnt(f_reg);
        d->set_foreground(dim);
        d->putsxy(right - w, y + (ROW_H - 22) / 2 + 1, dur);
        right -= w + 14;
    }
    const char *title = t->title[0] ? t->title : strrchr(t->path, '/') ? strrchr(t->path, '/') + 1 : t->path;
    unsigned tcol = (i == v.playing && !sel) ? (unsigned)global_settings.lse_color : fg;
    int ty = (ROW_H - row_gap - 22) / 2 + 1;
    text_fit(d, vp, f_med, tcol, 56, y + ty, right - 56, title);
    if (t->artist[0])
        text_fit(d, vp, f_reg, dim, 56, y + ty + row_gap, right - 56, t->artist);
}

static void draw(void)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = W; vp.height = H;
    struct viewport *last = d->set_viewport(&vp);
    d->set_background(global_settings.bg_color);
    d->set_drawmode(DRMODE_SOLID);
    d->clear_viewport();

    int t = v.sel - v.n_btn; /* selected track, <0 on buttons */
    int rows_full = (H - HDR_FULL) / ROW_H, rows_small = (H - HDR_SMALL) / ROW_H;
    bool full = t < rows_full;
    int y0, first, rows;
    if (full)
    {
        y0 = HDR_FULL + 6; first = 0; rows = rows_full;
        v.top = 0;
    }
    else
    {
        if (t < v.top) v.top = t;
        if (t >= v.top + rows_small) v.top = t - rows_small + 1;
        y0 = HDR_SMALL + 8; first = v.top; rows = rows_small;
    }
    if (v.l->n == 0)
    {
        d->set_drawmode(DRMODE_FG);
        text_fit(d, &vp, f_med, mixc(global_settings.fg_color, global_settings.bg_color, 60),
                 24, y0 + 20, W - 48, str(LANG_WHEEL_LIST_EMPTY));
    }
    for (int r = 0; r < rows && first + r < v.l->n; r++)
        draw_row(d, &vp, first + r, y0 + r * ROW_H, first + r == t);
    draw_header(d, &vp, full);

    /* scroll position */
    if (v.l->n > rows)
    {
        int area = H - y0 - 4;
        int bh = MAX(16, area * rows / v.l->n);
        int by = y0 + (area - bh) * first / MAX(1, v.l->n - rows);
        d->set_drawmode(DRMODE_SOLID);
        d->set_foreground(mixc(global_settings.fg_color, global_settings.bg_color, 30));
        d->fillrect(W - 4, by, 3, bh);
    }
    d->update();
    d->set_viewport(last);
}

/* the queue changes under us: reload it */
static void load_queue(struct wheel_list *l)
{
    int amount = playlist_amount();
    int first = playlist_get_first_index(NULL);
    struct playlist_track_info info;
    l->n = 0;
    l->total = amount;
    for (int i = 0; i < amount && l->n < l->max; i++)
    {
        int idx = (first + i) % MAX(1, amount);
        if (playlist_get_track_info(NULL, idx, &info) < 0)
            continue;
        struct wheel_track *t = &l->t[l->n++];
        /* the same song in the same place keeps its details: switching
         * views or a growing queue doesn't read every song's tags again */
        if (t->meta && !strcmp(t->path, info.filename))
            continue;
        strlcpy(t->path, info.filename, sizeof(t->path));
        t->title[0] = t->artist[0] = '\0';
        t->len_ms = 0;
        t->meta = false;
    }
}

static int queue_abs_index(int display)
{
    int amount = playlist_amount();
    return amount ? (playlist_get_first_index(NULL) + display) % amount : 0;
}

static void find_playing(void)
{
    v.playing = -1;
    struct mp3entry *id3 = audio_current_track();
    if (!(audio_status() & AUDIO_STATUS_PLAY) || !id3)
        return;
    if (v.lv->kind == LV_QUEUE)
    {
        v.playing = playlist_get_display_index() - 1;
        return;
    }
    for (int i = 0; i < v.l->n; i++)
        if (!strcmp(v.l->t[i].path, id3->path)) { v.playing = i; return; }
}

static void theme_off(void)
{
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
}
static void theme_on(void)
{
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, false);
    /* the status bar skin misses track changes while it is off (playback
     * may have started here): redraw all of it, not just what changed */
    wps_state_attach();
    skin_request_full_update(CUSTOM_STATUSBAR);
}

enum { OPT_NEXT, OPT_LATER, OPT_SIMILAR, OPT_ALBUM, OPT_REMOVE, OPT_UP, OPT_DOWN, OPT_SEND,
       OPT_SEND_ALL, OPT_VIEW, OPT_COUNT };

static void note_origin(struct wheel_lv *lv);
static int nested_depth;
/* the song the user was on when they switched views, -1 for none */
static int switch_sel = -1;
static int take_switch_sel(void)
{
    int i = switch_sel;
    switch_sel = -1;
    return i;
}
/* the classic list keeps the theme on; the other views turn it off */
static bool themed_view;
static void opts_theme_on(void)  { if (!themed_view) theme_on(); }
static void opts_theme_off(void) { if (!themed_view) theme_off(); }

static const int plv_lang[PLV_COUNT] = {
    LANG_WHEEL_VIEW_WHEEL, LANG_WHEEL_VIEW_CLASSIC, LANG_WHEEL_COVERFLOW, LANG_WHEEL_VIEW_DISC,
};

const char *wheel_playlist_view_name(int view)
{
    return str(plv_lang[MAX(0, MIN(PLV_COUNT - 1, view))]);
}

bool wheel_pick_playlist_view(void)
{
    const char *items[PLV_COUNT];
    int n = wheel_flow_supported() ? PLV_COUNT : PLV_FLOW;
    for (int k = 0; k < n; k++)
        items[k] = wheel_playlist_view_name(k);
    int cur = MAX(0, MIN(n - 1, global_settings.wheel_pl_view));
    int p = wheel_pick(str(LANG_WHEEL_PL_VIEW), items, n, cur);
    if (p < 0 || p == cur)
        return false;
    global_settings.wheel_pl_view = p;
    settings_save();
    return true;
}

/* long press on a track */
static int track_options(int i, bool *changed)
{
    static const int lang[OPT_COUNT] = {
        LANG_WHEEL_PLAY_NEXT, LANG_WHEEL_PLAY_LATER, LANG_WHEEL_SIMILAR,
        LANG_WHEEL_GOTO_ALBUM, LANG_WHEEL_REMOVE, LANG_WHEEL_MOVE_UP, LANG_WHEEL_MOVE_DOWN,
        LANG_WHEEL_BT_SEND, LANG_WHEEL_BT_SEND_ALL, LANG_WHEEL_CHANGE_VIEW,
    };
    int ids[OPT_COUNT], n = 0;
    const char *items[OPT_COUNT];
    bool queue = v.lv->kind == LV_QUEUE, file = v.lv->kind == LV_PLAYLIST_FILE;
    if (!queue) { ids[n++] = OPT_NEXT; ids[n++] = OPT_LATER; }
    if (nested_depth == 0) ids[n++] = OPT_SIMILAR;
    ids[n++] = OPT_ALBUM;
    if (queue || file)
    {
        ids[n++] = OPT_REMOVE;
        if (i > 0) ids[n++] = OPT_UP;
        if (i + 1 < v.l->n) ids[n++] = OPT_DOWN;
    }
    if (wheel_bt_send_supported())
    {
        ids[n++] = OPT_SEND;
        if (v.l->n > 1) ids[n++] = OPT_SEND_ALL;
    }
    if (!v.lv->fixed)
        ids[n++] = OPT_VIEW;
    for (int k = 0; k < n; k++)
        items[k] = str(lang[ids[k]]);
    const char *title = v.l->t[i].title[0] ? v.l->t[i].title : (const char *)str(LANG_WHEEL_SONG);
    opts_theme_on();
    int pick = wheel_pick(title, items, n, 0);
    int ret = 0;
    if (pick >= 0)
    {
        switch (ids[pick])
        {
            case OPT_NEXT:  queue_tracks(v.l, i, 1, true); break;
            case OPT_LATER: queue_tracks(v.l, i, 1, false); break;
            case OPT_SIMILAR:
            {
                splash(0, ID2P(LANG_WHEEL_MAKING_MIX));
                lv_nested.n = 0;
                if (wheel_mix_similar(v.l->t[i].path, &lv_nested))
                {
                    char title2[128];
                    snprintf(title2, sizeof(title2), "%s", v.l->t[i].title[0] ? v.l->t[i].title : "");
                    struct wheel_lv lv2 = { .title = title2, .subtitle = str(LANG_WHEEL_SIMILAR),
                                            .kind = LV_MIX, .list = &lv_nested };
                    nested_depth++;
                    ret = wheel_listview_show(&lv2);
                    nested_depth--;
                }
                else
                    splash(HZ * 2, ID2P(LANG_WHEEL_NEED_LIBRARY));
                break;
            }
            case OPT_ALBUM:
                /* GO_TO_FILEBROWSER is 0, which means "stay" here: the root
                 * menu sends GO_TO_PREVIOUS_BROWSER with a target to the files */
                strlcpy(wheel_browse_target, v.l->t[i].path, MAX_PATH);
                ret = GO_TO_PREVIOUS_BROWSER;
                break;
            case OPT_REMOVE:
                if (queue)
                {
                    playlist_delete(NULL, queue_abs_index(i));
                    *changed = true;
                }
                else
                {
                    memmove(&v.l->t[i], &v.l->t[i + 1], (v.l->n - i - 1) * sizeof(v.l->t[0]));
                    v.l->n--; v.l->total--;
                    write_playlist(v.lv->file, v.l);
                }
                break;
            case OPT_UP:
            case OPT_DOWN:
            {
                int to = ids[pick] == OPT_UP ? i - 1 : i + 1;
                if (queue)
                {
                    playlist_move(NULL, queue_abs_index(i), queue_abs_index(to));
                    *changed = true;
                }
                else
                {
                    struct wheel_track tmp = v.l->t[i];
                    v.l->t[i] = v.l->t[to];
                    v.l->t[to] = tmp;
                    write_playlist(v.lv->file, v.l);
                }
                v.sel += to - i;
                break;
            }
            case OPT_SEND:
            {
                const char *one[1] = { v.l->t[i].path };
                wheel_bt_send_screen(one, 1);
                break;
            }
            case OPT_SEND_ALL:
            {
                static const char *all[1000];
                for (int k = 0; k < v.l->n; k++)
                    all[k] = v.l->t[k].path;
                wheel_bt_send_screen(all, v.l->n);
                break;
            }
            case OPT_VIEW:
                if (wheel_pick_playlist_view())
                {
                    switch_sel = i;
                    ret = WHEEL_VIEW_SWITCH;
                }
                break;
        }
    }
    opts_theme_off();
    return ret;
}

static int show_wheel(struct wheel_lv *lv)
{
    /* the nested "similar" view keeps the outer one's state */
    typeof(v) saved = v;
    bool saved_themed = themed_view;
    themed_view = false;
    memset(&v, 0, sizeof(v));
    v.lv = lv;
    v.l = lv->list;
    load_fonts();
    if (lv->kind == LV_QUEUE)
        load_queue(v.l);
    splash(0, ID2P(LANG_WAIT));
    wheel_list_fill_meta(v.l);
    recount();
    pick_covers();
    v.tint = header_tint();
    v.n_btn = 0;
    if (lv->kind == LV_QUEUE)
    {
        v.btns[v.n_btn++] = BTN_NOWPLAYING;
        v.btns[v.n_btn++] = BTN_SAVE;
    }
    else
    {
        v.btns[v.n_btn++] = BTN_PLAY;
        v.btns[v.n_btn++] = BTN_SHUFFLE;
        if (lv->kind == LV_MIX)
            v.btns[v.n_btn++] = BTN_SAVE;
    }
    find_playing();
    int carried = take_switch_sel();
    if (carried >= 0 && carried < v.l->n)
    {   /* coming from another view: the same song */
        v.sel = v.n_btn + carried;
        v.top = MAX(0, carried - 2);
    }
    else if (v.playing >= 0)
    {   /* start on the current song, with a little of what came before */
        v.sel = v.n_btn + v.playing;
        v.top = MAX(0, v.playing - 2);
    }
    v.fill_next = 0;
    bool cached = lv->kind == LV_PLAYLIST_FILE && v.known == v.l->total;
    if (cached)
        cache_store(lv->file, v.l->total, v.secs);

    theme_off();
    int ret = 0;
    long last_queue_check = current_tick;
    bool done = false, moved = false;
    while (!done)
    {
        bool screen = wheel_screen_on();
        if (screen)
            draw();
        int count = v.n_btn + v.l->n;
        bool busy = v.fill_next < v.l->n || v.playing >= 0;
        int a = get_action(CONTEXT_STD, !screen ? HZ : busy ? HZ / 8 : HZ / 2);
        switch (a)
        {
            case ACTION_STD_NEXT:
            case ACTION_STD_NEXTREPEAT:
                if (v.sel < count - 1) v.sel++;
                moved = true;
                break;
            case ACTION_STD_PREV:
            case ACTION_STD_PREVREPEAT:
                if (v.sel > 0) v.sel--;
                moved = true;
                break;
            case ACTION_STD_OK:
                if (v.sel < v.n_btn)
                {
                    switch (v.btns[v.sel])
                    {
                        case BTN_PLAY:
                        case BTN_SHUFFLE:
                        {
                            bool sh = v.btns[v.sel] == BTN_SHUFFLE;
                            bool ok = lv->kind == LV_PLAYLIST_FILE && v.l->total > v.l->n
                                    ? play_file(lv->file, 0, sh)
                                    : wheel_play_list(v.l, 0, sh);
                            if (ok) { note_origin(lv); ret = GO_TO_WPS; done = true; }
                            break;
                        }
                        case BTN_SAVE:
                            theme_on();
                            save_as(lv->title, v.l, lv->kind == LV_QUEUE);
                            theme_off();
                            break;
                        case BTN_NOWPLAYING:
                            ret = GO_TO_WPS; done = true;
                            break;
                    }
                }
                else if (v.l->n > 0)
                {
                    int i = v.sel - v.n_btn;
                    if (lv->kind == LV_QUEUE)
                    {
                        playlist_start(queue_abs_index(i), 0, 0);
                        wheel_flow_play_started();
                        ret = GO_TO_WPS;
                    }
                    else if (lv->kind == LV_PLAYLIST_FILE && v.l->total > v.l->n)
                        ret = play_file(lv->file, i, false) ? GO_TO_WPS : 0;
                    else
                        ret = wheel_play_list(v.l, i, false) ? GO_TO_WPS : 0;
                    if (ret == GO_TO_WPS)
                        note_origin(lv);
                    done = ret != 0;
                }
                break;
            case ACTION_STD_CONTEXT:
                if (v.sel < v.n_btn && !lv->fixed)
                {
                    theme_on();
                    if (wheel_pick_playlist_view())
                    {
                        ret = WHEEL_VIEW_SWITCH;
                        done = true;
                    }
                    theme_off();
                }
                else if (v.sel >= v.n_btn && v.l->n > 0)
                {
                    bool changed = false;
                    int r = track_options(v.sel - v.n_btn, &changed);
                    if (r) { ret = r; done = true; }
                    if (changed)
                    {
                        load_queue(v.l);
                        wheel_list_fill_meta(v.l);
                    }
                    recount();
                    find_playing();
                    v.sel = MIN(v.sel, v.n_btn + v.l->n - 1);
                }
                break;
            case ACTION_STD_CANCEL:
                done = true;
                break;
            case ACTION_STD_MENU:
                ret = GO_TO_ROOT;
                done = true;
                break;
            case ACTION_NONE:
                /* fill in lengths in the background so the total gets exact */
                for (int k = 0; k < 4 && v.fill_next < v.l->n; k++, v.fill_next++)
                    wheel_track_fill_meta(&v.l->t[v.fill_next]);
                if (v.fill_next >= v.l->n && v.known < v.l->n)
                {
                    recount();
                    if (lv->kind == LV_PLAYLIST_FILE && v.known == v.l->total)
                        cache_store(lv->file, v.l->total, v.secs);
                }
                if (TIME_AFTER(current_tick, last_queue_check + HZ))
                {
                    last_queue_check = current_tick;
                    int was = v.playing;
                    find_playing();
                    if (lv->kind == LV_QUEUE && playlist_amount() != v.l->total)
                    {
                        load_queue(v.l);
                        wheel_list_fill_meta(v.l);
                        recount();
                    }
                    if (lv->kind == LV_QUEUE && was != v.playing)
                    {   /* next song (or playback just started): new cover */
                        pick_covers();
                        v.tint = header_tint();
                        if (!moved && v.playing >= 0)
                        {
                            v.sel = v.n_btn + v.playing;
                            v.top = MAX(0, v.playing - 2);
                        }
                    }
                }
                break;
            default:
                if (default_event_handler(a) == SYS_USB_CONNECTED)
                {
                    ret = GO_TO_ROOT;
                    done = true;
                }
                break;
        }
    }
    theme_on();
    v = saved;
    themed_view = saved_themed;
    return ret;
}

/* ============== where Now Playing goes back to (the list) ============== */
/* Back from Now Playing opens the list the music was started from, not the
 * main menu. The record is dropped as soon as another playlist plays. */
static struct {
    int kind;                 /* LV_*, -1: none */
    char file[MAX_PATH];      /* playlist file */
    char title[96];
    char first[MAX_PATH];     /* the playlist's first song, to recognise it */
    int amount;
    bool shuffle;             /* shuffle then (turning it on or off reorders) */
} origin = { .kind = -1 };

static void note_origin(struct wheel_lv *lv)
{
    struct playlist_track_info info;
    origin.kind = -1;
    if (!lv || playlist_amount() <= 0 || playlist_get_track_info(NULL, 0, &info) < 0)
        return;
    origin.kind = lv->kind;
    strlcpy(origin.file, lv->file ? lv->file : "", sizeof(origin.file));
    strlcpy(origin.title, lv->title ? lv->title : "", sizeof(origin.title));
    strlcpy(origin.first, info.filename, sizeof(origin.first));
    origin.amount = playlist_amount();
    origin.shuffle = global_settings.playlist_shuffle;
}

static int queue_view_titled(const char *title);

int wheel_back_from_wps(int *after)
{
    *after = GO_TO_ROOT;
    if (origin.kind < 0)
        return WHEEL_NO_ORIGIN;
    struct playlist_track_info info;
    bool same = playlist_amount() >= origin.amount &&
                playlist_get_track_info(NULL, 0, &info) >= 0 &&
                !strcmp(info.filename, origin.first);
    /* the same songs in another order: shuffle was turned on or off since */
    if (!same && playlist_amount() == origin.amount &&
        global_settings.playlist_shuffle != origin.shuffle)
        same = true;
    if (!same)
    {   /* something else plays now */
        origin.kind = -1;
        return WHEEL_NO_ORIGIN;
    }
    if (origin.kind == LV_PLAYLIST_FILE && origin.file[0] && file_exists(origin.file))
    {
        *after = GO_TO_PLAYLISTS_SCREEN;
        return wheel_open_playlist_file(origin.file);
    }
    return queue_view_titled(origin.title[0] ? origin.title : (const char *)str(LANG_WHEEL_UP_NEXT));
}

/* ===================== the other views of a list ===================== */

/* load the queue and the tags; v must point at lv */
static void prepare(struct wheel_lv *lv)
{
    memset(&v, 0, sizeof(v));
    v.lv = lv;
    v.l = lv->list;
    load_fonts();
    long t0 = current_tick;
    if (lv->kind == LV_QUEUE)
        load_queue(v.l);
    splash(0, ID2P(LANG_WAIT));
    wheel_list_fill_meta(v.l);
    find_playing();
    char what[48];
    snprintf(what, sizeof(what), "view: open %d songs", v.l->n);
    wheel_perf(what, current_tick - t0);
}

/* play from track i; true when playback started */
static bool play_at_(int i, bool shuffle);
static bool play_at(int i, bool shuffle)
{
    bool ok = play_at_(i, shuffle);
    if (ok)
        note_origin(v.lv);
    return ok;
}
static bool play_at_(int i, bool shuffle)
{
    struct wheel_lv *lv = v.lv;
    if (lv->kind == LV_QUEUE)
    {   /* the queue itself, not a copy of it */
        if (shuffle)
        {
            playlist_shuffle(current_tick, -1);
            playlist_start(0, 0, 0);
        }
        else
            playlist_start(queue_abs_index(i), 0, 0);
        wheel_flow_play_started();
        return true;
    }
    if (lv->kind == LV_PLAYLIST_FILE && v.l->total > v.l->n)
        return play_file(lv->file, i, shuffle);
    return wheel_play_list(v.l, i, shuffle);
}

static const char *track_name(struct wheel_track *t)
{
    if (!t->meta)
        wheel_track_fill_meta(t);
    if (t->title[0])
        return t->title;
    const char *base = strrchr(t->path, '/');
    return base ? base + 1 : t->path;
}

/* after options: the queue or the file may have changed */
static void reload_after_options(bool changed)
{
    if (changed && v.lv->kind == LV_QUEUE)
    {
        load_queue(v.l);
        wheel_list_fill_meta(v.l);
    }
    find_playing();
}

/* ------------------------- the classic text list ------------------------- */
/* one line per song, "Shuffle" on top, a mark on the one playing */

static int cl_result;

static const char *cl_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (item == 0)
        return str(LANG_WHEEL_SHUFFLE);
    if (item - 1 >= v.l->n)
        return str(LANG_WHEEL_LIST_EMPTY);
    /* the song playing gets a note on the right (themes may hide icons) */
    if (item - 1 == v.playing)
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "♪", track_name(&v.l->t[item - 1]));
    else
        strlcpy(buf, track_name(&v.l->t[item - 1]), len);
    return buf;
}

static enum themable_icons cl_get_icon(int item, void *data)
{
    (void)data;
    if (item == 0)
        return Icon_Playlist;
    return item - 1 == v.playing ? Icon_Audio : Icon_NOICON;
}

static int cl_action(int action, struct gui_synclist *lists)
{
    int item = gui_synclist_get_sel_pos(lists);
    switch (action)
    {
        case ACTION_STD_MENU:
            cl_result = GO_TO_ROOT;
            return ACTION_STD_CANCEL;
        case ACTION_TREE_WPS:
            cl_result = GO_TO_WPS;
            return ACTION_STD_CANCEL;
        case ACTION_STD_OK:
            if (v.l->n <= 0)
                return ACTION_REDRAW;
            if (item == 0 ? play_at(0, true) : play_at(item - 1, false))
            {
                cl_result = GO_TO_WPS;
                return ACTION_STD_CANCEL;
            }
            return ACTION_REDRAW;
        case ACTION_STD_CONTEXT:
        {
            int r = 0;
            if (item == 0 || v.l->n <= 0)
                r = wheel_pick_playlist_view() ? WHEEL_VIEW_SWITCH : 0;
            else
            {
                bool changed = false;
                v.sel = v.n_btn + item - 1;
                r = track_options(item - 1, &changed);
                reload_after_options(changed);
                if (!r)
                    gui_synclist_select_item(lists, MIN(v.sel - v.n_btn + 1, v.l->n));
            }
            if (r)
            {
                cl_result = r;
                return ACTION_STD_CANCEL;
            }
            gui_synclist_set_nb_items(lists, v.l->n + 1);
            gui_synclist_set_title(lists, v.lv->title, Icon_NOICON);
            return ACTION_REDRAW;
        }
        case ACTION_NONE:
        {
            /* the mark follows playback; Up Next follows the queue */
            int was = v.playing, n = v.l->n;
            if (v.lv->kind == LV_QUEUE && playlist_amount() != v.l->total)
            {
                load_queue(v.l);
                wheel_list_fill_meta(v.l);
                gui_synclist_set_nb_items(lists, v.l->n + 1);
            }
            find_playing();
            return (was != v.playing || n != v.l->n) ? ACTION_REDRAW : action;
        }
    }
    return action;
}

static int show_classic(struct wheel_lv *lv)
{
    bool saved_themed = themed_view;
    themed_view = true;
    prepare(lv);
    v.n_btn = 0;
    cl_result = 0;
    struct simplelist_info info;
    simplelist_info_init(&info, (char *)lv->title, v.l->n + 1, NULL);
    info.get_name = cl_get_name;
    info.get_icon = cl_get_icon;
    info.action_callback = cl_action;
    info.timeout = HZ;
    int carried = take_switch_sel();
    info.selection = carried >= 0 && carried < v.l->n ? carried + 1 :
                     v.playing >= 0 ? v.playing + 1 : (v.l->n > 0 ? 1 : 0);
    push_current_activity(ACTIVITY_DATABASEBROWSER);
    simplelist_show_list(&info);
    if (cl_result && cl_result != WHEEL_VIEW_SWITCH)
        pop_current_activity_without_refresh();
    else
        pop_current_activity();
    themed_view = saved_themed;
    return cl_result;
}

/* --------------------------- the cover views --------------------------- */

static void lvf_text(struct wheel_flow_src *s, int i, struct wheel_flow_text *t)
{
    (void)s;
    struct wheel_track *tr = &v.l->t[i];
    strlcpy(t->title, track_name(tr), sizeof(t->title));
    strlcpy(t->sub, tr->artist, sizeof(t->sub));
    if (tr->len_ms)
    {
        unsigned sec = tr->len_ms / 1000;
        snprintf(t->meta, sizeof(t->meta), "%u:%02u", sec / 60, sec % 60);
    }
}

static bool lvf_track(struct wheel_flow_src *s, int i, char *path, size_t len)
{
    (void)s;
    if (i < 0 || i >= v.l->n)
        return false;
    strlcpy(path, v.l->t[i].path, len);
    return true;
}

static int lvf_ok(struct wheel_flow_src *s, int i, bool stay)
{
    (void)s;
    if (!play_at(i, false))
        return 0;
    return stay ? 0 : GO_TO_WPS;
}

static int lvf_options(struct wheel_flow_src *s, int i)
{
    bool changed = false;
    v.sel = v.n_btn + i;
    int r = track_options(i, &changed);
    reload_after_options(changed);
    s->n = v.l->n;
    if (!r)
        s->sel = MAX(0, MIN(v.sel - v.n_btn, v.l->n - 1));
    return r ? r : WHEEL_FLOW_CHANGED;
}

static int lvf_playing(struct wheel_flow_src *s)
{
    if (v.lv->kind == LV_QUEUE && playlist_amount() != v.l->total)
    {
        load_queue(v.l);
        wheel_list_fill_meta(v.l);
        s->n = v.l->n;
    }
    find_playing();
    return v.playing;
}

static int show_cover_view(struct wheel_lv *lv, bool disc)
{
    bool saved_themed = themed_view;
    themed_view = false;
    prepare(lv);
    v.n_btn = 0;
    if (v.l->n <= 0)
    {
        themed_view = saved_themed;
        return show_wheel(lv);   /* nothing to show covers of */
    }
    int carried = take_switch_sel();
    struct wheel_flow_src s = {
        .n = v.l->n, .sel = carried >= 0 && carried < v.l->n ? carried : MAX(0, v.playing),
        .heading = lv->title,
        .text = lvf_text, .track = lvf_track, .ok = lvf_ok,
        .options = lvf_options, .playing = lvf_playing,
    };
    int r = disc ? wheel_disc_show(&s) : wheel_flow_show(&s);
    themed_view = saved_themed;
    return r;
}

/* playlists, mixes and Up Next open in the view picked in Appearance (or
 * from a long press); album and folder lists always use the Wheel list */
int wheel_listview_show(struct wheel_lv *lv)
{
    typeof(v) saved = v;
    int view = lv->fixed ? PLV_WHEEL : global_settings.wheel_pl_view;
    int r;
    for (;;)
    {
        if ((view == PLV_FLOW || view == PLV_DISC) && !wheel_flow_supported())
            view = PLV_WHEEL;
        switch (view)
        {
            case PLV_CLASSIC: r = show_classic(lv); break;
            case PLV_FLOW:    r = show_cover_view(lv, false); break;
            case PLV_DISC:    r = show_cover_view(lv, true); break;
            default:          r = show_wheel(lv); break;
        }
        if (r != WHEEL_VIEW_SWITCH)
            break;
        view = global_settings.wheel_pl_view;
    }
    v = saved;
    return r;
}

/* ============================ entry points ============================ */

static int queue_view_titled(const char *title)
{
    if (playlist_amount() <= 0)
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_NOTHING_PLAYING));
        return GO_TO_PREVIOUS;
    }
    struct wheel_lv lv = { .title = title, .subtitle = str(LANG_WHEEL_QUEUE),
                           .kind = LV_QUEUE, .list = &lv_main };
    int r = wheel_listview_show(&lv);
    return r ? r : GO_TO_PREVIOUS;
}

int wheel_queue_view(void)
{
    return queue_view_titled(str(LANG_WHEEL_UP_NEXT));
}

int wheel_open_playlist_file(const char *file)
{
    char title[96];
    const char *base = strrchr(file, '/');
    strlcpy(title, base ? base + 1 : file, sizeof(title));
    char *dot = strrchr(title, '.');
    if (dot) *dot = '\0';
    wheel_load_playlist_file(file, &lv_main);
    struct wheel_lv lv = { .title = title, .subtitle = str(LANG_PLAYLIST),
                           .kind = LV_PLAYLIST_FILE, .file = file, .list = &lv_main };
    return wheel_listview_show(&lv);
}

/* an album folder as a list (sorted by file name, which is track order
 * for nearly every library) */
static int name_cmp(const void *a, const void *b)
{
    return strcasecmp(((const struct wheel_track *)a)->path, ((const struct wheel_track *)b)->path);
}

int wheel_open_folder(const char *dir, const char *title, const char *subtitle)
{
    struct wheel_list *l = &lv_main;
    l->n = l->total = 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) && l->n < l->max)
    {
        if (e->d_name[0] == '.')
            continue;
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if ((filetype_get_attr(path) & FILE_ATTR_MASK) != FILE_ATTR_AUDIO)
            continue;
        struct wheel_track *t = &l->t[l->n++];
        strlcpy(t->path, path, sizeof(t->path));
        t->title[0] = t->artist[0] = '\0';
        t->len_ms = 0;
        t->meta = false;
    }
    closedir(d);
    l->total = l->n;
    qsort(l->t, l->n, sizeof(l->t[0]), name_cmp);
    struct wheel_lv lv = { .title = title, .subtitle = subtitle, .kind = LV_MIX, .list = l,
                           .fixed = true };
    return wheel_listview_show(&lv);
}

/* ------------------------- the Playlists screen ------------------------- */

#define MAX_PLAYLISTS 200
static struct {
    char names[MAX_PLAYLISTS][64];
    int n;
    bool have_queue;
} pls;

static void scan_playlists(void)
{
    pls.n = 0;
    pls.have_queue = playlist_amount() > 0;
    DIR *dir = opendir(catalog_dir());
    if (!dir)
        return;
    struct dirent *e;
    while ((e = readdir(dir)) && pls.n < MAX_PLAYLISTS)
    {
        if (e->d_name[0] == '.' || !is_playlist_name((char *)e->d_name))
            continue;
        strlcpy(pls.names[pls.n++], (char *)e->d_name, 64);
    }
    closedir(dir);
    /* alphabetical */
    for (int i = 1; i < pls.n; i++)
        for (int j = i; j > 0 && strcasecmp(pls.names[j - 1], pls.names[j]) > 0; j--)
        {
            char tmp[64];
            strcpy(tmp, pls.names[j]);
            strcpy(pls.names[j], pls.names[j - 1]);
            strcpy(pls.names[j - 1], tmp);
        }
}

static const char *pl_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (pls.have_queue)
    {
        if (item == 0)
        {
            char c[32];
            song_count(playlist_amount(), c, sizeof(c));
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_UP_NEXT), c);
            return buf;
        }
        item--;
    }
    if (item >= pls.n)
        return str(LANG_WHEEL_NO_PLAYLISTS);
    char name[64], path[MAX_PATH], val[48];
    strlcpy(name, pls.names[item], sizeof(name));
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    snprintf(path, sizeof(path), "%s/%s", catalog_dir(), pls.names[item]);
    int count; unsigned long secs;
    if (wheel_playlist_cached(path, &count, &secs))
    {
        char dur[24];
        wheel_format_duration(secs, dur, sizeof(dur));
        snprintf(val, sizeof(val), "%d · %s", count, dur);
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", name, val);
    }
    else
        strlcpy(buf, name, len);
    return buf;
}

static bool pl_get_nav(int item, void *data)
{
    (void)data;
    return pls.have_queue || item < pls.n;
}

static int pl_result;
static int pl_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, pl_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
    {
        pl_result = GO_TO_ROOT;
        return ACTION_STD_CANCEL;
    }
    if (action != ACTION_STD_OK)
        return action;
    int item = gui_synclist_get_sel_pos(lists);
    if (pls.have_queue && item == 0)
        pl_result = wheel_queue_view();
    else
    {
        item -= pls.have_queue ? 1 : 0;
        if (item < 0 || item >= pls.n)
            return ACTION_REDRAW;
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", catalog_dir(), pls.names[item]);
        pl_result = wheel_open_playlist_file(path);
    }
    if (pl_result == GO_TO_PREVIOUS)
        pl_result = 0;
    if (pl_result)
        return ACTION_STD_CANCEL;
    scan_playlists();
    gui_synclist_set_nb_items(lists, MAX(1, pls.n + (pls.have_queue ? 1 : 0)));
    gui_synclist_set_title(lists, str(LANG_PLAYLISTS), Icon_NOICON);
    return ACTION_REDRAW;
}

int wheel_playlists_screen(void)
{
    struct simplelist_info info;
    scan_playlists();
    pl_result = 0;
    simplelist_info_init(&info, str(LANG_PLAYLISTS),
                         MAX(1, pls.n + (pls.have_queue ? 1 : 0)), NULL);
    info.get_name = pl_get_name;
    info.action_callback = pl_action;
    push_current_activity(ACTIVITY_PLAYLISTBROWSER);
    simplelist_show_list(&info);
    if (pl_result)
        pop_current_activity_without_refresh();
    else
        pop_current_activity();
    return pl_result;
}
