/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - chapter lists, read-only.
 *
 * Rockbox reads the MP4 "chpl" atom only for gapless trimming and nothing
 * else (lib/rbcodec/metadata/mp4.c), so books play as one long track. This
 * file reads the chapter list without touching the audio file:
 *   - MP4 / M4B: the QuickTime chapter text track that a "tref/chap" points
 *     at (what most M4B tools write), else Nero's udta/chpl;
 *   - MP3: ID3v2.3 / 2.4 CHAP frames with their TIT2 titles;
 *   - any file: a cue sheet next to it or in .rockbox/cue (no 99 limit).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
#include "sonora_chapters.h"
#include "sonora_util.h"

/* ------------------------------ helpers ------------------------------ */

static bool lower_ascii_eq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static bool rd(int fd, void *buf, size_t n)
{
    return (size_t)read(fd, buf, n) == n;
}

static bool rd_u8(int fd, uint8_t *v) { return rd(fd, v, 1); }

static bool rd_u16(int fd, uint16_t *v)
{
    unsigned char b[2];
    if (!rd(fd, b, 2)) return false;
    *v = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}

static bool rd_u24(int fd, uint32_t *v)
{
    unsigned char b[3];
    if (!rd(fd, b, 3)) return false;
    *v = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    return true;
}

static bool rd_u32(int fd, uint32_t *v)
{
    unsigned char b[4];
    if (!rd(fd, b, 4)) return false;
    *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return true;
}

static bool rd_u64(int fd, uint64_t *v)
{
    uint32_t hi, lo;
    if (!rd_u32(fd, &hi) || !rd_u32(fd, &lo)) return false;
    *v = ((uint64_t)hi << 32) | lo;
    return true;
}

static bool seek_to(int fd, int64_t pos)
{
    if (pos < 0 || pos > 0x7fffffffLL)
        return false; /* files over 2 GB are not walked */
    return lseek(fd, (off_t)pos, SEEK_SET) == (off_t)pos;
}

static int cmp_start(const void *a, const void *b)
{
    const struct sonora_chapter *x = a, *y = b;
    return x->start_ms < y->start_ms ? -1 : (x->start_ms > y->start_ms ? 1 : 0);
}

/* sort, drop exact duplicates and chapters that start past the end */
static int finish(struct sonora_chapter *ch, int n, uint32_t duration_ms)
{
    if (n <= 0)
        return 0;
    qsort(ch, n, sizeof(*ch), cmp_start);
    int o = 0;
    for (int i = 0; i < n; i++)
    {
        if (duration_ms && ch[i].start_ms >= duration_ms && i > 0)
            break;
        if (o > 0 && ch[o - 1].start_ms == ch[i].start_ms)
        {
            if (!ch[o - 1].title[0])
                memcpy(ch[o - 1].title, ch[i].title, SONORA_CH_TITLE);
            continue;
        }
        if (o != i)
            ch[o] = ch[i];
        o++;
    }
    return o;
}

/* text read from a file into a title: UTF-8, or UTF-16 with a BOM */
static void set_title(struct sonora_chapter *c, const unsigned char *t, size_t n)
{
    char tmp[SONORA_CH_TITLE];
    if (n >= 2 && ((t[0] == 0xfe && t[1] == 0xff) || (t[0] == 0xff && t[1] == 0xfe)))
        sonora_utf16_to_utf8(t, n, true, tmp, sizeof(tmp));
    else
    {
        size_t k = n < sizeof(tmp) - 1 ? n : sizeof(tmp) - 1;
        memcpy(tmp, t, k);
        tmp[k] = '\0';
        /* never cut a UTF-8 sequence in half */
        while (k > 0 && ((unsigned char)tmp[k - 1] & 0xc0) == 0x80) k--;
        if (k > 0 && ((unsigned char)tmp[k - 1] & 0xc0) == 0xc0 && n > sizeof(tmp) - 1)
            tmp[k - 1] = '\0';
    }
    sonora_clean(c->title, tmp, SONORA_CH_TITLE);
}

/* -------------------------------- MP4 -------------------------------- */

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                            ((uint32_t)(c) << 8) | (uint32_t)(d))
#define MAX_TRAKS 12

struct trak {
    uint32_t id;
    uint32_t chap_ref[4];
    int n_chap_ref;
    uint32_t timescale;
    uint32_t handler;
    int64_t stbl_start, stbl_end;
};

struct mp4 {
    int fd;
    struct trak t[MAX_TRAKS];
    int nt;
    uint32_t mv_timescale;
    uint64_t mv_duration;
    int64_t chpl_start, chpl_end;
};

/* atom header at the current position; returns false at the end */
static bool atom(int fd, int64_t pos, int64_t end, uint32_t *type,
                 int64_t *body, int64_t *next)
{
    uint32_t size32;
    if (pos + 8 > end || !seek_to(fd, pos) || !rd_u32(fd, &size32) || !rd_u32(fd, type))
        return false;
    int64_t size = size32, hdr = 8;
    if (size32 == 1)
    {
        uint64_t big;
        if (!rd_u64(fd, &big))
            return false;
        size = (int64_t)big;
        hdr = 16;
    }
    else if (size32 == 0)
        size = end - pos;
    if (size < hdr || pos + size > end)
        return false;
    *body = pos + hdr;
    *next = pos + size;
    return true;
}

static void walk_trak(struct mp4 *m, int64_t pos, int64_t end)
{
    if (m->nt >= MAX_TRAKS)
        return;
    struct trak *t = &m->t[m->nt];
    memset(t, 0, sizeof(*t));
    uint32_t type;
    int64_t body, next;
    while (atom(m->fd, pos, end, &type, &body, &next))
    {
        if (type == FOURCC('t','k','h','d'))
        {
            uint8_t ver;
            uint32_t flags;
            if (seek_to(m->fd, body) && rd_u8(m->fd, &ver) && rd_u24(m->fd, &flags))
            {
                lseek(m->fd, ver == 1 ? 16 : 8, SEEK_CUR);
                rd_u32(m->fd, &t->id);
            }
        }
        else if (type == FOURCC('t','r','e','f'))
        {
            int64_t p2 = body, b2, n2;
            uint32_t t2;
            while (atom(m->fd, p2, next, &t2, &b2, &n2))
            {
                if (t2 == FOURCC('c','h','a','p') && seek_to(m->fd, b2))
                {
                    for (int64_t q = b2; q + 4 <= n2 && t->n_chap_ref < 4; q += 4)
                        if (!rd_u32(m->fd, &t->chap_ref[t->n_chap_ref++]))
                            break;
                }
                p2 = n2;
            }
        }
        else if (type == FOURCC('m','d','i','a'))
        {
            int64_t p2 = body, b2, n2;
            uint32_t t2;
            while (atom(m->fd, p2, next, &t2, &b2, &n2))
            {
                if (t2 == FOURCC('m','d','h','d'))
                {
                    uint8_t ver;
                    uint32_t flags;
                    if (seek_to(m->fd, b2) && rd_u8(m->fd, &ver) && rd_u24(m->fd, &flags))
                    {
                        lseek(m->fd, ver == 1 ? 16 : 8, SEEK_CUR);
                        rd_u32(m->fd, &t->timescale);
                    }
                }
                else if (t2 == FOURCC('h','d','l','r'))
                {
                    if (seek_to(m->fd, b2 + 8))
                        rd_u32(m->fd, &t->handler);
                }
                else if (t2 == FOURCC('m','i','n','f'))
                {
                    int64_t p3 = b2, b3, n3;
                    uint32_t t3;
                    while (atom(m->fd, p3, n2, &t3, &b3, &n3))
                    {
                        if (t3 == FOURCC('s','t','b','l'))
                        {
                            t->stbl_start = b3;
                            t->stbl_end = n3;
                        }
                        p3 = n3;
                    }
                }
                p2 = n2;
            }
        }
        pos = next;
    }
    m->nt++;
}

static void walk_moov(struct mp4 *m, int64_t pos, int64_t end)
{
    uint32_t type;
    int64_t body, next;
    while (atom(m->fd, pos, end, &type, &body, &next))
    {
        if (type == FOURCC('m','v','h','d'))
        {
            uint8_t ver;
            uint32_t flags;
            if (seek_to(m->fd, body) && rd_u8(m->fd, &ver) && rd_u24(m->fd, &flags))
            {
                lseek(m->fd, ver == 1 ? 16 : 8, SEEK_CUR);
                rd_u32(m->fd, &m->mv_timescale);
                if (ver == 1)
                    rd_u64(m->fd, &m->mv_duration);
                else
                {
                    uint32_t d;
                    if (rd_u32(m->fd, &d))
                        m->mv_duration = d;
                }
            }
        }
        else if (type == FOURCC('t','r','a','k'))
            walk_trak(m, body, next);
        else if (type == FOURCC('u','d','t','a'))
        {
            int64_t p2 = body, b2, n2;
            uint32_t t2;
            while (atom(m->fd, p2, next, &t2, &b2, &n2))
            {
                if (t2 == FOURCC('c','h','p','l'))
                {
                    m->chpl_start = b2;
                    m->chpl_end = n2;
                }
                p2 = n2;
            }
        }
        pos = next;
    }
}

static uint32_t to_ms(uint64_t t, uint32_t timescale)
{
    if (!timescale)
        return 0;
    uint64_t ms = t * 1000 / timescale;
    return ms > 0xffffffffULL ? 0xffffffffu : (uint32_t)ms;
}

/* the chapter text track: sample times from stts, offsets from
 * stsc + stco/co64 + stsz, each sample = 16-bit length + text */
static int read_text_track(struct mp4 *m, const struct trak *t,
                           struct sonora_chapter *ch, int max)
{
    static uint32_t starts[SONORA_MAX_CHAPTERS];
    static uint32_t sizes[SONORA_MAX_CHAPTERS];
    static int64_t chunk_off[SONORA_MAX_CHAPTERS];
    static int64_t sample_off[SONORA_MAX_CHAPTERS];
    struct { uint32_t first, per; } stsc[64];
    int n_stsc = 0, n_chunks = 0, n = 0, n_sizes = 0;
    uint32_t fixed_size = 0, size_count = 0;
    if (max > SONORA_MAX_CHAPTERS)
        max = SONORA_MAX_CHAPTERS;

    int64_t pos = t->stbl_start, body, next;
    uint32_t type, count, u;
    while (atom(m->fd, pos, t->stbl_end, &type, &body, &next))
    {
        if (!seek_to(m->fd, body + 4)) /* version + flags */
            break;
        if (type == FOURCC('s','t','t','s') && rd_u32(m->fd, &count))
        {
            uint64_t time = 0;
            for (uint32_t e = 0; e < count && n < max; e++)
            {
                uint32_t samples, delta;
                if (!rd_u32(m->fd, &samples) || !rd_u32(m->fd, &delta))
                    break;
                for (uint32_t s = 0; s < samples && n < max; s++)
                {
                    starts[n++] = to_ms(time, t->timescale);
                    time += delta;
                }
            }
        }
        else if (type == FOURCC('s','t','s','z') && rd_u32(m->fd, &fixed_size) &&
                 rd_u32(m->fd, &size_count))
        {
            if (!fixed_size)
                for (uint32_t s = 0; s < size_count && n_sizes < max; s++)
                {
                    if (!rd_u32(m->fd, &u))
                        break;
                    sizes[n_sizes++] = u;
                }
        }
        else if (type == FOURCC('s','t','s','c') && rd_u32(m->fd, &count))
        {
            for (uint32_t e = 0; e < count && n_stsc < 64; e++)
            {
                uint32_t desc;
                if (!rd_u32(m->fd, &stsc[n_stsc].first) || !rd_u32(m->fd, &stsc[n_stsc].per) ||
                    !rd_u32(m->fd, &desc))
                    break;
                n_stsc++;
            }
        }
        else if ((type == FOURCC('s','t','c','o') || type == FOURCC('c','o','6','4')) &&
                 rd_u32(m->fd, &count))
        {
            for (uint32_t e = 0; e < count && n_chunks < max; e++)
            {
                if (type == FOURCC('c','o','6','4'))
                {
                    uint64_t v;
                    if (!rd_u64(m->fd, &v)) break;
                    chunk_off[n_chunks++] = (int64_t)v;
                }
                else
                {
                    if (!rd_u32(m->fd, &u)) break;
                    chunk_off[n_chunks++] = u;
                }
            }
        }
        pos = next;
    }
    if (fixed_size)
        for (n_sizes = 0; n_sizes < n && n_sizes < max; n_sizes++)
            sizes[n_sizes] = fixed_size;
    if (n > n_sizes)
        n = n_sizes;
    if (!n || !n_chunks || !n_stsc)
        return 0;

    /* where each sample lives */
    int s = 0;
    for (int e = 0; e < n_stsc && s < n; e++)
    {
        uint32_t last = (e + 1 < n_stsc) ? stsc[e + 1].first : (uint32_t)n_chunks + 1;
        for (uint32_t c = stsc[e].first; c < last && s < n; c++)
        {
            if (c < 1 || (int)c > n_chunks)
                break;
            int64_t off = chunk_off[c - 1];
            for (uint32_t k = 0; k < stsc[e].per && s < n; k++)
            {
                sample_off[s] = off;
                off += sizes[s];
                s++;
            }
        }
    }
    n = s;

    for (int i = 0; i < n; i++)
    {
        ch[i].start_ms = starts[i];
        ch[i].title[0] = '\0';
        uint16_t len;
        if (sizes[i] < 2 || !seek_to(m->fd, sample_off[i]) || !rd_u16(m->fd, &len))
            continue;
        if (len > sizes[i] - 2)
            len = (uint16_t)(sizes[i] - 2);
        unsigned char buf[SONORA_CH_TITLE * 2];
        size_t take = len < sizeof(buf) ? len : sizeof(buf);
        if (rd(m->fd, buf, take))
            set_title(&ch[i], buf, take);
    }
    return n;
}

static int read_chpl(struct mp4 *m, struct sonora_chapter *ch, int max)
{
    uint8_t ver, count;
    uint32_t flags, skip;
    if (!seek_to(m->fd, m->chpl_start) || !rd_u8(m->fd, &ver) || !rd_u24(m->fd, &flags))
        return 0;
    if (ver && !rd_u32(m->fd, &skip))
        return 0;
    if (!rd_u8(m->fd, &count))
        return 0;
    int n = 0;
    for (int i = 0; i < count && n < max; i++)
    {
        uint64_t start;
        uint8_t len;
        unsigned char buf[256];
        if (!rd_u64(m->fd, &start) || !rd_u8(m->fd, &len) || (len && !rd(m->fd, buf, len)))
            break;
        ch[n].start_ms = (uint32_t)(start / 10000); /* 100 ns units */
        ch[n].title[0] = '\0';
        set_title(&ch[n], buf, len);
        n++;
    }
    return n;
}

int sonora_chapters_mp4(int fd, struct sonora_chapter *ch, int max,
                        int *source, uint32_t *duration_ms)
{
    static struct mp4 m;
    memset(&m, 0, sizeof(m));
    m.fd = fd;
    if (source) *source = SCH_NONE;
    long fsize = sonora_filesize(fd);
    if (fsize < 16)
        return 0;

    int64_t pos = 0, body, next;
    uint32_t type;
    bool seen_ftyp = false;
    while (atom(fd, pos, fsize, &type, &body, &next))
    {
        if (type == FOURCC('f','t','y','p'))
            seen_ftyp = true;
        else if (type == FOURCC('m','o','o','v'))
        {
            walk_moov(&m, body, next);
            break;
        }
        pos = next;
    }
    if (!seen_ftyp && !m.nt)
        return 0;
    uint32_t dur = to_ms(m.mv_duration, m.mv_timescale);
    if (duration_ms)
        *duration_ms = dur;

    /* a track some other track points at with tref/chap */
    for (int i = 0; i < m.nt; i++)
        for (int r = 0; r < m.t[i].n_chap_ref; r++)
            for (int j = 0; j < m.nt; j++)
                if (m.t[j].id == m.t[i].chap_ref[r] && m.t[j].stbl_end > m.t[j].stbl_start)
                {
                    int n = finish(ch, read_text_track(&m, &m.t[j], ch, max), dur);
                    if (n > 0)
                    {
                        if (source) *source = SCH_QUICKTIME;
                        return n;
                    }
                }
    if (m.chpl_end > m.chpl_start)
    {
        int n = finish(ch, read_chpl(&m, ch, max), dur);
        if (n > 0)
        {
            if (source) *source = SCH_NERO;
            return n;
        }
    }
    return 0;
}

/* -------------------------------- ID3v2 ------------------------------- */

static uint32_t syncsafe(const unsigned char *b)
{
    return ((uint32_t)(b[0] & 0x7f) << 21) | ((uint32_t)(b[1] & 0x7f) << 14) |
           ((uint32_t)(b[2] & 0x7f) << 7) | (b[3] & 0x7f);
}

static uint32_t be32(const unsigned char *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

static void id3_text(const unsigned char *p, size_t n, char *out, size_t len)
{
    if (!n) { out[0] = '\0'; return; }
    unsigned enc = p[0];
    p++; n--;
    char tmp[SONORA_CH_TITLE * 2];
    switch (enc)
    {
        case 0: sonora_latin1_to_utf8(p, n, tmp, sizeof(tmp)); break;
        case 1: sonora_utf16_to_utf8(p, n, false, tmp, sizeof(tmp)); break;
        case 2: sonora_utf16_to_utf8(p, n, true, tmp, sizeof(tmp)); break;
        default:
        {
            size_t k = n < sizeof(tmp) - 1 ? n : sizeof(tmp) - 1;
            memcpy(tmp, p, k);
            tmp[k] = '\0';
            break;
        }
    }
    sonora_clean(out, tmp, len);
}

int sonora_chapters_id3(int fd, struct sonora_chapter *ch, int max)
{
    unsigned char h[10];
    if (!seek_to(fd, 0) || !rd(fd, h, 10) || memcmp(h, "ID3", 3))
        return 0;
    int major = h[3];
    if (major != 3 && major != 4)
        return 0; /* ID3v2.2 has no CHAP frames */
    uint32_t tag_end = 10 + syncsafe(h + 6);
    uint32_t pos = 10;
    if (h[5] & 0x40)
    {   /* extended header */
        unsigned char e[4];
        if (!rd(fd, e, 4))
            return 0;
        pos += major == 4 ? syncsafe(e) : be32(e) + 4;
    }
    int n = 0;
    static unsigned char body[4096];
    while (pos + 10 <= tag_end && n < max)
    {
        unsigned char f[10];
        if (!seek_to(fd, pos) || !rd(fd, f, 10) || !f[0])
            break;
        uint32_t size = major == 4 ? syncsafe(f + 4) : be32(f + 4);
        if (!size || pos + 10 + size > tag_end)
            break;
        if (!memcmp(f, "CHAP", 4))
        {
            uint32_t take = size < sizeof(body) ? size : sizeof(body);
            if (!rd(fd, body, take))
                break;
            size_t i = 0;
            while (i < take && body[i]) i++; /* element id */
            i++;
            if (i + 16 <= take)
            {
                ch[n].start_ms = be32(body + i);
                ch[n].title[0] = '\0';
                i += 16;
                /* sub-frames: the title is TIT2 */
                while (i + 10 <= take)
                {
                    uint32_t ss = major == 4 ? syncsafe(body + i + 4) : be32(body + i + 4);
                    if (!body[i] || i + 10 + ss > take)
                        break;
                    if (!memcmp(body + i, "TIT2", 4))
                        id3_text(body + i + 10, ss, ch[n].title, SONORA_CH_TITLE);
                    i += 10 + ss;
                }
                n++;
            }
        }
        pos += 10 + size;
    }
    return finish(ch, n, 0);
}

/* -------------------------------- cue -------------------------------- */

static const char *after_word(const char *s, const char *word)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t l = strlen(word);
    for (size_t i = 0; i < l; i++)
        if (lower_ascii_eq(s[i], word[i]) == 0)
            return NULL;
    if (s[l] && s[l] != ' ' && s[l] != '\t')
        return NULL;
    s += l;
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void unquote(const char *s, char *out, size_t len)
{
    if (*s == '"')
    {
        s++;
        const char *e = strchr(s, '"');
        size_t k = e ? (size_t)(e - s) : strlen(s);
        if (k >= len) k = len - 1;
        memcpy(out, s, k);
        out[k] = '\0';
    }
    else
    {
        strlcpy(out, s, len);
        char *sp = strpbrk(out, " \t");
        if (sp) *sp = '\0';
    }
}

static bool same_name(const char *cue_file, const char *audio_path)
{
    const char *a = strrchr(audio_path, '/');
    a = a ? a + 1 : audio_path;
    const char *c = strrchr(cue_file, '/');
    if (!c) c = strrchr(cue_file, '\\');
    c = c ? c + 1 : cue_file;
    for (; *a && *c; a++, c++)
        if (!lower_ascii_eq(*a, *c))
            return false;
    return !*a && !*c;
}

int sonora_chapters_cue(const char *cue_path, const char *audio_path,
                        struct sonora_chapter *ch, int max)
{
    int fd = open(cue_path, O_RDONLY | O_BINARY);
    if (fd < 0)
        return 0;
    struct sonora_lines r;
    char line[512], file[MAX_PATH] = "";
    int files = 0, n = 0, cur = -1;
    bool ours = true, any_match = false;
    sonora_lines_init(&r, fd);
    /* first pass: do the FILE lines name our file? */
    while (sonora_lines_next(&r, line, sizeof(line)))
    {
        const char *p = after_word(line, "FILE");
        if (p)
        {
            files++;
            unquote(p, file, sizeof(file));
            if (audio_path && same_name(file, audio_path))
                any_match = true;
        }
    }
    lseek(fd, 0, SEEK_SET);
    sonora_lines_init(&r, fd);
    ours = files <= 1 || !any_match;
    while (sonora_lines_next(&r, line, sizeof(line)))
    {
        const char *p;
        char *s = line;
        if ((unsigned char)s[0] == 0xef && (unsigned char)s[1] == 0xbb && (unsigned char)s[2] == 0xbf)
            s += 3; /* UTF-8 BOM */
        if ((p = after_word(s, "FILE")))
        {
            unquote(p, file, sizeof(file));
            ours = files <= 1 || !any_match || (audio_path && same_name(file, audio_path));
            cur = -1;
        }
        else if (!ours)
            continue;
        else if ((p = after_word(s, "TRACK")))
        {
            if (n >= max)
                break;
            cur = n++;
            ch[cur].start_ms = 0xffffffffu;
            ch[cur].title[0] = '\0';
        }
        else if (cur >= 0 && (p = after_word(s, "TITLE")))
        {
            char t[SONORA_CH_TITLE];
            unquote(p, t, sizeof(t));
            sonora_clean(ch[cur].title, t, SONORA_CH_TITLE);
        }
        else if (cur >= 0 && (p = after_word(s, "INDEX")))
        {
            int idx = atoi(p);
            const char *t = strpbrk(p, " \t");
            if (!t)
                continue;
            unsigned mm = 0, ss = 0, ff = 0;
            if (sscanf(t, " %u:%u:%u", &mm, &ss, &ff) >= 2 &&
                (idx == 1 || ch[cur].start_ms == 0xffffffffu))
                ch[cur].start_ms = (mm * 60 + ss) * 1000 + ff * 1000 / 75;
        }
    }
    close(fd);
    int o = 0;
    for (int i = 0; i < n; i++)
        if (ch[i].start_ms != 0xffffffffu)
            ch[o++] = ch[i];
    return finish(ch, o, 0);
}

/* ------------------------------- facade ------------------------------ */

static bool find_cue(const char *path, char *out, size_t len)
{
    char base[MAX_PATH];
    const char *slash = strrchr(path, '/');
    strlcpy(base, slash ? slash + 1 : path, sizeof(base));
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    /* same name, .cue */
    strlcpy(out, path, len);
    char *d = strrchr(out, '.'), *s = strrchr(out, '/');
    if (d && (!s || d > s))
    {
        *d = '\0';
        strlcat(out, ".cue", len);
        int fd = open(out, O_RDONLY);
        if (fd >= 0) { close(fd); return true; }
    }
    /* .rockbox/cue/<name>.cue (keeps the music folders untouched) */
    snprintf(out, len, "%s/%s.cue", SONORA_CUE_DIR, base);
    int fd = open(out, O_RDONLY);
    if (fd >= 0) { close(fd); return true; }
    /* file.ext.cue */
    snprintf(out, len, "%s.cue", path);
    fd = open(out, O_RDONLY);
    if (fd >= 0) { close(fd); return true; }
    return false;
}

int sonora_chapters_read(const char *path, struct sonora_chapter *ch, int max,
                         int *source)
{
    int n = 0, src = SCH_NONE;
    if (max > SONORA_MAX_CHAPTERS)
        max = SONORA_MAX_CHAPTERS;
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd >= 0)
    {
        if (sonora_ext_is(path, "m4b") || sonora_ext_is(path, "m4a") ||
            sonora_ext_is(path, "mp4") || sonora_ext_is(path, "aac"))
            n = sonora_chapters_mp4(fd, ch, max, &src, NULL);
        else
        {
            n = sonora_chapters_id3(fd, ch, max);
            if (n > 0)
                src = SCH_ID3;
        }
        close(fd);
    }
    if (n <= 1)
    {   /* one "chapter" is no chapter list; a cue sheet may do better */
        char cue[MAX_PATH];
        if (find_cue(path, cue, sizeof(cue)))
        {
            static struct sonora_chapter tmp[SONORA_MAX_CHAPTERS];
            int k = sonora_chapters_cue(cue, path, tmp, max);
            if (k > n)
            {
                memcpy(ch, tmp, k * sizeof(*ch));
                n = k;
                src = SCH_CUE;
            }
        }
    }
    if (n <= 0)
        src = SCH_NONE;
    if (source)
        *source = src;
    return n;
}
