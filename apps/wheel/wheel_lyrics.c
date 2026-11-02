/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - Now Playing modes and synced lyrics.
 *
 * Center button on Now Playing cycles:
 *   normal (wheel = volume) -> scrub (wheel = seek) -> lyrics -> normal
 * Lyrics come from a .lrc file next to the song (same name), shown by the
 * theme with the %Wa %Wp %Wc %Wn %Wo tags (two lines before/after the
 * current one).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "file.h"
#include "audio.h"
#include "appevents.h"
#include "metadata.h"
#include "pathfuncs.h"
#include "string-extra.h"
#include "misc.h"
#include "wheel_lyrics.h"
#include "wheel_typo.h"
#include "wheel_connect.h"
#include "wheel_art.h"
#include "lang.h"
#include "action.h"
#include "font.h"
#include "screen_access.h"
#include "viewport.h"
#include "settings.h"
#include "rbpaths.h"
#include "rbunicode.h"

#define MAX_LINES  400
#define TEXT_SIZE  (24*1024)
#define SCRUB_IDLE (HZ*4)

struct lrc_line { long ms; unsigned short off; };

static struct lrc_line lines[MAX_LINES];
static char text[TEXT_SIZE];
static int n_lines;
static char track_path[MAX_PATH];
static bool need_load;
static bool synced;

static int mode = WHEEL_MODE_NORMAL;
static long last_scrub;
static int scrub_streak;

/* ----------------------------- LRC parser ----------------------------- */

static int cmp_lines(const void *a, const void *b)
{
    long d = ((const struct lrc_line *)a)->ms - ((const struct lrc_line *)b)->ms;
    return d < 0 ? -1 : d > 0;
}

/* "[mm:ss.xx]" -> ms, returns chars consumed or 0 */
static int parse_stamp(const char *p, long *ms)
{
    if (p[0] != '[' || !isdigit((unsigned char)p[1]))
        return 0;
    char *end;
    long min = strtol(p + 1, &end, 10);
    if (*end != ':')
        return 0;
    long sec = strtol(end + 1, &end, 10);
    long frac = 0, scale = 1;
    if (*end == '.' || *end == ':')
    {
        const char *f = end + 1;
        while (isdigit((unsigned char)*f) && scale < 1000)
        {
            frac = frac * 10 + (*f - '0');
            scale *= 10;
            f++;
        }
        while (isdigit((unsigned char)*f)) f++;
        end = (char *)f;
    }
    if (*end != ']')
        return 0;
    *ms = (min * 60 + sec) * 1000 + (frac * 1000) / scale;
    return end - p + 1;
}

static void load_lrc(void)
{
    n_lines = 0;
    need_load = false;
    if (!track_path[0])
        return;

    char lrc[MAX_PATH];
    strlcpy(lrc, track_path, sizeof(lrc));
    char *dot = strrchr(lrc, '.');
    char *slash = strrchr(lrc, '/');
    if (!dot || (slash && dot < slash))
        return;
    strlcpy(dot, ".lrc", sizeof(lrc) - (dot - lrc));
    int fd = open(lrc, O_RDONLY);
    if (fd < 0)
    {
        strlcpy(dot, ".LRC", sizeof(lrc) - (dot - lrc));
        fd = open(lrc, O_RDONLY);
        if (fd < 0)
            return;
    }

    char line[512];
    size_t used = 0;
    while (read_line(fd, line, sizeof(line)) > 0 && n_lines < MAX_LINES)
    {
        const char *p = line;
        if (!strncmp(p, "\xef\xbb\xbf", 3)) /* UTF-8 BOM */
            p += 3;
        long stamps[8];
        int n_st = 0, used_ch;
        long ms;
        while (n_st < 8 && (used_ch = parse_stamp(p, &ms)) > 0)
        {
            stamps[n_st++] = ms;
            p += used_ch;
        }
        if (n_st == 0)
        {
            /* [ar:...] style tags are metadata; anything else is plain text */
            if (p[0] == '[' || !p[0] || p[0] == '\r')
                continue;
            stamps[n_st++] = -1; /* unsynced line */
        }
        while (*p == ' ')
            p++;
        size_t len = strlen(p);
        while (len && (p[len-1] == '\r' || p[len-1] == ' '))
            len--;
        if (used + len + 1 > sizeof(text))
            break;
        memcpy(text + used, p, len);
        text[used + len] = '\0';
        for (int i = 0; i < n_st && n_lines < MAX_LINES; i++)
        {
            lines[n_lines].ms = stamps[i];
            lines[n_lines].off = used;
            n_lines++;
        }
        used += len + 1;
    }
    close(fd);
    /* mixed files: if anything is timed, drop the untimed lines */
    int timed = 0;
    for (int i = 0; i < n_lines; i++)
        if (lines[i].ms >= 0) timed++;
    if (timed)
    {
        int k = 0;
        for (int i = 0; i < n_lines; i++)
            if (lines[i].ms >= 0) lines[k++] = lines[i];
        n_lines = k;
        qsort(lines, n_lines, sizeof(lines[0]), cmp_lines);
    }
    synced = timed > 0;
}

static void track_changed(unsigned short id, void *param)
{
    (void)id;
    struct mp3entry *id3 = ((struct track_event *)param)->id3;
    if (id3)
        strlcpy(track_path, id3->path, sizeof(track_path));
    else
        track_path[0] = '\0';
    need_load = true;
    if (mode == WHEEL_MODE_SCRUB)
        mode = WHEEL_MODE_NORMAL;
}

void wheel_lyrics_init(void)
{
    add_event(PLAYBACK_EVENT_TRACK_CHANGE, track_changed);
}

bool wheel_lyrics_available(void)
{
    if (need_load)
        load_lrc();
    return n_lines > 0;
}

/* rel = -2..2 around the line being sung now */
const char *wheel_lyrics_line(int rel)
{
    if (!wheel_lyrics_available())
        return NULL;
    struct mp3entry *id3 = audio_current_track();
    long pos = id3 ? (long)id3->elapsed : 0;
    int cur = -1;
    for (int i = 0; i < n_lines && lines[i].ms <= pos; i++)
        cur = i;
    int want = cur + rel;
    if (want < 0 || want >= n_lines)
        return NULL;
    const char *t = text + lines[want].off;
    return *t ? t : NULL;
}

/* --------------------------- Now Playing modes ------------------------- */

int wheel_wps_mode(void) { return mode; }

void wheel_wps_reset_mode(void) { mode = WHEEL_MODE_NORMAL; }

/* centre button on Now Playing:
 *   normal -> lyrics (full screen) -> scrub -> normal
 * the lyrics step is skipped when there are none and no way to fetch them */
bool wheel_wps_cycle_mode(void)
{
    switch (mode)
    {
        case WHEEL_MODE_NORMAL:
            if (wheel_lyrics_available() || wheel_net_available())
            {
                mode = WHEEL_MODE_LYRICS;
                bool to_scrub = wheel_lyrics_screen();
                mode = to_scrub ? WHEEL_MODE_SCRUB : WHEEL_MODE_NORMAL;
            }
            else
                mode = WHEEL_MODE_SCRUB;
            break;
        case WHEEL_MODE_SCRUB:
        default:
            mode = WHEEL_MODE_NORMAL;
            break;
    }
    if (mode == WHEEL_MODE_SCRUB)
    {
        last_scrub = current_tick;
        scrub_streak = 0;
    }
    return true;
}

/* one wheel step while scrubbing; speeds up when turned continuously */
void wheel_wps_scrub(int dir)
{
    struct mp3entry *id3 = audio_current_track();
    if (!id3 || !id3->length)
        return;
    if (TIME_BEFORE(current_tick, last_scrub + HZ/4))
        scrub_streak = MIN(scrub_streak + 1, 20);
    else
        scrub_streak = 0;
    last_scrub = current_tick;
    long step = 2000 + scrub_streak * 1000; /* 2 s, up to 22 s per detent */
    long pos = (long)id3->elapsed + dir * step;
    if (pos < 0)
        pos = 0;
    if (pos > (long)id3->length - 1000)
        pos = id3->length - 1000;
    audio_ff_rewind(pos); /* seeks while playing or paused */
}

/* called on WPS timeouts: leave scrub mode after a few idle seconds */
bool wheel_wps_idle(void)
{
    if (mode == WHEEL_MODE_SCRUB && TIME_AFTER(current_tick, last_scrub + SCRUB_IDLE))
    {
        mode = WHEEL_MODE_NORMAL;
        return true;
    }
    return false;
}


/* ===================== full-screen lyrics (synced) ====================== */

#define LY_MARGIN     24
#define LY_TOP        76      /* lyrics area */
#define LY_BOTTOM     (LCD_HEIGHT - 22)
#define LY_ANCHOR     (LY_TOP + 46)   /* where the current line sits */
#define LY_MAX_ROWS   900     /* wrapped rows */
#define FOLLOW_AFTER  (HZ*3)  /* manual scroll, then follow again */

struct ly_row { unsigned short line; unsigned short off; unsigned char len; short y; };
static struct ly_row rows[LY_MAX_ROWS];
static int n_rows;
static int line_first_row[MAX_LINES + 1];
static int font_big = -1, font_small = -1;
WHEEL_ART_INIT(ly_art, 48);

static unsigned blend(unsigned a, unsigned b, int pct) /* pct of a */
{
    int r = (RGB_UNPACK_RED(a) * pct + RGB_UNPACK_RED(b) * (100 - pct)) / 100;
    int g = (RGB_UNPACK_GREEN(a) * pct + RGB_UNPACK_GREEN(b) * (100 - pct)) / 100;
    int bl = (RGB_UNPACK_BLUE(a) * pct + RGB_UNPACK_BLUE(b) * (100 - pct)) / 100;
    return LCD_RGBPACK(r, g, bl);
}

/* word-wrap every lyric line into rows that fit the screen */
/* Han, kana and CJK punctuation (Hangul is written with spaces) */
static bool is_cjk_cp(ucschar_t c)
{
    return (c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) ||
           (c >= 0xff00 && c <= 0xffef);
}
static bool is_cjk_at(const char *t, int pos)
{
    ucschar_t c;
    utf8decode((const unsigned char *)t + pos, &c);
    return is_cjk_cp(c);
}
static bool is_cjk_before(const char *t, int pos)
{
    int p = pos - 1;
    while (p > 0 && ((t[p] & 0xc0) == 0x80)) p--;
    return p >= 0 && is_cjk_at(t, p);
}

static void wrap_all(int font, int width)
{
    n_rows = 0;
    for (int i = 0; i < n_lines && n_rows < LY_MAX_ROWS; i++)
    {
        line_first_row[i] = n_rows;
        const char *t = text + lines[i].off;
        int start = 0, len = strlen(t);
        if (len == 0)
        {   /* instrumental gap: one empty row */
            rows[n_rows++] = (struct ly_row){ i, lines[i].off, 0, 0 };
            continue;
        }
        while (start < len && n_rows < LY_MAX_ROWS)
        {
            int best = len - start, w, h;
            char tmp[256];
            /* longest run of whole words that fits; Chinese and Japanese
             * have no spaces, so they may break between any characters */
            int end = start, last_ok = -1;
            while (end <= len)
            {
                if (end == len || t[end] == ' ' ||
                    (end > start && ((t[end] & 0xc0) != 0x80) &&
                     (is_cjk_at(t, end) || is_cjk_before(t, end))))
                {
                    int n = MIN(end - start, (int)sizeof(tmp) - 1);
                    memcpy(tmp, t + start, n); tmp[n] = 0;
                    font_getstringsize(tmp, &w, &h, font);
                    if (w > width && last_ok > start)
                        break;
                    last_ok = end;
                }
                end++;
            }
            best = (last_ok > start ? last_ok : len) - start;
            rows[n_rows++] = (struct ly_row){ i, lines[i].off + start, MIN(best, 255), 0 };
            start += best;
            while (start < len && t[start] == ' ') start++;
        }
    }
    line_first_row[n_lines] = n_rows;
    /* rows of one line sit tight, lines get air between them; the
     * typography line spacing sets how much (135 % = the classic look) */
    int pct = typo_active()->ly_pct;
    if (pct < 100 || pct > 220) pct = 135;
    int between = 16 * (pct - 100) / 35, within = 2 * pct / 135;
    int fh = font_get(font)->height, y = 0;
    for (int r = 0; r < n_rows; r++)
    {
        rows[r].y = y;
        y += fh + ((r + 1 < n_rows && rows[r + 1].line == rows[r].line) ? within : between);
    }
}

static int current_line(void)
{
    struct mp3entry *id3 = audio_current_track();
    long pos = id3 ? (long)id3->elapsed + 250 : 0; /* show a hair early */
    int cur = -1;
    for (int i = 0; i < n_lines && lines[i].ms <= pos; i++)
        cur = i;
    return cur;
}

static void draw_lyrics(int scroll_px, int cur, unsigned bg, const char *status)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    unsigned white = LCD_RGBPACK(0xff, 0xff, 0xff);

    d->set_background(bg);
    d->set_drawmode(DRMODE_SOLID);
    d->clear_viewport();

    /* header: small cover, title and artist */
    struct mp3entry *id3 = audio_current_track();
    int hx = LY_MARGIN;
    if (id3 && wheel_art_load(&ly_art, id3->path))
    {
        wheel_art_draw(&ly_art, d, LY_MARGIN, 14, 48);
        hx += 60;
    }
    if (id3)
    {
        vp.font = font_small >= 0 ? font_small : FONT_UI;
        d->set_foreground(white);
        d->set_drawmode(DRMODE_FG);
        d->putsxy(hx, 16, id3->title ? id3->title : "");
        d->set_foreground(blend(white, bg, 65));
        d->putsxy(hx, 40, id3->artist ? id3->artist : "");
    }

    vp.font = font_big >= 0 ? font_big : FONT_UI;
    int rh = font_get(vp.font)->height + 6;
    if (status)
    {
        d->set_foreground(blend(white, bg, 80));
        int w, h;
        d->getstringsize(status, &w, &h);
        d->putsxy((LCD_WIDTH - w) / 2, (LY_TOP + LY_BOTTOM - h) / 2, status);
    }
    else
    {
        for (int r = 0; r < n_rows; r++)
        {
            int y = LY_ANCHOR + rows[r].y - scroll_px;
            if (y < LY_TOP || y + rh > LY_BOTTOM)
                continue;
            int ln = rows[r].line;
            unsigned col;
            if (!synced)
                col = white;
            else if (ln == cur)
                col = white;
            else if (ln < cur)
                col = blend(white, bg, 45);   /* already sung */
            else
                col = blend(white, bg, 62);   /* coming up */
            /* fade rows near the edges */
            if (y < LY_TOP + rh || y + 2 * rh > LY_BOTTOM)
                col = blend(col, bg, 60);
            d->set_foreground(col);
            char tmp[256];
            memcpy(tmp, text + rows[r].off, rows[r].len);
            tmp[rows[r].len] = 0;
            d->putsxy(LY_MARGIN, y, tmp);
        }
        /* cover the area under the header so scrolled rows don't overlap it */
        d->set_drawmode(DRMODE_SOLID|DRMODE_INVERSEVID);
        d->fillrect(0, 0, LCD_WIDTH, LY_TOP - 6);
        d->set_drawmode(DRMODE_FG);
        if (id3)
        {   /* redraw the header on top */
            if (ly_art.valid)
                wheel_art_draw(&ly_art, d, LY_MARGIN, 14, 48);
            vp.font = font_small >= 0 ? font_small : FONT_UI;
            d->set_foreground(white);
            d->putsxy(hx, 16, id3->title ? id3->title : "");
            d->set_foreground(blend(white, bg, 65));
            d->putsxy(hx, 40, id3->artist ? id3->artist : "");
        }
    }

    /* thin progress line at the bottom */
    if (id3 && id3->length)
    {
        int w = (LCD_WIDTH - 2 * LY_MARGIN);
        int done = (long long)w * id3->elapsed / id3->length;
        d->set_drawmode(DRMODE_SOLID);
        d->set_foreground(blend(white, bg, 25));
        d->fillrect(LY_MARGIN, LCD_HEIGHT - 10, w, 3);
        d->set_foreground(white);
        d->fillrect(LY_MARGIN, LCD_HEIGHT - 10, done, 3);
    }
    d->set_drawmode(DRMODE_SOLID);
    d->update();
    d->set_viewport(last);
}

/* returns true if the user asked for scrub mode next */
bool wheel_lyrics_screen(void)
{
    /* fonts of the typography profile (lyrics line, details) */
    font_big = typo_font(TU_LYRICS);
    font_small = typo_font(TU_META);
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);

    char shown_path[MAX_PATH] = "";
    int width = LCD_WIDTH - 2 * LY_MARGIN;
    int rh = font_get(font_big >= 0 ? font_big : FONT_UI)->height + 2;
    int scroll = 0, target = 0, manual = 0;
    long manual_until = 0;
    bool fetching = false, to_scrub = false, done = false;
    const char *status = NULL;
    unsigned bg = LCD_RGBPACK(0x2a, 0x2a, 0x30);

    while (!done)
    {
        struct mp3entry *id3 = audio_current_track();
        if (!id3)
            break;
        if (strcmp(shown_path, id3->path))
        {   /* new song: reload, rewrap, recolour */
            strlcpy(shown_path, id3->path, sizeof(shown_path));
            need_load = true;
            wheel_lyrics_available();
            wrap_all(font_big >= 0 ? font_big : FONT_UI, width);
            bg = wheel_art_load(&ly_art, id3->path) ? ly_art.tint
                                                    : LCD_RGBPACK(0x2a, 0x2a, 0x30);
            scroll = target = manual = 0;
            status = NULL;
            fetching = false;
            if (n_lines == 0)
            {
                if (wheel_lyrics_fetch_start(id3->artist, id3->title, id3->album,
                                             id3->length / 1000, id3->path))
                {
                    fetching = true;
                    status = str(LANG_WHEEL_LYRICS_SEARCHING);
                }
                else
                    status = str(LANG_WHEEL_NO_LYRICS);
            }
        }
        if (fetching)
        {
            int st = wheel_lyrics_fetch_state();
            if (st != 0)
            {
                fetching = false;
                if (st > 0)
                {
                    need_load = true;
                    wheel_lyrics_available();
                    wrap_all(font_big >= 0 ? font_big : FONT_UI, width);
                    status = n_lines ? NULL : str(LANG_WHEEL_NO_LYRICS);
                }
                else
                    status = str(LANG_WHEEL_NO_LYRICS);
            }
        }

        int cur = synced ? current_line() : -1;
        if (!status)
        {
            if (TIME_AFTER(current_tick, manual_until))
            {   /* follow the song: current line at the anchor */
                int row = cur >= 0 ? line_first_row[cur] : 0;
                target = n_rows ? rows[MIN(row, n_rows - 1)].y : 0;
                manual = 0;
            }
            else
                target = manual;
            /* ease towards the target (looks like Spotify's glide) */
            int diff = target - scroll;
            if (diff > 2 || diff < -2)
                scroll += diff / 3 + (diff > 0 ? 1 : -1);
            else
                scroll = target;
        }
        draw_lyrics(scroll, cur, bg, status);

        int action = get_action(CONTEXT_WPS, scroll != target ? HZ/25 : HZ/5);
        switch (action)
        {
            case ACTION_WPS_VOLUP:     /* wheel: scroll the lyrics */
            case ACTION_WPS_VOLDOWN:
                if (!status)
                {
                    manual = (TIME_AFTER(current_tick, manual_until) ? scroll : manual)
                             + (action == ACTION_WPS_VOLUP ? rh : -rh);
                    manual = MAX(0, MIN(manual, n_rows ? rows[n_rows - 1].y : 0));
                    manual_until = current_tick + FOLLOW_AFTER;
                }
                break;
            case ACTION_WPS_CYCLE_MODE: /* centre: on to scrubbing */
            case ACTION_WPS_HOTKEY:
                to_scrub = true;
                done = true;
                break;
            case ACTION_WPS_BROWSE:
            case ACTION_WPS_MENU:
            case ACTION_STD_CANCEL:
                done = true;
                break;
            case ACTION_WPS_PLAY:
                if (audio_status() & AUDIO_STATUS_PAUSE)
                    audio_resume();
                else
                    audio_pause();
                break;
            case ACTION_WPS_SKIPNEXT:
                audio_next();
                break;
            case ACTION_WPS_SKIPPREV:
                audio_prev();
                break;
            default:
                if (default_event_handler(action) == SYS_USB_CONNECTED)
                    done = true;
                break;
        }
    }
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, true);
    return to_scrub;
}
