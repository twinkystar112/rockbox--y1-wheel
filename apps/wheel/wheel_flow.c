/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - Album Flow and disc stories.
 *
 * Album Flow: covers stand side by side in 3D; the one in the middle faces
 * you and the others turn away towards the edges, each with a reflection.
 * Turning the wheel slides them past. Used for every album in the database
 * (main menu) and as a view for playlists.
 *
 * Disc stories: one card per song. The cover is printed on a disc that
 * turns while the song plays, over a blurred copy of the cover, with a row
 * of progress segments at the top.
 *
 * Everything is drawn straight into the 16-bit frame buffer with integer
 * maths; covers are decoded once and kept in a small cache.
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
#include "file.h"
#include "dir.h"
#include "font.h"
#include "lcd.h"
#include "misc.h"
#include "splash.h"
#include "settings.h"
#include "playlist.h"
#include "audio.h"
#include "metadata.h"
#include "viewport.h"
#include "skin_engine/skin_engine.h"
#include "screens.h"
#include "root_menu.h"
#include "wps.h"
#include "string-extra.h"
#ifdef HAVE_ALBUMART
#include "albumart.h"
#include "bmp.h"
#endif
#ifdef HAVE_JPEG
#include "jpeg_load.h"
#endif
#ifdef HAVE_TAGCACHE
#include "tagcache.h"
#endif
#include "wheel_typo.h"
#include "wheel_library.h"
#include "wheel_listview.h"
#include "wheel_screens.h"
#include "wheel_flow.h"
#include "wheel_palette.h"

#if defined(HAVE_ALBUMART) && defined(HAVE_LCD_COLOR) && (LCD_DEPTH == 16) && \
    (LCD_PIXELFORMAT == RGB565) && (CONFIG_PLATFORM & PLATFORM_HOSTED)
#define FLOW_OK 1
#endif

/* ---------------------- slow spots, for real players ---------------------- */
/* Anything that takes longer than ~150 ms goes to .rockbox/wheel-perf.log
 * (kept under 64 KB), so tests on the Y1 can say what was slow. */
#define PERF_LOG ROCKBOX_DIR "/wheel-perf.log"
void wheel_perf(const char *what, long ticks)
{
    if (ticks < HZ * 15 / 100)   /* only what the user could feel */
        return;
    int fd = open(PERF_LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    if (ffilesize(fd) > 64 * 1024)
    {
        close(fd);
        fd = open(PERF_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0)
            return;
    }
    fdprintf(fd, "%6ld s  %-34s %6ld ms\n", current_tick / HZ, what, ticks * 1000 / HZ);
    close(fd);
}

bool wheel_screen_on(void)
{
#if defined(HAVE_LCD_ENABLE) || defined(HAVE_LCD_SLEEP)
    return lcd_active();
#else
    return true;
#endif
}

/* Rockbox threads take turns on one processor: while a song the user just
 * picked is loading, the views hold back their own heavy work (covers) so
 * the decoder gets the time. */
static long play_t0;
void wheel_flow_play_started(void)
{
    play_t0 = current_tick ? current_tick : 1;
}
static bool audio_starting(void)
{
    if (!play_t0)
        return false;
    long dt = current_tick - play_t0;
    struct mp3entry *id3 = audio_current_track();
    if ((audio_status() & (AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE)) == AUDIO_STATUS_PLAY &&
        id3 && id3->elapsed > 0)
    {
        play_t0 = 0;
        wheel_perf("play: first sound after", dt);
        return false;
    }
    if (dt > 8 * HZ)
    {
        play_t0 = 0;
        wheel_perf("play: still silent after", dt);
        return false;
    }
    return true;
}

#ifdef FLOW_OK

#define W LCD_WIDTH
#define H LCD_HEIGHT
#define SC(v) ((v) * LCD_HEIGHT / 360)   /* laid out for 480x360 */

/* =============================== maths =============================== */

static const short sin_tab[91] = {
    0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126,
    3406, 3686, 3964, 4240, 4516, 4790, 5063, 5334, 5604, 5872, 6138, 6402,
    6664, 6924, 7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397,
    9630, 9860, 10087, 10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982,
    12176, 12365, 12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741, 13894, 14044,
    14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191, 15296, 15396, 15491,
    15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083, 16135, 16182, 16225, 16262,
    16294, 16322, 16344, 16362, 16374, 16382, 16384,
};

/* sine of an angle in 1/256 degree, Q14 */
static int isin(long a)
{
    a %= 360 * 256;
    if (a < 0)
        a += 360 * 256;
    int sign = 1;
    if (a >= 180 * 256)
    {
        a -= 180 * 256;
        sign = -1;
    }
    if (a > 90 * 256)
        a = 180 * 256 - a;
    int i = a >> 8, f = a & 255;
    int v = sin_tab[i];
    if (i < 90)
        v += ((sin_tab[i + 1] - v) * f) >> 8;
    return sign * v;
}
static int icos(long a) { return isin(a + 90 * 256); }

static uint32_t isqrt32(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b)
    {
        if (x >= r + b)
        {
            x -= r + b;
            r = (r >> 1) + b;
        }
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

/* RGB565 mix: a = 0..32 parts of fg */
static inline fb_data blend(fb_data fg, fb_data bg, int a)
{
    uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81F;
    uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81F;
    uint32_t r = (b + (((f - b) * (uint32_t)a) >> 5)) & 0x07E0F81F;
    return (fb_data)(r | (r >> 16));
}

/* light theme: Album Flow follows it (see flow_pick_mode) */
static bool flow_light;
static fb_data ui_bg, ui_fg;

static inline fb_data rgb(int r, int g, int b)
{
    r = MAX(0, MIN(255, r)); g = MAX(0, MIN(255, g)); b = MAX(0, MIN(255, b));
    return (fb_data)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
#define R8(c) ((((c) >> 11) & 31) * 255 / 31)
#define G8(c) ((((c) >> 5) & 63) * 255 / 63)
#define B8(c) (((c) & 31) * 255 / 31)

static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

/* a 4x4 ordered dither keeps the soft gradients from banding */
static const signed char bayer[4][4] = {
    { -6, 2, -4, 4 }, { 6, -2, 8, 0 }, { -3, 5, -5, 3 }, { 9, 1, 7, -1 },
};

/* ============================ frame buffer ============================ */

static fb_data *fb;
static int fb_stride;
static struct viewport fvp;
static struct viewport *fvp_last;

static void frame_begin(void)
{
    struct screen *d = &screens[SCREEN_MAIN];
    viewport_set_defaults(&fvp, SCREEN_MAIN);
    fvp.x = fvp.y = 0;
    fvp.width = W;
    fvp.height = H;
    fvp_last = d->set_viewport(&fvp);
    fb = FBADDR(0, 0);
    fb_stride = FBADDR(0, 1) - fb;
}

static void frame_end(void)
{
    struct screen *d = &screens[SCREEN_MAIN];
    d->update();
    d->set_viewport(fvp_last);
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

/* ================================ text ================================ */

static int f_big = -1, f_mid = -1, f_small = -1;

static void load_fonts(void)
{
    f_big = typo_font(TU_DISPLAY_NP);
    f_mid = typo_font(TU_SECONDARY);
    f_small = typo_font(TU_META);
}
static int fnt(int f) { return f >= 0 ? f : FONT_UI; }
static int font_h(int f)
{
    struct font *pf = font_get(fnt(f));
    return pf ? pf->height : 16;
}

/* draw s in font f, cut with an ellipsis to max_w; align: 0 left,
 * 1 centre (x is the middle), 2 right (x is the right edge) */
static int text_at(int f, unsigned col, int x, int y, int max_w, int align, const char *s)
{
    struct screen *d = &screens[SCREEN_MAIN];
    char buf[200];
    int w, h;
    fvp.font = fnt(f);
    strlcpy(buf, s, sizeof(buf));
    d->getstringsize(buf, &w, &h);
    if (w > max_w)
    {
        size_t len = MIN(strlen(buf), sizeof(buf) - 4 + 1);  /* room for the "…" */
        while (len > 0)
        {
            do { len--; } while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
            strcpy(&buf[len], "…");
            d->getstringsize(buf, &w, &h);
            if (w <= max_w)
                break;
        }
    }
    if (align == 1)
        x -= w / 2;
    else if (align == 2)
        x -= w;
    d->set_drawmode(DRMODE_FG);
    d->set_foreground(col);
    d->putsxy(x, y, buf);
    return w;
}

/* =========================== cover pictures =========================== */

#define CS (SC(176) & ~1)        /* carousel covers */
#define DS (SC(216) & ~1)        /* the disc */
#define SLOTS 24

static long dec_buf[(DS * DS * sizeof(fb_data) + 80 * 1024) / sizeof(long)];
static struct mp3entry art_id3;

/* the picture for a track: key gets the image file, or the track itself
 * when the picture is inside it. false if it has none */
static bool art_find(const char *track, char *key, size_t len)
{
    struct dim dim = { .width = CS, .height = CS };
    int fd = open(track, O_RDONLY);
    if (fd < 0)
        return false;
    bool ok = get_metadata(&art_id3, fd, track);
    close(fd);
    if (!ok)
        return false;
    if (find_albumart(&art_id3, key, len, &dim))
        return true;
#ifdef HAVE_JPEG
    if (art_id3.has_embedded_albumart && art_id3.albumart.type == AA_TYPE_JPG)
    {
        strlcpy(key, track, len);
        return true;
    }
#endif
    return false;
}

/* darkened average colour, for backgrounds behind white text */
static unsigned tint_of(const fb_data *px, int size)
{
    long r = 0, g = 0, b = 0, n = 0;
    int step = MAX(1, size / 24);
    for (int y = 0; y < size; y += step)
        for (int x = 0; x < size; x += step)
        {
            fb_data c = px[y * size + x];
            r += R8(c); g += G8(c); b += B8(c);
            n++;
        }
    r /= n; g /= n; b /= n;
    long mx = MAX(r, MAX(g, b));
    if (mx > 120)
    {
        r = r * 120 / mx; g = g * 120 / mx; b = b * 120 / mx;
    }
    if (mx < 28)
        r = g = b = 30;
    return rgb(r, g, b);
}

/* decode right after art_find() into out (size x size, centred on black) */
static bool art_decode(const char *track, const char *key, fb_data *out, int size,
                       unsigned *tint)
{
    struct bitmap bm;
    memset(&bm, 0, sizeof(bm));
    bm.width = size;
    bm.height = size;
    bm.data = (unsigned char *)dec_buf;
    int fmt = FORMAT_NATIVE | FORMAT_RESIZE | FORMAT_KEEP_ASPECT | FORMAT_DITHER;
    int got = -1;
    if (strcmp(key, track))
    {
        const char *ext = strrchr(key, '.');
#ifdef HAVE_JPEG
        if (ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg")))
            got = read_jpeg_file(key, &bm, sizeof(dec_buf), fmt, NULL);
        else
#endif
            got = read_bmp_file(key, &bm, sizeof(dec_buf), fmt, NULL);
        (void)ext;
    }
#ifdef HAVE_JPEG
    else
        got = clip_jpeg_file(track, art_id3.albumart.pos, art_id3.albumart.size,
                             &bm, sizeof(dec_buf), fmt, NULL);
#endif
    if (got <= 0 || bm.width <= 0 || bm.height <= 0 || bm.width > size || bm.height > size)
        return false;
    const fb_data *src = (const fb_data *)dec_buf;
    int ox = (size - bm.width) / 2, oy = (size - bm.height) / 2;
    if (bm.width < size || bm.height < size)
        memset(out, 0, size * size * sizeof(fb_data));
    for (int y = 0; y < bm.height; y++)
        memcpy(&out[(oy + y) * size + ox], &src[y * bm.width], bm.width * sizeof(fb_data));
    *tint = tint_of(out, size);
    return true;
}

/* no picture: a record sleeve in colours of its own, so covers still differ */
static void stand_in(fb_data *out, int size, uint32_t seed, unsigned *tint)
{
    static const unsigned char hues[8][3] = {
        { 92, 64, 140 }, { 40, 96, 140 }, { 30, 120, 110 }, { 150, 90, 40 },
        { 140, 50, 70 }, { 70, 110, 60 }, { 60, 70, 110 }, { 120, 100, 50 },
    };
    const unsigned char *hc = hues[seed % 8];
    int c = size / 2;
    int r_rec = size * 40 / 100, r_lab = size * 14 / 100, r_hole = MAX(2, size / 60);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
        {
            /* sleeve: diagonal light */
            int k = 60 + 50 * (size - x + size - y) / (2 * size);
            int r = hc[0] * k / 100, g = hc[1] * k / 100, b = hc[2] * k / 100;
            int dx = x - c, dy = y - c;
            int d = isqrt32(dx * dx + dy * dy);
            if (d < r_hole)
                r = g = b = 20;
            else if (d < r_lab)
            {
                r = MIN(255, hc[0] + 60); g = MIN(255, hc[1] + 60); b = MIN(255, hc[2] + 60);
            }
            else if (d < r_rec)
            {   /* grooves */
                int gr = (d & 3) == 0 ? 34 : 18;
                r = g = b = gr;
                if (dx > dy && dx - dy < size / 5)
                    r = g = b = gr + 22;   /* a little shine */
            }
            out[y * size + x] = rgb(r + bayer[y & 3][x & 3], g + bayer[y & 3][x & 3],
                                    b + bayer[y & 3][x & 3]);
        }
    *tint = rgb(hc[0] * 2 / 3, hc[1] * 2 / 3, hc[2] * 2 / 3);
}

/* -------------------------------- the cache -------------------------------- */

struct slot {
    int item;               /* item of the current source, when gen matches */
    unsigned gen;
    long used;              /* frame it was last drawn in */
    bool art;               /* false: the stand-in */
    unsigned tint;
    char path[MAX_PATH];    /* track it was made from */
    char key[MAX_PATH];     /* picture file, "" for none */
    fb_data px[CS * CS];
};
static struct slot slots[SLOTS];
static unsigned cur_gen = 1;
static long frame_no = 1;
static fb_data blank[CS * CS];
static bool blank_ok;

static struct slot *slot_find(int i)
{
    for (int k = 0; k < SLOTS; k++)
        if (slots[k].gen == cur_gen && slots[k].item == i)
        {
            slots[k].used = frame_no;
            return &slots[k];
        }
    return NULL;
}

static struct slot *slot_victim(void)
{
    struct slot *best = NULL;
    for (int k = 0; k < SLOTS; k++)
    {
        if (!slots[k].gen)
            return &slots[k];
        if (slots[k].used == frame_no)
            continue;   /* on screen now */
        if (!best || slots[k].used < best->used)
            best = &slots[k];
    }
    return best;
}

/* -------------------------- covers kept on the card -------------------------- */
/* On the player a cover costs a lot: the song's tags are read to find the
 * picture, then the JPEG is decoded and scaled. Each cover made is kept as
 * raw pixels in .rockbox/wheel/thumbs, in at most 1024 files of ~62 KB that
 * are reused by hash (a newer song takes the place of an older one). */
#define THUMB_DIR    ROCKBOX_DIR "/wheel/thumbs"
#define THUMB_FILES  1024
#define THUMB_MAGIC  0x31485457u     /* "WTH1" */
struct thumb_head {
    uint32_t magic, key, size, tint, art, side;
};
static int thumb_dir = -1;           /* -1 unknown, 0 can't, 1 ready */

static uint32_t track_size(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    uint32_t n = (uint32_t)ffilesize(fd);
    close(fd);
    return n;
}

static void thumb_name(uint32_t h, char *buf, size_t len)
{
    snprintf(buf, len, THUMB_DIR "/%03x.thm", (unsigned)(h % THUMB_FILES));
}

/* 1: picture read, 0: known to have none, -1: not kept (or changed) */
static int thumb_read(uint32_t h, uint32_t size, fb_data *px, unsigned *tint)
{
    char name[MAX_PATH];
    thumb_name(h, name, sizeof(name));
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return -1;
    struct thumb_head hd;
    int r = -1;
    if (read(fd, &hd, sizeof(hd)) == (ssize_t)sizeof(hd) && hd.magic == THUMB_MAGIC &&
        hd.key == h && hd.size == size && hd.side == CS)
    {
        if (!hd.art)
            r = 0;
        else if (read(fd, px, CS * CS * sizeof(fb_data)) == (ssize_t)(CS * CS * sizeof(fb_data)))
        {
            *tint = hd.tint;
            r = 1;
        }
    }
    close(fd);
    return r;
}

static void thumb_write(uint32_t h, uint32_t size, const fb_data *px, unsigned tint, bool art)
{
    if (thumb_dir < 0)
    {
        if (!dir_exists(THUMB_DIR))
        {
            mkdir(ROCKBOX_DIR "/wheel");
            mkdir(THUMB_DIR);
        }
        thumb_dir = dir_exists(THUMB_DIR) ? 1 : 0;
    }
    if (thumb_dir <= 0)
        return;
    char name[MAX_PATH];
    thumb_name(h, name, sizeof(name));
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    struct thumb_head hd = { THUMB_MAGIC, h, size, tint, art ? 1 : 0, CS };
    bool ok = write(fd, &hd, sizeof(hd)) == (ssize_t)sizeof(hd);
    if (ok && art)
        ok = write(fd, px, CS * CS * sizeof(fb_data)) == (ssize_t)(CS * CS * sizeof(fb_data));
    close(fd);
    if (!ok)
        remove(name);
}

static struct slot *slot_load(struct wheel_flow_src *s, int i)
{
    char path[MAX_PATH], key[MAX_PATH];
    path[0] = key[0] = '\0';
    if (!s->track || !s->track(s, i, path, sizeof(path)))
        path[0] = '\0';
    /* made already (another list, or the queue moved) */
    for (int k = 0; path[0] && k < SLOTS; k++)
        if (slots[k].gen && !strcmp(slots[k].path, path) &&
            !(slots[k].gen == cur_gen && slots[k].used == frame_no))
        {
            slots[k].item = i;
            slots[k].gen = cur_gen;
            slots[k].used = frame_no;
            return &slots[k];
        }
    struct slot *v = slot_victim();
    if (!v)
        return NULL;
    v->gen = 0;
    v->art = false;
    uint32_t h = 0, size = 0;
    int kept = -1;
    if (path[0])
    {
        h = hash_str(path);
        size = track_size(path);
        if (size)
            kept = thumb_read(h, size, v->px, &v->tint);
    }
    if (kept == 1)
        v->art = true;          /* key stays "": no sharing with art_find keys */
    bool have = kept < 0 && path[0] && art_find(path, key, sizeof(key));
    if (have)
    {
        for (int k = 0; k < SLOTS; k++)
            if (&slots[k] != v && slots[k].gen && slots[k].art && !strcmp(slots[k].key, key))
            {   /* the same picture: one album's songs */
                memcpy(v->px, slots[k].px, sizeof(v->px));
                v->tint = slots[k].tint;
                v->art = true;
                break;
            }
        if (!v->art)
            v->art = art_decode(path, key, v->px, CS, &v->tint);
    }
    if (!v->art)
    {
        key[0] = '\0';
        stand_in(v->px, CS, path[0] ? hash_str(path) : (uint32_t)i, &v->tint);
    }
    if (kept < 0 && size)
        thumb_write(h, size, v->px, v->tint, v->art);
    strlcpy(v->path, path, sizeof(v->path));
    strlcpy(v->key, key, sizeof(v->key));
    v->item = i;
    v->gen = cur_gen;
    v->used = frame_no;
    return v;
}

/* ============================ Album Flow ============================ */

#define FLOW_TOP   SC(34)
#define PERSP      520          /* camera distance */
#define SIDE_X     158          /* first cover beside the middle one */
#define SIDE_Z     120          /* ...how far back it stands */
#define SIDE_STEP  26           /* then one every so many pixels */
#define SIDE_DEG   60           /* turned away by */
#define REFL_PCT   30
#define REFL_GAP   2

static int flow_cy;             /* horizon: middle of the covers */
static int refl_limit;          /* reflections stop above the text */

/* where item d (Q8 covers from the middle) stands */
static void layout(long d, long *xc, long *zc, long *deg)
{
    long a = labs(d);
    int sg = d < 0 ? -1 : 1;
    if (a < 256)
    {
        *xc = sg * SIDE_X * a;          /* Q8 pixels */
        *zc = SIDE_Z * a;
        *deg = sg * SIDE_DEG * a;       /* Q8 degrees */
    }
    else
    {
        *xc = sg * (SIDE_X * 256 + (a - 256) * SIDE_STEP);
        *zc = SIDE_Z * 256;
        *deg = sg * SIDE_DEG * 256;
    }
}

static void put_col_span(const fb_data *col, int sx, long ytop_q8, long h_q8, int shade)
{
    /* one screen column of a cover: rows from ytop for h, texture column col
     * (CS texels, stride CS) */
    if (h_q8 < 256)
        return;
    long yend_q8 = ytop_q8 + h_q8;
    int y0 = ytop_q8 >> 8, y1 = (yend_q8 - 1) >> 8;
    int32_t step = (int32_t)(((int64_t)CS << 24) / h_q8);   /* Q16 texels per row */
    int ys = MAX(0, y0);
    /* texel row of the first drawn row, Q16 */
    int32_t v = (int32_t)(((int64_t)(((long)ys << 8) + 128 - ytop_q8) * step) >> 8);
    for (int y = ys; y <= y1 && y < H; y++, v += step)
    {
        int tv = v >> 16;
        tv = MAX(0, MIN(CS - 1, tv));
        fb_data c = col[tv * CS];
        if (shade < 32)
            c = blend(c, flow_light ? ui_bg : 0, shade);
        fb_data *p = &fb[y * fb_stride + sx];
        /* soft top and bottom edges */
        int cov = 256;
        if (y == y0)
            cov = 256 - (ytop_q8 & 255);
        if (y == y1)
            cov = MIN(cov, (int)(yend_q8 - ((long)y1 << 8)));
        if (cov < 248)
            c = blend(c, *p, MAX(0, cov) >> 3);
        *p = c;
    }
    /* reflection, fading from REFL_ALPHA */
    int rh = (int)((h_q8 * REFL_PCT / 100) >> 8);
    int ry = y1 + 1 + REFL_GAP;
    v = step >> 1;
    for (int r = 0; r < rh; r++, v += step)
    {
        int y = ry + r;
        if (y >= refl_limit || y >= H)
            break;
        if (y < 0)
            continue;
        int tv = CS - 1 - (v >> 16);
        if (tv < 0)
            break;
        int f = (rh - r) * 32 / rh;               /* 32 .. 0 */
        int a = (12 * f * f) >> 10;               /* up to 12/32, fading fast */
        if (a <= 0)
            break;
        fb_data c = col[tv * CS];
        fb_data *p = &fb[y * fb_stride + sx];
        *p = blend(c, *p, a * shade >> 5);
    }
}

static void draw_cover(const fb_data *tex, long xc, long zc, long deg)
{
    const int hs = CS / 2;
    int c = icos(deg), s = -isin(deg);      /* inner edge nearer */
    int shade = 32 - (int)(labs(deg) * 9 / (SIDE_DEG * 256));
    /* a cover facing us: copy it as it is */
    if (deg == 0 && zc == 0)
    {
        int x0 = W / 2 + (int)(xc >> 8) - hs;
        for (int x = 0; x < CS; x++)
            if (x0 + x >= 0 && x0 + x < W)
                put_col_span(&tex[x], x0 + x, ((long)flow_cy << 8) - (hs << 8),
                             (long)CS << 8, 32);
        return;
    }
    /* the two vertical edges on screen */
    long ex[2];
    for (int k = 0; k < 2; k++)
    {
        long u = k ? hs : -hs;
        long X = xc + ((u * c * 256) >> 14);
        long Z = zc - ((u * s * 256) >> 14);
        ex[k] = (X * PERSP) / (PERSP * 256L + Z);
    }
    int x0 = (int)MIN(ex[0], ex[1]) - 1, x1 = (int)MAX(ex[0], ex[1]) + 1;
    x0 = MAX(x0, -W / 2);
    x1 = MIN(x1, W / 2 - 1);
    for (int sx = x0; sx <= x1; sx++)
    {
        long sxh = 2 * sx + 1;   /* half pixels: the middle of the column */
        int64_t num = (int64_t)sxh * (PERSP * 256L + zc) * 16384 -
                      (int64_t)2 * xc * PERSP * 16384;
        int64_t den = (int64_t)2 * c * PERSP + (int64_t)sxh * s;
        if (den == 0)
            continue;
        long u = (long)(num / den);           /* Q8, -hs..hs on the cover */
        if (u < -hs * 256L || u >= hs * 256L)
            continue;
        long z = zc - (long)(((int64_t)u * s) >> 14);
        long h_q8 = (long)(((int64_t)CS * PERSP * 65536) / (PERSP * 256L + z));
        int tu = (int)(u >> 8) + hs;
        tu = MAX(0, MIN(CS - 1, tu));
        put_col_span(&tex[tu], W / 2 + sx, ((long)flow_cy << 8) - h_q8 / 2, h_q8, shade);
    }
}

static unsigned bg_tint, bg_target;

/* Album Flow follows the rest of the interface: on a light theme the
 * backdrop is a pale wash of the cover's colour over the theme's
 * background and the words use the theme's text colour */
static fb_data ui_dim, ui_faint;
static fb_data rgb24(unsigned c)
{
    return rgb((c >> 16) & 255, (c >> 8) & 255, c & 255);
}
static void flow_pick_mode(void)
{
    /* the colours the Wheel skins are drawn in right now: the palette, or
     * the classic look in its light or dark form (Appearance) */
    const struct wheel_palette *p = wheel_palette_active();
    if (!p)
        p = wheel_palette_classic(global_settings.wheel_appearance == 1 ||
                                  (global_settings.wheel_appearance == 2 && wheel_is_night()));
    flow_light = p && !p->dark;
    ui_bg = rgb24(p ? p->c[PR_BG] : 0xffffff);
    ui_fg = rgb24(p ? p->c[PR_TEXT] : 0x141416);
    ui_dim = rgb24(p ? p->c[PR_TEXT2] : 0x6e6e73);
    ui_faint = rgb24(p ? p->c[PR_TEXT3] : 0x8e8e93);
}

static void draw_backdrop(void)
{
    int r0 = R8(bg_tint), g0 = G8(bg_tint), b0 = B8(bg_tint);
    /* what the gradient fades into */
    int rb = 10, gb = 10, bb = 12;
    if (flow_light)
    {
        rb = R8(ui_bg); gb = G8(ui_bg); bb = B8(ui_bg);
        /* a pastel of the cover: mostly the background, some of the colour */
        r0 = (r0 * 45 + rb * 55) / 100;
        g0 = (g0 * 45 + gb * 55) / 100;
        b0 = (b0 * 45 + bb * 55) / 100;
    }
    for (int y = 0; y < H; y++)
    {
        /* the cover's colour high up, the background under the covers */
        int k = y < flow_cy ? 100 - y * 45 / flow_cy : MAX(0, 55 - (y - flow_cy) * 70 / (H - flow_cy));
        int r = rb + (r0 - rb) * k / 100, g = gb + (g0 - gb) * k / 100, b = bb + (b0 - bb) * k / 100;
        fb_data *line = &fb[y * fb_stride];
        fb_data c[4];
        for (int i = 0; i < 4; i++)
            c[i] = rgb(r + bayer[y & 3][i] / 2, g + bayer[y & 3][i] / 2, b + bayer[y & 3][i] / 2);
        for (int x = 0; x < W; x++)
            line[x] = c[x & 3];
    }
}

static unsigned mix_tint(unsigned a, unsigned b, int pct)
{
    return rgb((R8(a) * pct + R8(b) * (100 - pct)) / 100,
               (G8(a) * pct + G8(b) * (100 - pct)) / 100,
               (B8(a) * pct + B8(b) * (100 - pct)) / 100);
}

static void flow_draw(struct wheel_flow_src *s, long pos, int sel, int *missing)
{
    struct screen *d = &screens[SCREEN_MAIN];
    frame_begin();
    frame_no++;
    if (!blank_ok)
    {
        unsigned t;
        stand_in(blank, CS, 6, &t);
        blank_ok = true;
    }
    /* text block from the bottom up */
    int hb = font_h(f_big), hm = font_h(f_mid), hs_ = font_h(f_small);
    int y_meta = H - SC(8) - hs_;
    int y_sub = y_meta - hm - 1;
    int y_title = y_sub - hb + 2;
    flow_cy = FLOW_TOP + CS / 2;
    refl_limit = y_title - 2;

    /* the background follows the middle cover's colour */
    struct slot *mid = slot_find(sel);
    bg_target = mid ? mid->tint : rgb(36, 38, 46);
    unsigned nt = mix_tint(bg_target, bg_tint, 25);
    bg_tint = nt == bg_tint ? bg_target : nt;   /* rounding would never get there */
    draw_backdrop();

    /* far covers first */
    int first = MAX(0, (int)(pos >> 8) - 7), last = MIN(s->n - 1, (int)(pos >> 8) + 8);
    int order[16], m = 0;
    for (int i = first; i <= last && m < 16; i++)
        order[m++] = i;
    for (int a = 1; a < m; a++)
        for (int b = a; b > 0; b--)
        {
            long da = labs(order[b] * 256L - pos), db = labs(order[b - 1] * 256L - pos);
            if (da <= db)
                break;
            int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t;
        }
    *missing = -1;
    long best = 0;
    for (int k = 0; k < m; k++)
    {
        int i = order[k];
        long dq = i * 256L - pos;
        long xc, zc, deg;
        layout(dq, &xc, &zc, &deg);
        struct slot *sl = slot_find(i);
        if (!sl && (*missing < 0 || labs(dq) < best))
        {
            *missing = i;
            best = labs(dq);
        }
        draw_cover(sl ? sl->px : blank, xc, zc, deg);
    }

    /* words */
    unsigned white = LCD_RGBPACK(255, 255, 255);
    unsigned dim = LCD_RGBPACK(190, 190, 196), faint = LCD_RGBPACK(140, 140, 150);
    if (flow_light)
    {
        white = ui_fg;
        dim = ui_dim;
        faint = ui_faint;
    }
    if (s->heading)
        text_at(f_small, faint, SC(14), SC(8), W / 2, 0, s->heading);
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%d / %d", sel + 1, s->n);
    text_at(f_small, faint, W - SC(14), SC(8), W / 3, 2, cnt);
    if (s->n > 0)
    {
        struct wheel_flow_text t;
        memset(&t, 0, sizeof(t));
        s->text(s, sel, &t);
        text_at(f_big, white, W / 2, y_title, W - SC(28), 1, t.title);
        text_at(f_mid, dim, W / 2, y_sub, W - SC(40), 1, t.sub);
        text_at(f_small, faint, W / 2, y_meta, W - SC(40), 1, t.meta);
    }
    (void)d;
    frame_end();
}

static void clamp_sel(struct wheel_flow_src *s)
{
    if (s->sel >= s->n) s->sel = s->n - 1;
    if (s->sel < 0) s->sel = 0;
}

int wheel_flow_show(struct wheel_flow_src *s)
{
    flow_pick_mode();
    load_fonts();
    cur_gen++;
    clamp_sel(s);
    long pos = s->sel * 256L;
    int ret = 0, n_seen = s->n;
    bool done = false;
    bg_tint = rgb(36, 38, 46);
    theme_off();
    while (!done && s->n > 0)
    {
        int missing = -1;
        long t0 = current_tick;
        bool screen = wheel_screen_on();
        if (screen)
        {
            flow_draw(s, pos, s->sel, &missing);
            wheel_perf("flow: frame", current_tick - t0);
        }
        long target = s->sel * 256L, diff = target - pos;
        bool moving = diff != 0;
        if (moving)
        {
            long step = diff / 4;
            if (labs(step) < 20)
                step = diff > 0 ? MIN(20, diff) : MAX(-20, diff);
            pos += step;
        }
        /* one cover per frame; none while flying past or while the song
         * just picked is loading */
        bool hold = missing >= 0 && audio_starting();
        if (missing >= 0 && !hold && labs(diff) < 3 * 256)
        {
            long t1 = current_tick;
            slot_load(s, missing);
            wheel_perf("flow: cover", current_tick - t1);
        }
        bool busy = moving || (missing >= 0 && !hold) || bg_tint != bg_target;
        /* rest at least as long as the frame took: on the player the
         * decoder runs in the same turns as this loop */
        long work = current_tick - t0;
        int tmo = busy ? (int)MAX(1, MIN(work, HZ / 5)) : (hold ? HZ / 10 : HZ / 2);
        if (!screen)
        {
            pos = s->sel * 256L;   /* no animation to finish */
            tmo = HZ;
        }
        int a = get_action(CONTEXT_STD, tmo);
        switch (a)
        {
            case ACTION_STD_NEXT:
            case ACTION_STD_NEXTREPEAT:
                if (s->sel < s->n - 1) s->sel++;
                break;
            case ACTION_STD_PREV:
            case ACTION_STD_PREVREPEAT:
                if (s->sel > 0) s->sel--;
                break;
            case ACTION_STD_OK:
                pos = s->sel * 256L;
                ret = s->ok ? s->ok(s, s->sel, false) : 0;
                load_fonts();
                if (ret == WHEEL_FLOW_CHANGED)
                    ret = 0, cur_gen++;
                done = ret != 0;
                break;
            case ACTION_STD_CONTEXT:
                pos = s->sel * 256L;
                ret = s->options ? s->options(s, s->sel) : 0;
                load_fonts();
                if (ret == WHEEL_FLOW_CHANGED)
                    ret = 0, cur_gen++;
                done = ret != 0;
                clamp_sel(s);
                pos = s->sel * 256L;
                break;
            case ACTION_STD_CANCEL:
                done = true;
                break;
            case ACTION_STD_MENU:
                ret = GO_TO_ROOT;
                done = true;
                break;
            case ACTION_NONE:
                if (s->playing)
                    s->playing(s);   /* lets a live queue grow */
                break;
            default:
                if (default_event_handler(a) == SYS_USB_CONNECTED)
                {
                    ret = GO_TO_ROOT;
                    done = true;
                }
                break;
        }
        if (s->n != n_seen)
        {
            n_seen = s->n;
            cur_gen++;
            clamp_sel(s);
            pos = s->sel * 256L;
        }
    }
    theme_on();
    return ret;
}

/* ============================ disc stories ============================ */

#define DISC_R   (DS / 2)
#define SPIN     (40L * 256)            /* degrees a second while playing */

static fb_data disc_tex[DS * DS];
static fb_data disc_bg[W * H];
static unsigned disc_tint;
static int disc_cx, disc_cy;

/* blurred, darkened cover filling the screen, with the disc's shadow */
static void make_backdrop(bool art)
{
    enum { GW = 24, GH = 18 };
    static int grid[GH + 1][GW + 1][3];
    /* the cover is square, the screen is wider: use its middle band */
    int band = DS * H / W, top = (DS - band) / 2;
    for (int gy = 0; gy <= GH; gy++)
        for (int gx = 0; gx <= GW; gx++)
        {
            int cx = MIN(DS - 1, gx * (DS - 1) / GW), cy = top + MIN(band - 1, gy * (band - 1) / GH);
            long r = 0, g = 0, b = 0, n = 0;
            for (int y = MAX(0, cy - 6); y <= MIN(DS - 1, cy + 6); y += 3)
                for (int x = MAX(0, cx - 6); x <= MIN(DS - 1, cx + 6); x += 3)
                {
                    fb_data c = disc_tex[y * DS + x];
                    r += R8(c); g += G8(c); b += B8(c); n++;
                }
            grid[gy][gx][0] = r / n; grid[gy][gx][1] = g / n; grid[gy][gx][2] = b / n;
        }
    if (!art)
        for (int gy = 0; gy <= GH; gy++)
            for (int gx = 0; gx <= GW; gx++)
            {   /* a plain wash in the sleeve's colour */
                grid[gy][gx][0] = R8(disc_tint) + 30; grid[gy][gx][1] = G8(disc_tint) + 30;
                grid[gy][gx][2] = B8(disc_tint) + 30;
            }
    /* two soft passes */
    for (int pass = 0; pass < 2; pass++)
        for (int ch = 0; ch < 3; ch++)
        {
            static int tmp[GH + 1][GW + 1];
            for (int gy = 0; gy <= GH; gy++)
                for (int gx = 0; gx <= GW; gx++)
                {
                    int sum = 0, n = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                        {
                            int yy = gy + dy, xx = gx + dx;
                            if (yy < 0 || yy > GH || xx < 0 || xx > GW) continue;
                            sum += grid[yy][xx][ch]; n++;
                        }
                    tmp[gy][gx] = sum / n;
                }
            for (int gy = 0; gy <= GH; gy++)
                for (int gx = 0; gx <= GW; gx++)
                    grid[gy][gx][ch] = tmp[gy][gx];
        }
    /* stretch it over the screen, darker at the top and bottom for the words */
    for (int y = 0; y < H; y++)
    {
        int fy = y * GH * 256 / (H - 1), gy = MIN(GH - 1, fy >> 8), wy = fy - gy * 256;
        int dark = 52 + 14 * (y < H / 2 ? y : H - 1 - y) / (H / 2);   /* % */
        for (int x = 0; x < W; x++)
        {
            int fx = x * GW * 256 / (W - 1), gx = MIN(GW - 1, fx >> 8), wx = fx - gx * 256;
            int ch[3];
            for (int k = 0; k < 3; k++)
            {
                int a = grid[gy][gx][k] * (256 - wx) + grid[gy][gx + 1][k] * wx;
                int b = grid[gy + 1][gx][k] * (256 - wx) + grid[gy + 1][gx + 1][k] * wx;
                ch[k] = ((a >> 8) * (256 - wy) + (b >> 8) * wy) >> 8;
                ch[k] = ch[k] * dark / 100 + bayer[y & 3][x & 3];
            }
            disc_bg[y * W + x] = rgb(ch[0], ch[1], ch[2]);
        }
    }
    /* the disc's shadow, a little below it */
    int R = DISC_R, sy = disc_cy + SC(6);
    for (int y = MAX(0, sy - R - 16); y < MIN(H, sy + R + 16); y++)
        for (int x = MAX(0, disc_cx - R - 16); x < MIN(W, disc_cx + R + 16); x++)
        {
            int dx = x - disc_cx, dy = y - sy;
            int d = isqrt32(dx * dx + dy * dy) - R + 4;
            if (d < 0 || d >= 18) continue;
            int a = 12 * (18 - d) / 18;
            disc_bg[y * W + x] = blend(0, disc_bg[y * W + x], a);
        }
}

static void disc_prepare(struct wheel_flow_src *s, int i)
{
    char path[MAX_PATH], key[MAX_PATH];
    path[0] = key[0] = '\0';
    if (!s->track || !s->track(s, i, path, sizeof(path)))
        path[0] = '\0';
    bool art = path[0] && art_find(path, key, sizeof(key)) &&
               art_decode(path, key, disc_tex, DS, &disc_tint);
    if (!art)
        stand_in(disc_tex, DS, path[0] ? hash_str(path) : (uint32_t)i, &disc_tint);
    make_backdrop(art);
}

static void draw_disc(int cx, int cy, long ang)
{
    const int R = DISC_R, R2 = R * R;
    const int hole = R * 11 / 100, hub = R * 30 / 100, ring = R * 33 / 100;
    int cs = icos(ang), sn = isin(ang);
    /* texture steps per screen pixel, Q16 */
    long du_x = ((long)cs * DS << 2) / (2 * R), dv_x = -((long)sn * DS << 2) / (2 * R);
    long du_y = ((long)sn * DS << 2) / (2 * R), dv_y = ((long)cs * DS << 2) / (2 * R);
    for (int dy = -R; dy <= R; dy++)
    {
        int y = cy + dy;
        if (y < 0 || y >= H)
            continue;
        int span = isqrt32((uint32_t)MAX(0, R2 - dy * dy)) + 1;
        long u = ((long)DS << 15) - span * du_x + dy * du_y;
        long v = ((long)DS << 15) - span * dv_x + dy * dv_y;
        fb_data *line = &fb[y * fb_stride];
        const fb_data *under = &disc_bg[y * W];
        for (int dx = -span; dx <= span; dx++, u += du_x, v += dv_x)
        {
            int x = cx + dx;
            if (x < 0 || x >= W)
                continue;
            int r2 = dx * dx + dy * dy;
            /* outside the rim, with a soft edge */
            int cov = ((R2 - r2) * 128 / R) + 128;
            if (cov <= 0)
                continue;
            int hcov = ((r2 - hole * hole) * 128 / MAX(1, hole)) + 128;
            if (hcov <= 0)
                continue;   /* the hole shows what is behind */
            /* smooth sampling: the turning picture stays clean */
            int tu = (int)(u >> 16), tv = (int)(v >> 16);
            int fu = (u >> 11) & 31, fv = (v >> 11) & 31;
            tu = MAX(0, MIN(DS - 2, tu)); tv = MAX(0, MIN(DS - 2, tv));
            const fb_data *t0 = &disc_tex[tv * DS + tu];
            fb_data c = blend(blend(t0[DS + 1], t0[DS], fu), blend(t0[1], t0[0], fu), fv);
            if (r2 < hub * hub)
            {   /* clear plastic round the hole */
                c = blend(0xFFFF, under[x], 7);
            }
            else if (r2 < ring * ring)
                c = blend(0xFFFF, blend(c, under[x], 10), 6);
            else
            {
                /* light falling on it from the top left; it stays put */
                int band = dx - dy;
                int w = R / 3;
                if (band > -w && band < w)
                    c = blend(0xFFFF, c, 4 - abs(band) * 4 / w);
                if (r2 > (R - 3) * (R - 3))
                    c = blend(0xFFFF, c, 7);   /* the edge catches light */
            }
            if (cov < 256)
                c = blend(c, under[x], cov >> 3);
            if (hcov < 256)
                c = blend(c, under[x], hcov >> 3);
            line[x] = c;
        }
    }
}

static void fmt_time(char *buf, size_t len, unsigned long ms)
{
    unsigned long s = ms / 1000;
    if (s >= 3600)
        snprintf(buf, len, "%lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
    else
        snprintf(buf, len, "%lu:%02lu", s / 60, s % 60);
}

static void segments(int n, int sel, int pct)
{
    /* stories show every card; here a window of them */
    const int MAXSEG = 24, gap = 3, y0 = SC(8), hh = 3;
    int first = 0, m = n;
    if (n > MAXSEG)
    {
        m = MAXSEG;
        first = MAX(0, MIN(n - MAXSEG, sel - MAXSEG / 2));
    }
    int x0 = SC(10), avail = W - 2 * x0;
    int sw = (avail - (m - 1) * gap) / MAX(1, m);
    if (sw < 2)
        return;
    for (int k = 0; k < m; k++)
    {
        int i = first + k, x = x0 + k * (sw + gap);
        int fill = i < sel ? sw : i > sel ? 0 : sw * pct / 100;
        for (int y = y0; y < y0 + hh; y++)
        {
            fb_data *line = &fb[y * fb_stride];
            for (int c = 0; c < sw; c++)
                line[x + c] = blend(0xFFFF, line[x + c], c < fill ? 29 : 10);
        }
    }
}

static void disc_draw(struct wheel_flow_src *s, int sel, long ang, int slide, bool card_playing)
{
    frame_begin();
    for (int y = 0; y < H; y++)
        memcpy(&fb[y * fb_stride], &disc_bg[y * W], W * sizeof(fb_data));
    draw_disc(disc_cx + slide, disc_cy, ang);
    if (card_playing && (audio_status() & AUDIO_STATUS_PAUSE))
    {   /* paused: two bars over the hole */
        int bh = DISC_R * 22 / 100, bw = MAX(3, bh / 4), gap = bw;
        for (int y = disc_cy - bh / 2; y < disc_cy + bh / 2; y++)
            for (int x = 0; x < bw; x++)
            {
                int xl = disc_cx + slide - gap / 2 - bw + x, xr = disc_cx + slide + gap / 2 + x;
                fb[y * fb_stride + xl] = blend(0xFFFF, fb[y * fb_stride + xl], 28);
                fb[y * fb_stride + xr] = blend(0xFFFF, fb[y * fb_stride + xr], 28);
            }
    }

    int pct = 0;
    unsigned long elapsed = 0, length = 0;
    struct mp3entry *id3 = audio_current_track();
    if (card_playing && id3 && id3->length)
    {
        elapsed = id3->elapsed;
        length = id3->length;
        pct = (int)MIN(100, elapsed * 100 / length);
    }
    segments(s->n, sel, pct);

    unsigned white = LCD_RGBPACK(255, 255, 255), dim = LCD_RGBPACK(215, 215, 222);
    int hs_ = font_h(f_small);
    char right[56], cnt[24];
    snprintf(cnt, sizeof(cnt), "%d / %d", sel + 1, s->n);
    struct wheel_flow_text t;
    memset(&t, 0, sizeof(t));
    s->text(s, sel, &t);
    if (length)
    {
        char a[24], b[24];
        fmt_time(a, sizeof(a), elapsed);
        fmt_time(b, sizeof(b), length);
        snprintf(right, sizeof(right), "%s / %s", a, b);
    }
    else
        strlcpy(right, t.meta, sizeof(right));
    int y_head = SC(18);
    int rw = text_at(f_small, dim, W - SC(12), y_head, W / 3, 2, right);
    int lw = text_at(f_small, white, SC(12), y_head, W - SC(36) - rw - SC(70),
                     0, s->heading ? s->heading : "");
    text_at(f_small, dim, SC(12) + lw + SC(8), y_head, SC(70), 0, cnt);
    (void)hs_;

    int hb = font_h(f_big), hm = font_h(f_mid);
    int y_sub = H - SC(10) - hm, y_title = y_sub - hb + 2;
    text_at(f_big, white, W / 2 + slide / 2, y_title, W - SC(28), 1, t.title);
    text_at(f_mid, dim, W / 2 + slide / 2, y_sub, W - SC(40), 1, t.sub);
    frame_end();
}

int wheel_disc_show(struct wheel_flow_src *s)
{
    load_fonts();
    clamp_sel(s);
    int hb = font_h(f_big), hm = font_h(f_mid);
    int text_top = H - SC(10) - hm - hb + 2;
    disc_cx = W / 2;
    disc_cy = SC(36) + (text_top - SC(6) - SC(36)) / 2;
    int shown = -1, ret = 0, n_seen = s->n, slide = 0;
    long ang = 0, vel = 0, last = current_tick;
    int was_playing = -1;
    int pending = -1;              /* card whose song was picked, not playing yet */
    long pending_t = 0;
    bool done = false;
    theme_off();
    while (!done && s->n > 0)
    {
        long t0 = current_tick;
        if (shown != s->sel)
        {
            slide = shown < 0 ? 0 : (s->sel > shown ? SC(90) : -SC(90));
            shown = s->sel;
            disc_prepare(s, shown);
            wheel_perf("disc: cover", current_tick - t0);
            ang = 0;
        }
        int playing = s->playing ? s->playing(s) : -1;
        if (s->n != n_seen)
        {
            n_seen = s->n;
            clamp_sel(s);
            continue;
        }
        /* like stories: when the song on the card ends, the next card */
        if (was_playing == shown && playing >= 0 && playing != shown && playing < s->n)
        {
            s->sel = playing;
            was_playing = playing;
            continue;
        }
        was_playing = playing;
        bool on_card = playing == shown;
        audio_starting();   /* notes how long the song took to start */
        if (pending >= 0 && (playing == pending || TIME_AFTER(current_tick, pending_t + 4 * HZ)))
            pending = -1;
        bool starting = pending >= 0 && pending == shown && !on_card;
        /* it spins as soon as the song is picked: on the player it can take
         * a moment before the sound starts */
        bool rolling = starting || (on_card &&
            (audio_status() & (AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE)) == AUDIO_STATUS_PLAY);
        long now = current_tick, dt = MIN(HZ / 4, now - last);
        last = now;
        long want = rolling ? SPIN : 0;
        vel += (want - vel) / 4;
        if (labs(want - vel) < 64)
            vel = want;
        ang += vel * dt / HZ;
        bool screen = wheel_screen_on();
        if (screen)
        {
            long td = current_tick;
            disc_draw(s, shown, ang, slide, on_card);
            wheel_perf("disc: frame", current_tick - td);
        }
        else
            slide = 0, vel = 0;
        if (slide)
        {
            slide = slide * 2 / 3;
            if (abs(slide) < 3)
                slide = 0;
        }
        int tmo = (slide || vel) ? HZ / 15 : (on_card ? HZ / 4 : HZ / 2);
        if (slide || vel)   /* rest at least as long as the frame took */
            tmo = MAX(tmo, (int)MIN(current_tick - t0, HZ / 5));
        if (!screen)
            tmo = HZ;
        int a = get_action(CONTEXT_STD, tmo);
        switch (a)
        {
            case ACTION_STD_NEXT:
            case ACTION_STD_NEXTREPEAT:
                if (s->sel < s->n - 1) s->sel++;
                break;
            case ACTION_STD_PREV:
            case ACTION_STD_PREVREPEAT:
                if (s->sel > 0) s->sel--;
                break;
            case ACTION_STD_OK:
                if (on_card)   /* as the Now Playing screen does it */
                    wps_do_playpause(false);
                else if (starting)
                    break;     /* still loading: a second press would start it over */
                else if (s->ok)
                {
                    pending = s->sel;
                    pending_t = current_tick;
                    ret = s->ok(s, s->sel, true);
                    load_fonts();
                    if (ret == WHEEL_FLOW_CHANGED)
                        ret = 0;
                    done = ret != 0;
                    was_playing = s->sel;   /* follow it from now on */
                }
                break;
            case ACTION_STD_CONTEXT:
                ret = s->options ? s->options(s, s->sel) : 0;
                load_fonts();
                if (ret == WHEEL_FLOW_CHANGED)
                    ret = 0;
                done = ret != 0;
                clamp_sel(s);
                shown = -1;   /* the card may have changed */
                break;
            case ACTION_STD_CANCEL:
                done = true;
                break;
            case ACTION_STD_MENU:
                ret = GO_TO_ROOT;
                done = true;
                break;
            case ACTION_NONE:
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
    return ret;
}

bool wheel_flow_supported(void) { return true; }

/* ========================== every album ========================== */
#ifdef HAVE_TAGCACHE

#define MAX_ALBUMS  4000
#define MAX_ATR     12000
#define POOL_SIZE   (240 * 1024)

struct alb {
    uint32_t key;
    int32_t  rep;           /* tagcache index of its first song */
    uint32_t name, artist;  /* offsets into pool */
    uint16_t rep_order;
    uint16_t count, year, commit;
};
static struct alb albs[MAX_ALBUMS];
static uint16_t alb_order[MAX_ALBUMS];
static int n_albs;
static char pool[POOL_SIZE];
static size_t pool_used;
static struct atr { int32_t idx; uint32_t len_ms; uint16_t alb; uint16_t order; } atr[MAX_ATR];
static int n_atr;
static int16_t ahash[8192];
static long albs_built = -1;
static int albs_sorted = -1;

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

static uint32_t pool_add(const char *s)
{
    size_t len = strlen(s) + 1;
    if (pool_used + len > POOL_SIZE)
        return 0;   /* offset 0 is always "" */
    memcpy(&pool[pool_used], s, len);
    pool_used += len;
    return pool_used - len;
}

static bool bad_tag(const char *s) { return !s[0] || !strcmp(s, UNTAGGED); }

static bool albums_build(void)
{
    if (albs_built >= 0 && TIME_BEFORE(current_tick, albs_built + HZ * 600))
        return n_albs > 0;
    if (!tagcache_get_stat()->ready)
        return false;
    struct tagcache_search tcs;
    static char buf[MAX_PATH];
    char al[128], ar[128];
    n_albs = n_atr = 0;
    pool[0] = '\0';
    pool_used = 1;
    memset(ahash, -1, sizeof(ahash));
    if (!tagcache_search(&tcs, tag_filename))
        return false;
    int seen = 0;
    while (n_atr < MAX_ATR && tagcache_get_next(&tcs, buf, sizeof(buf)))
    {
        if ((++seen & 127) == 0)
            yield();
        bool folder = false;
        if (!tagcache_retrieve(&tcs, tcs.idx_id, tag_album, al, sizeof(al)) || bad_tag(al))
        {   /* no album tag: its folder stands for the album */
            char *slash = strrchr(buf, '/');
            if (slash)
                *slash = '\0';
            const char *f = strrchr(buf, '/');
            strlcpy(al, f ? f + 1 : buf, sizeof(al));
            folder = true;
        }
        if (!tagcache_retrieve(&tcs, tcs.idx_id, tag_albumartist, ar, sizeof(ar)) || bad_tag(ar))
            if (!tagcache_retrieve(&tcs, tcs.idx_id, tag_artist, ar, sizeof(ar)) || bad_tag(ar))
                ar[0] = '\0';
        uint32_t key = folder ? hash_ci(buf) * 7 + 1 : hash_ci(al) * 31 + hash_ci(ar);
        unsigned h = key & 8191;
        int a = -1;
        while (ahash[h] >= 0)
        {
            if (albs[ahash[h]].key == key) { a = ahash[h]; break; }
            h = (h + 1) & 8191;
        }
        int disc = tagcache_get_numeric(&tcs, tag_discnumber);
        int trk = tagcache_get_numeric(&tcs, tag_tracknumber);
        uint16_t ord = MAX(0, MIN(63, disc)) * 1024 + MAX(0, MIN(1023, trk));
        if (a < 0)
        {
            if (n_albs >= MAX_ALBUMS)
                continue;
            a = n_albs++;
            ahash[h] = a;
            struct alb *b = &albs[a];
            memset(b, 0, sizeof(*b));
            b->key = key;
            b->name = pool_add(al);
            b->artist = pool_add(ar);
            b->rep = tcs.idx_id;
            b->rep_order = ord;
        }
        struct alb *b = &albs[a];
        b->count++;
        int year = tagcache_get_numeric(&tcs, tag_year);
        if (year > b->year && year < 3000) b->year = year;
        int commit = tagcache_get_numeric(&tcs, tag_commitid);
        if (commit > b->commit) b->commit = MIN(65535, commit);
        if (ord < b->rep_order)
        {
            b->rep = tcs.idx_id;
            b->rep_order = ord;
        }
        struct atr *t = &atr[n_atr++];
        t->idx = tcs.idx_id;
        t->alb = a;
        t->order = ord;
        t->len_ms = tagcache_get_numeric(&tcs, tag_length);
    }
    tagcache_search_finish(&tcs);
    albs_built = current_tick;
    albs_sorted = -1;
    return n_albs > 0;
}

static int cmp_artist(const void *pa, const void *pb)
{
    const struct alb *a = &albs[*(const uint16_t *)pa], *b = &albs[*(const uint16_t *)pb];
    int r = strcasecmp(&pool[a->artist], &pool[b->artist]);
    if (r) return r;
    if (a->year != b->year) return a->year - b->year;
    return strcasecmp(&pool[a->name], &pool[b->name]);
}
static int cmp_title(const void *pa, const void *pb)
{
    const struct alb *a = &albs[*(const uint16_t *)pa], *b = &albs[*(const uint16_t *)pb];
    int r = strcasecmp(&pool[a->name], &pool[b->name]);
    return r ? r : strcasecmp(&pool[a->artist], &pool[b->artist]);
}
static int cmp_year(const void *pa, const void *pb)
{
    const struct alb *a = &albs[*(const uint16_t *)pa], *b = &albs[*(const uint16_t *)pb];
    if (a->year != b->year) return b->year - a->year;   /* newest first */
    return cmp_artist(pa, pb);
}
static int cmp_added(const void *pa, const void *pb)
{
    const struct alb *a = &albs[*(const uint16_t *)pa], *b = &albs[*(const uint16_t *)pb];
    if (a->commit != b->commit) return b->commit - a->commit;
    return cmp_artist(pa, pb);
}

static void albums_sort(void)
{
    int mode = MAX(0, MIN(3, global_settings.wheel_flow_sort));
    if (albs_sorted == mode)
        return;
    static int (* const cmp[4])(const void *, const void *) = {
        cmp_artist, cmp_title, cmp_year, cmp_added,
    };
    for (int i = 0; i < n_albs; i++)
        alb_order[i] = i;
    qsort(alb_order, n_albs, sizeof(alb_order[0]), cmp[mode]);
    albs_sorted = mode;
}

static struct alb *alb_at(int i) { return &albs[alb_order[i]]; }

static void alb_text(struct wheel_flow_src *s, int i, struct wheel_flow_text *t)
{
    (void)s;
    struct alb *a = alb_at(i);
    strlcpy(t->title, &pool[a->name], sizeof(t->title));
    strlcpy(t->sub, &pool[a->artist], sizeof(t->sub));
    char songs[32];
    snprintf(songs, sizeof(songs), "%d %s", a->count,
             str(a->count == 1 ? LANG_WHEEL_SONG : LANG_WHEEL_SONGS));
    if (a->year)
        snprintf(t->meta, sizeof(t->meta), "%d · %s", a->year, songs);
    else
        strlcpy(t->meta, songs, sizeof(t->meta));
}

static bool alb_track(struct wheel_flow_src *s, int i, char *path, size_t len)
{
    (void)s;
    struct tagcache_search tcs;
    if (!tagcache_search(&tcs, tag_filename))
        return false;
    bool ok = tagcache_retrieve(&tcs, alb_at(i)->rep, tag_filename, path, len);
    tagcache_search_finish(&tcs);
    return ok;
}

/* the album's songs in disc and track order */
static struct wheel_list *alb_fill(int i)
{
    static int pick[600];
    int a = alb_order[i], m = 0;
    for (int k = 0; k < n_atr && m < (int)ARRAYLEN(pick); k++)
        if (atr[k].alb == a)
            pick[m++] = k;
    for (int x = 1; x < m; x++)
        for (int y = x; y > 0 && atr[pick[y - 1]].order > atr[pick[y]].order; y--)
        {
            int t = pick[y]; pick[y] = pick[y - 1]; pick[y - 1] = t;
        }
    struct wheel_list *l = wheel_lv_list();
    struct tagcache_search tcs;
    if (!tagcache_search(&tcs, tag_filename))
        return l;
    for (int k = 0; k < m && l->n < l->max; k++)
    {
        struct atr *t = &atr[pick[k]];
        struct wheel_track *w = &l->t[l->n];
        if (!tagcache_retrieve(&tcs, t->idx, tag_filename, w->path, sizeof(w->path)))
            continue;
        if (!tagcache_retrieve(&tcs, t->idx, tag_title, w->title, sizeof(w->title)) ||
            bad_tag(w->title))
            w->title[0] = '\0';
        if (!tagcache_retrieve(&tcs, t->idx, tag_artist, w->artist, sizeof(w->artist)) ||
            bad_tag(w->artist))
            w->artist[0] = '\0';
        w->len_ms = t->len_ms;
        w->meta = true;
        l->n++;
    }
    l->total = l->n;
    tagcache_search_finish(&tcs);
    return l;
}

static int alb_ok(struct wheel_flow_src *s, int i, bool stay)
{
    (void)s; (void)stay;
    struct alb *a = alb_at(i);
    struct wheel_list *l = alb_fill(i);
    if (l->n <= 0)
        return 0;
    theme_on();
    struct wheel_lv lv = { .title = &pool[a->name], .subtitle = &pool[a->artist],
                           .kind = LV_MIX, .list = l, .fixed = true };
    int r = wheel_listview_show(&lv);
    theme_off();
    return r;
}

enum { AO_PLAY, AO_SHUFFLE, AO_NEXT, AO_LATER, AO_SORT, AO_COUNT };

static int alb_options(struct wheel_flow_src *s, int i)
{
    static const int lang[AO_COUNT] = { LANG_WHEEL_PLAY, LANG_WHEEL_SHUFFLE,
        LANG_WHEEL_PLAY_NEXT, LANG_WHEEL_PLAY_LATER, LANG_WHEEL_SORT_ALBUMS };
    const char *items[AO_COUNT];
    for (int k = 0; k < AO_COUNT; k++)
        items[k] = str(lang[k]);
    theme_on();
    int pick = wheel_pick(&pool[alb_at(i)->name], items, AO_COUNT, 0);
    int ret = 0;
    bool playing = audio_status() & AUDIO_STATUS_PLAY;
    switch (pick)
    {
        case AO_PLAY:
        case AO_SHUFFLE:
            if (wheel_play_list(alb_fill(i), 0, pick == AO_SHUFFLE))
                ret = GO_TO_WPS;
            break;
        case AO_NEXT:
        case AO_LATER:
        {
            struct wheel_list *l = alb_fill(i);
            if (!playing)
            {
                if (wheel_play_list(l, 0, false))
                    ret = GO_TO_WPS;
                break;
            }
            char rp[MAX_PATH];
            if (pick == AO_NEXT)
                for (int k = l->n - 1; k >= 0; k--)
                    playlist_insert_track(NULL, wheel_real_path(l->t[k].path, rp, sizeof(rp)),
                                          PLAYLIST_INSERT_FIRST, false, false);
            else
                for (int k = 0; k < l->n; k++)
                    playlist_insert_track(NULL, wheel_real_path(l->t[k].path, rp, sizeof(rp)),
                                          PLAYLIST_INSERT_LAST, false, false);
            playlist_sync(NULL);
            splash(HZ, pick == AO_NEXT ? ID2P(LANG_WHEEL_ADDED_NEXT) : ID2P(LANG_WHEEL_ADDED_QUEUE));
            break;
        }
        case AO_SORT:
        {
            static const int names[4] = { LANG_WHEEL_SORT_ARTIST, LANG_WHEEL_SORT_TITLE,
                                          LANG_WHEEL_SORT_YEAR, LANG_WHEEL_SORT_ADDED };
            const char *it[4];
            for (int k = 0; k < 4; k++)
                it[k] = str(names[k]);
            int cur = MAX(0, MIN(3, global_settings.wheel_flow_sort));
            int p = wheel_pick(str(LANG_WHEEL_SORT_ALBUMS), it, 4, cur);
            if (p >= 0 && p != cur)
            {
                int keep = alb_order[i];
                global_settings.wheel_flow_sort = p;
                settings_save();
                albums_sort();
                for (int k = 0; k < n_albs; k++)
                    if (alb_order[k] == keep)
                        s->sel = k;
                ret = WHEEL_FLOW_CHANGED;
            }
            break;
        }
    }
    theme_off();
    return ret;
}

static int alb_playing(struct wheel_flow_src *s) { (void)s; return -1; }

int wheel_album_flow(void)
{
    if (!tagcache_get_stat()->ready)
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_NEED_LIBRARY));
        return 0;
    }
    splash(0, ID2P(LANG_WAIT));
    long t0 = current_tick;
    if (!albums_build())
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_NEED_LIBRARY));
        return 0;
    }
    albums_sort();
    char what[48];
    snprintf(what, sizeof(what), "albums: list %d", n_albs);
    wheel_perf(what, current_tick - t0);
    static int last_sel;
    struct wheel_flow_src s = {
        .n = n_albs, .sel = MIN(last_sel, n_albs - 1), .heading = str(LANG_ALBUMS),
        .text = alb_text, .track = alb_track, .ok = alb_ok, .options = alb_options,
        .playing = alb_playing,
    };
    int r = wheel_flow_show(&s);
    last_sel = s.sel;
    return r;
}

#else /* !HAVE_TAGCACHE */
int wheel_album_flow(void) { return 0; }
#endif

#else /* !FLOW_OK */

bool wheel_flow_supported(void) { return false; }
int  wheel_flow_show(struct wheel_flow_src *s) { (void)s; return 0; }
int  wheel_disc_show(struct wheel_flow_src *s) { (void)s; return 0; }
int  wheel_album_flow(void) { return 0; }

#endif
