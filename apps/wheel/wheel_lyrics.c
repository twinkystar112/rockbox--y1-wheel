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
#include <stdio.h>
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
#include "splash.h"
#include "wheel_lyrics.h"
#include "wheel_flow.h"
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

/* one line of LRC or plain text; used: bytes of text[] taken so far */
static size_t text_used;
static void add_line(const char *p)
{
    if (n_lines >= MAX_LINES)
        return;
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
            return;
        stamps[n_st++] = -1; /* unsynced line */
    }
    while (*p == ' ')
        p++;
    size_t len = strlen(p);
    while (len && (p[len-1] == '\r' || p[len-1] == ' '))
        len--;
    if (text_used + len + 1 > sizeof(text))
        return;
    memcpy(text + text_used, p, len);
    text[text_used + len] = '\0';
    for (int i = 0; i < n_st && n_lines < MAX_LINES; i++)
    {
        lines[n_lines].ms = stamps[i];
        lines[n_lines].off = text_used;
        n_lines++;
    }
    text_used += len + 1;
}

/* a timed line (SYLT) */
static void add_timed(long ms, const char *p)
{
    size_t len = strlen(p);
    while (len && (p[len-1] == '\r' || p[len-1] == '\n' || p[len-1] == ' '))
        len--;
    while (len && (*p == '\n' || *p == '\r'))
        p++, len--;
    if (n_lines >= MAX_LINES || text_used + len + 1 > sizeof(text))
        return;
    memcpy(text + text_used, p, len);
    text[text_used + len] = '\0';
    lines[n_lines].ms = ms;
    lines[n_lines].off = text_used;
    n_lines++;
    text_used += len + 1;
}

/* a whole block of text: one add_line() per line */
static void add_text(char *t)
{
    while (*t && n_lines < MAX_LINES)
    {
        char *nl = t + strcspn(t, "\r\n");
        char c = *nl;
        *nl = '\0';
        add_line(t);
        if (!c)
            break;
        t = nl + 1;
        if (c == '\r' && *t == '\n')
            t++;
    }
}

/* mixed lyrics: if anything is timed, drop the untimed lines */
static void finish_lines(void)
{
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

/* ------------------------- lyrics inside the song ------------------------- */
/* MP3 (ID3v2.3/2.4 USLT and SYLT), FLAC (LYRICS / UNSYNCEDLYRICS comments)
 * and M4A/MP4 ("\xa9lyr"). Read only; the file is never changed. */
static char emb[TEXT_SIZE];
static unsigned char frame[TEXT_SIZE * 2];

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static uint32_t syncsafe(const unsigned char *p)
{
    return (uint32_t)(p[0] & 127) << 21 | (uint32_t)(p[1] & 127) << 14 |
           (uint32_t)(p[2] & 127) << 7 | (p[3] & 127);
}

/* ID3 text in encoding enc (0 Latin-1, 1 UTF-16 + BOM, 2 UTF-16BE, 3 UTF-8)
 * -> UTF-8 in out; returns the bytes of src used, the terminator included */
static size_t id3_text(int enc, const unsigned char *src, size_t n, char *out, size_t cap)
{
    size_t len = 0, used;
    bool wide = enc == 1 || enc == 2;
    if (wide)
    {
        while (len + 1 < n && (src[len] || src[len + 1]))
            len += 2;
        used = MIN(n, len + 2);
    }
    else
    {
        while (len < n && src[len])
            len++;
        used = MIN(n, len + 1);
    }
    if (!cap)
        return used;
    unsigned char *o = (unsigned char *)out;
    if (enc == 3)
    {
        size_t k = MIN(len, cap - 1);
        memcpy(o, src, k);
        o += k;
    }
    else if (enc == 0)
    {
        size_t k = MIN(len, (cap - 1) / 2);
        o = iso_decode(src, o, ISO_8859_1, k);
    }
    else
    {
        bool le = enc == 1 && len >= 2 && src[0] == 0xff && src[1] == 0xfe;
        bool bom = enc == 1 && len >= 2 && ((src[0] == 0xff && src[1] == 0xfe) ||
                                            (src[0] == 0xfe && src[1] == 0xff));
        const unsigned char *u = src + (bom ? 2 : 0);
        size_t units = MIN((len - (bom ? 2 : 0)) / 2, (cap - 1) / 3);
        o = le ? utf16LEdecode(u, o, units) : utf16BEdecode(u, o, units);
    }
    *o = '\0';
    return used;
}

static bool id3_lyrics(int fd)
{
    unsigned char h[10];
    lseek(fd, 0, SEEK_SET);
    if (read(fd, h, 10) != 10 || memcmp(h, "ID3", 3) || h[3] < 3 || h[3] > 4 || (h[5] & 0x80))
        return false;   /* no tag, ID3v2.2 or a tag in "unsynchronisation" */
    int ver = h[3];
    uint32_t end = 10 + syncsafe(h + 6), pos = 10;
    if (h[5] & 0x40)
    {   /* extended header */
        unsigned char x[4];
        if (read(fd, x, 4) != 4)
            return false;
        pos += ver == 4 ? syncsafe(x) : 4 + be32(x);
    }
    bool got_sylt = false, got_uslt = false;
    while (pos + 10 <= end)
    {
        lseek(fd, pos, SEEK_SET);
        if (read(fd, h, 10) != 10 || !h[0])
            break;                          /* padding */
        uint32_t size = ver == 4 ? syncsafe(h + 4) : be32(h + 4);
        pos += 10 + size;
        if (pos > end || size < 6)
            continue;
        bool sylt = !memcmp(h, "SYLT", 4), uslt = !memcmp(h, "USLT", 4);
        bool txxx = !memcmp(h, "TXXX", 4);  /* some taggers: TXXX "LYRICS" etc. */
        if ((!sylt && !uslt && !txxx) || (ver == 4 && (h[9] & 0x03)) || size > sizeof(frame) ||
            ((uslt || txxx) && (got_uslt || got_sylt)) || (sylt && got_sylt))
            continue;                       /* not lyrics, or packed in a way we skip */
        if ((uint32_t)read(fd, frame, size) != size)
            break;
        int enc = frame[0];
        if (enc > 3)
            continue;
        if (txxx)
        {
            char desc[24];
            size_t at = 1 + id3_text(enc, frame + 1, size - 1, desc, sizeof(desc));
            if (at < size && (!strcasecmp(desc, "LYRICS") || !strcasecmp(desc, "UNSYNCEDLYRICS") ||
                              !strcasecmp(desc, "UNSYNCED LYRICS") || !strcasecmp(desc, "USLT")))
            {
                id3_text(enc, frame + at, size - at, emb, sizeof(emb));
                got_uslt = emb[0] != '\0';
            }
        }
        else if (uslt)
        {
            size_t at = 4 + id3_text(enc, frame + 4, size - 4, NULL, 0);  /* descriptor */
            if (at < size)
            {
                id3_text(enc, frame + at, size - at, emb, sizeof(emb));
                got_uslt = emb[0] != '\0';
            }
        }
        else if (frame[4] == 2)
        {   /* SYLT with times in ms: text, then a 4-byte time, repeated */
            int before = n_lines;
            size_t at = 6 + id3_text(enc, frame + 6, size - 6, NULL, 0);
            char one[512];
            while (at < size)
            {
                at += id3_text(enc, frame + at, size - at, one, sizeof(one));
                if (at + 4 > size)
                    break;
                add_timed((long)be32(frame + at), one);
                at += 4;
            }
            got_sylt = n_lines > before;
        }
    }
    if (got_sylt)
        return true;
    if (got_uslt)
        add_text(emb);
    return got_uslt;
}

/* a "NAME=value" comment the lyrics live in */
static bool lyrics_name(const char *c, size_t n)
{
    static const char *names[] = { "LYRICS=", "UNSYNCEDLYRICS=", "UNSYNCED LYRICS=" };
    for (size_t i = 0; i < ARRAYLEN(names); i++)
    {
        size_t k = strlen(names[i]);
        if (n >= k && !strncasecmp(c, names[i], k))
            return true;
    }
    return false;
}

static bool flac_lyrics(int fd)
{
    unsigned char h[4];
    lseek(fd, 0, SEEK_SET);
    if (read(fd, h, 4) != 4 || memcmp(h, "fLaC", 4))
        return false;
    for (int blocks = 0; blocks < 64; blocks++)
    {
        if (read(fd, h, 4) != 4)
            return false;
        bool last = h[0] & 0x80;
        uint32_t len = (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
        off_t next = lseek(fd, 0, SEEK_CUR) + len;
        if ((h[0] & 0x7f) == 4)
        {   /* VORBIS_COMMENT, little-endian lengths */
            unsigned char b[4];
            if (read(fd, b, 4) != 4)
                return false;
            lseek(fd, le32(b), SEEK_CUR);           /* vendor */
            if (read(fd, b, 4) != 4)
                return false;
            uint32_t count = le32(b);
            for (uint32_t i = 0; i < count && i < 1000; i++)
            {
                if (read(fd, b, 4) != 4)
                    return false;
                uint32_t n = le32(b);
                char name[20];
                size_t peek = MIN(n, sizeof(name));
                if (read(fd, name, peek) != (ssize_t)peek)
                    return false;
                if (lyrics_name(name, peek))
                {
                    size_t eq = (char *)memchr(name, '=', peek) - name + 1;
                    lseek(fd, -(off_t)(peek - eq), SEEK_CUR);
                    size_t want = MIN(n - eq, sizeof(emb) - 1);
                    if (read(fd, emb, want) != (ssize_t)want)
                        return false;
                    emb[want] = '\0';
                    add_text(emb);
                    return n_lines > 0;
                }
                lseek(fd, n - peek, SEEK_CUR);
            }
            return false;
        }
        if (last)
            return false;
        lseek(fd, next, SEEK_SET);
    }
    return false;
}

/* MP4: moov > udta > meta > ilst > \xa9lyr > data */
static bool mp4_find(int fd, off_t from, off_t to, const char *const *path)
{
    off_t pos = from;
    while (pos + 8 <= to)
    {
        unsigned char h[8];
        lseek(fd, pos, SEEK_SET);
        if (read(fd, h, 8) != 8)
            return false;
        uint64_t size = be32(h), head = 8;
        if (size == 1)
        {
            unsigned char x[8];
            if (read(fd, x, 8) != 8)
                return false;
            size = (uint64_t)be32(x) << 32 | be32(x + 4);
            head = 16;
        }
        else if (size == 0)
            size = to - pos;
        if (size < head || pos + (off_t)size > to)
            return false;
        if (!memcmp(h + 4, path[0], 4))
        {
            off_t body = pos + head + (!memcmp(path[0], "meta", 4) ? 4 : 0);
            if (path[1])
                return mp4_find(fd, body, pos + size, path + 1);
            /* the "data" atom: size, "data", type, locale, then the text */
            unsigned char d[16];
            lseek(fd, body, SEEK_SET);
            if (read(fd, d, 16) != 16 || memcmp(d + 4, "data", 4) || be32(d) < 16)
                return false;
            size_t n = MIN((size_t)be32(d) - 16, sizeof(emb) - 1);
            if (read(fd, emb, n) != (ssize_t)n)
                return false;
            emb[n] = '\0';
            add_text(emb);
            return n_lines > 0;
        }
        pos += size;
    }
    return false;
}

static bool mp4_lyrics(int fd)
{
    static const char *const path[] = { "moov", "udta", "meta", "ilst", "\xa9lyr", NULL };
    unsigned char h[8];
    lseek(fd, 0, SEEK_SET);
    if (read(fd, h, 8) != 8 || memcmp(h + 4, "ftyp", 4))
        return false;
    return mp4_find(fd, 0, ffilesize(fd), path);
}

static void load_embedded(void)
{
    int fd = open(track_path, O_RDONLY);
    if (fd < 0)
        return;
    if (!id3_lyrics(fd) && !flac_lyrics(fd))
        mp4_lyrics(fd);
    close(fd);
}

static void load_lrc(void)
{
    n_lines = 0;
    text_used = 0;
    synced = false;
    need_load = false;
    if (!track_path[0])
        return;

    /* a .lrc next to the song wins; else lyrics stored in the song */
    char lrc[MAX_PATH];
    strlcpy(lrc, track_path, sizeof(lrc));
    char *dot = strrchr(lrc, '.');
    char *slash = strrchr(lrc, '/');
    int fd = -1;
    if (dot && !(slash && dot < slash))
    {
        strlcpy(dot, ".lrc", sizeof(lrc) - (dot - lrc));
        fd = open(lrc, O_RDONLY);
        if (fd < 0)
        {
            strlcpy(dot, ".LRC", sizeof(lrc) - (dot - lrc));
            fd = open(lrc, O_RDONLY);
        }
    }
    if (fd >= 0)
    {
        char line[512];
        while (read_line(fd, line, sizeof(line)) > 0 && n_lines < MAX_LINES)
            add_line(line);
        close(fd);
    }
    else
        load_embedded();
    finish_lines();
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

/* the line being sung now, -1 before the first */
static int sung_line(void)
{
    struct mp3entry *id3 = audio_current_track();
    long pos = id3 ? (long)id3->elapsed : 0;
    int cur = -1;
    if (!synced)
    {   /* no times: move through the words at the song's pace */
        if (id3 && id3->length > 0)
            cur = (int)((long long)n_lines * pos / (long long)id3->length);
        cur = MIN(cur, n_lines - 1);
    }
    else
        for (int i = 0; i < n_lines && lines[i].ms <= pos; i++)
            cur = i;
    return cur;
}

/* rel = -2..2 around the line being sung now */
const char *wheel_lyrics_line(int rel)
{
    if (!wheel_lyrics_available())
        return NULL;
    int cur = sung_line();
    int want = cur + rel;
    if (want < 0 || want >= n_lines)
        return NULL;
    const char *t = text + lines[want].off;
    return *t ? t : NULL;
}

/* ------------------------ karaoke (%Wk in a skin) ------------------------
 * The line being sung in the viewport's font and colour, and the next one
 * under it in the font of the viewport the tag names, dimmed. Long lines
 * wrap onto more rows (centred, never scrolling sideways), so a whole line
 * can always be read. Drawn again only when the line changes (or the skin
 * asks for everything): nothing to do between lines. */

/* breaks s into rows that fit width: returns the row count (at most max);
 * a last row that still doesn't hold the rest ends in "..." */
static int wrap_rows(struct screen *d, const char *s, int width,
                     char rows[][96], int max, bool *cut)
{
    int n = 0;
    if (cut)
        *cut = false;
    while (*s == ' ') s++;
    while (*s && n < max)
    {
        int fit = 0, len = strlen(s), w, h;
        /* the longest run of whole words that fits */
        for (int i = 1; i <= len; i++)
        {
            if (i < len && s[i] != ' ')
                continue;
            if (i >= (int)sizeof(rows[0]))
                break;
            char tmp[96];
            memcpy(tmp, s, i); tmp[i] = '\0';
            d->getstringsize(tmp, &w, &h);
            if (w > width)
                break;
            fit = i;
        }
        if (fit == 0)
        {   /* one word wider than the screen: cut it */
            fit = 1;
            for (int i = 1; i <= len && i < (int)sizeof(rows[0]) - 1; i++)
            {
                char tmp[96];
                memcpy(tmp, s, i); tmp[i] = '\0';
                d->getstringsize(tmp, &w, &h);
                if (w > width) break;
                fit = i;
            }
            /* (don't split a UTF-8 character) */
            while (fit > 1 && (s[fit] & 0xc0) == 0x80) fit--;
        }
        bool last = n == max - 1 && s[fit] && s[fit + strspn(s + fit, " ")];
        memcpy(rows[n], s, fit);
        rows[n][fit] = '\0';
        if (last && cut)
            *cut = true;
        if (last)
        {   /* make room for "..." */
            char tmp[100];
            for (;;)
            {
                snprintf(tmp, sizeof(tmp), "%s...", rows[n]);
                d->getstringsize(tmp, &w, &h);
                int l = strlen(rows[n]);
                if (w <= width || l <= 1) break;
                do l--; while (l > 1 && (rows[n][l] & 0xc0) == 0x80);
                rows[n][l] = '\0';
            }
            strlcat(rows[n], "...", sizeof(rows[0]));
        }
        n++;
        s += fit;
        while (*s == ' ') s++;
    }
    return n;
}

void wheel_lyrics_draw_karaoke(struct screen *d, struct viewport *vp,
                               struct viewport *next_vp, bool full)
{
    static int last_cur = -2;
    static char last_track[MAX_PATH];
    if (!wheel_lyrics_available())
        return;
    int cur = sung_line();
    if (!full && cur == last_cur && !strcmp(last_track, track_path))
        return;
    last_cur = cur;
    strlcpy(last_track, track_path, sizeof(last_track));

    int w = vp->width, h = vp->height;
    int old_font = vp->font;
    unsigned fg = vp->fg_pattern;
    d->set_drawmode(DRMODE_SOLID | DRMODE_INVERSEVID);
    d->fillrect(0, 0, w, h);                 /* (the backdrop, if there is one) */

    const char *now = cur >= 0 ? text + lines[cur].off : NULL;
    if (!now || !*now)
        now = "\xc2\xb7 \xc2\xb7 \xc2\xb7";      /* between lines: a pause */
    const char *next = cur + 1 < n_lines ? text + lines[cur + 1].off : NULL;

    static char rows[5][96];
    int fh, fw;
    bool cut;
    d->getstringsize("Ag", &fw, &fh);
    int n1 = wrap_rows(d, now, w - 8, rows, MAX(1, MIN(5, h / fh)), &cut);
    if (cut && next_vp)
    {   /* too long for the big letters: the smaller ones, so all of it shows */
        vp->font = next_vp->font;
        d->getstringsize("Ag", &fw, &fh);
        n1 = wrap_rows(d, now, w - 8, rows, MAX(1, MIN(5, h / fh)), NULL);
    }
    d->set_drawmode(DRMODE_FG);
    int y = 0;
    for (int i = 0; i < n1 && y + fh <= h; i++, y += fh)
    {
        int tw, th;
        d->getstringsize(rows[i], &tw, &th);
        d->putsxy((w - tw) / 2, y, rows[i]);
    }
    if (next && *next)
    {
        if (next_vp)
            vp->font = next_vp->font;
        d->getstringsize("Ag", &fw, &fh);
        y += 6;
        int left = (h - y) / MAX(1, fh);
        if (left > 0)
        {
            int n2 = wrap_rows(d, next, w - 8, rows, MIN(2, left), NULL);
            /* the next line dimmed: between the text and page colours */
            unsigned dim = LCD_RGBPACK(
                (RGB_UNPACK_RED(global_settings.fg_color) + RGB_UNPACK_RED(vp->bg_pattern)) / 2,
                (RGB_UNPACK_GREEN(global_settings.fg_color) + RGB_UNPACK_GREEN(vp->bg_pattern)) / 2,
                (RGB_UNPACK_BLUE(global_settings.fg_color) + RGB_UNPACK_BLUE(vp->bg_pattern)) / 2);
            d->set_foreground(dim);
            for (int i = 0; i < n2 && y + fh <= h; i++, y += fh)
            {
                int tw, th;
                d->getstringsize(rows[i], &tw, &th);
                d->putsxy((w - tw) / 2, y, rows[i]);
            }
        }
    }
    vp->font = old_font;
    d->set_foreground(fg);
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
            {   /* say so, rather than skipping the step without a word */
                splash(HZ, ID2P(LANG_WHEEL_NO_LYRICS));
                mode = WHEEL_MODE_SCRUB;
            }
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

/* the lyrics from the Now Playing menu (hold centre) */
void wheel_lyrics_open(void)
{
    if (!wheel_lyrics_available() && !wheel_net_available())
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_NO_LYRICS_HINT));
        return;
    }
    mode = WHEEL_MODE_LYRICS;
    bool to_scrub = wheel_lyrics_screen();
    mode = to_scrub ? WHEEL_MODE_SCRUB : WHEEL_MODE_NORMAL;
    if (mode == WHEEL_MODE_SCRUB)
    {
        last_scrub = current_tick;
        scrub_streak = 0;
    }
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
        bool screen = wheel_screen_on();
        if (screen)
            draw_lyrics(scroll, cur, bg, status);
        else
            scroll = target;   /* nothing to glide to with the screen off */

        int action = get_action(CONTEXT_WPS, !screen ? HZ : scroll != target ? HZ/25 : HZ/5);
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
            case ACTION_WPS_CYCLE_MODE: /* centre: on to scrubbing (Wheel) */
            case ACTION_WPS_HOTKEY:     /* or just closes the lyrics (classic) */
                to_scrub = global_settings.wheel_np_centre != 0;
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
