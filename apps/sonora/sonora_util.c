/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - small helpers shared by the parsers, the store and the screens.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
#include "sonora_util.h"

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

int sonora_natcmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b))
        {
            while (*a == '0' && isdigit((unsigned char)a[1])) a++;
            while (*b == '0' && isdigit((unsigned char)b[1])) b++;
            const char *ea = a, *eb = b;
            while (isdigit((unsigned char)*ea)) ea++;
            while (isdigit((unsigned char)*eb)) eb++;
            if (ea - a != eb - b)
                return (ea - a) < (eb - b) ? -1 : 1;
            for (; a < ea; a++, b++)
                if (*a != *b)
                    return *a < *b ? -1 : 1;
            continue;
        }
        int ca = lower((unsigned char)*a), cb = lower((unsigned char)*b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        a++; b++;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

void sonora_clean(char *dst, const char *src, size_t len)
{
    size_t i = 0;
    if (!len)
        return;
    for (; src && *src && i + 1 < len; src++)
        dst[i++] = (*src == '\t' || *src == '\n' || *src == '\r') ? ' ' : *src;
    dst[i] = '\0';
}

size_t sonora_put_utf8(char *out, size_t room, unsigned cp)
{
    if (cp < 0x80)
    {
        if (room < 1) return 0;
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800)
    {
        if (room < 2) return 0;
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000)
    {
        if (room < 3) return 0;
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    if (cp > 0x10ffff || room < 4)
        return 0;
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

void sonora_latin1_to_utf8(const unsigned char *in, size_t n, char *out, size_t len)
{
    size_t o = 0;
    if (!len)
        return;
    for (size_t i = 0; i < n && in[i]; i++)
    {
        size_t w = sonora_put_utf8(out + o, len - 1 - o, in[i]);
        if (!w) break;
        o += w;
    }
    out[o] = '\0';
}

void sonora_utf16_to_utf8(const unsigned char *in, size_t n, bool big_endian,
                          char *out, size_t len)
{
    size_t o = 0, i = 0;
    if (!len)
        return;
    if (n >= 2)
    {   /* a byte order mark wins over the caller's guess */
        if (in[0] == 0xfe && in[1] == 0xff) { big_endian = true; i = 2; }
        else if (in[0] == 0xff && in[1] == 0xfe) { big_endian = false; i = 2; }
    }
    for (; i + 1 < n; i += 2)
    {
        unsigned cp = big_endian ? (in[i] << 8) | in[i + 1] : (in[i + 1] << 8) | in[i];
        if (!cp)
            break;
        if (cp >= 0xd800 && cp < 0xdc00 && i + 3 < n)
        {
            unsigned lo = big_endian ? (in[i + 2] << 8) | in[i + 3]
                                     : (in[i + 3] << 8) | in[i + 2];
            if (lo >= 0xdc00 && lo < 0xe000)
            {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                i += 2;
            }
        }
        size_t w = sonora_put_utf8(out + o, len - 1 - o, cp);
        if (!w) break;
        o += w;
    }
    out[o] = '\0';
}

static unsigned entity(const char *s, size_t n, size_t *used)
{
    /* s points after '&' */
    static const struct { const char *name; unsigned cp; } named[] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' },
        { "apos;", '\'' }, { "nbsp;", 0xa0 },
    };
    if (n >= 2 && s[0] == '#')
    {
        unsigned cp = 0;
        size_t i = 1;
        bool hex = i < n && (s[i] == 'x' || s[i] == 'X');
        if (hex) i++;
        size_t start = i;
        for (; i < n && i < 10; i++)
        {
            char c = s[i];
            if (c >= '0' && c <= '9') cp = cp * (hex ? 16 : 10) + (c - '0');
            else if (hex && lower(c) >= 'a' && lower(c) <= 'f') cp = cp * 16 + (lower(c) - 'a' + 10);
            else break;
        }
        if (i > start && i < n && s[i] == ';')
        {
            *used = i + 1;
            return cp ? cp : '?';
        }
        return 0;
    }
    for (size_t k = 0; k < sizeof(named) / sizeof(named[0]); k++)
    {
        size_t l = strlen(named[k].name);
        if (n >= l && !memcmp(s, named[k].name, l))
        {
            *used = l;
            return named[k].cp;
        }
    }
    return 0;
}

/* one pass: CDATA copied raw, tags dropped, entities decoded */
void sonora_xml_text(const char *in, size_t n, char *out, size_t len)
{
    size_t o = 0, i = 0;
    bool space = true; /* drops leading space */
    if (!len)
        return;
    while (i < n && o + 1 < len)
    {
        char c = in[i];
        if (c == '<')
        {
            if (n - i >= 9 && !memcmp(in + i, "<![CDATA[", 9))
            {
                size_t j = i + 9;
                while (j + 2 < n && memcmp(in + j, "]]>", 3))
                {
                    char d = in[j++];
                    bool ws = d == ' ' || d == '\t' || d == '\n' || d == '\r';
                    if (ws) { if (!space && o + 1 < len) { out[o++] = ' '; space = true; } }
                    else if (d == '<')
                    {   /* HTML inside CDATA (show notes): skip the tag */
                        while (j < n && in[j] != '>') j++;
                        if (j < n) j++;
                        if (!space && o + 1 < len) { out[o++] = ' '; space = true; }
                    }
                    else if (d == '&')
                    {
                        size_t used = 0;
                        unsigned cp = entity(in + j, n - j, &used);
                        if (cp)
                        {
                            j += used;
                            size_t w = sonora_put_utf8(out + o, len - 1 - o, cp == 0xa0 ? ' ' : cp);
                            o += w; space = false;
                        }
                        else if (o + 1 < len) { out[o++] = '&'; space = false; }
                    }
                    else if (o + 1 < len) { out[o++] = d; space = false; }
                }
                i = j + 3;
                continue;
            }
            while (i < n && in[i] != '>') i++;
            i++;
            if (!space && o + 1 < len) { out[o++] = ' '; space = true; }
            continue;
        }
        if (c == '&')
        {
            size_t used = 0;
            unsigned cp = entity(in + i + 1, n - i - 1, &used);
            if (cp)
            {
                i += 1 + used;
                if (cp == 0xa0) cp = ' ';
                if (cp == ' ')
                {
                    if (!space) { out[o++] = ' '; space = true; }
                    continue;
                }
                o += sonora_put_utf8(out + o, len - 1 - o, cp);
                space = false;
                continue;
            }
        }
        bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (ws)
        {
            if (!space) { out[o++] = ' '; space = true; }
        }
        else
        {
            out[o++] = c;
            space = false;
        }
        i++;
    }
    while (o > 0 && out[o - 1] == ' ')
        o--;
    out[o] = '\0';
}

uint32_t sonora_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; s && *s; s++)
    {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

int sonora_split(char *line, char **fields, int max)
{
    int n = 0;
    if (max <= 0)
        return 0;
    fields[n++] = line;
    for (char *p = line; *p; p++)
    {
        if (*p == '\t')
        {
            *p = '\0';
            if (n < max)
                fields[n++] = p + 1;
        }
    }
    return n;
}

bool sonora_ext_is(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    if (!dot || strchr(dot, '/'))
        return false;
    dot++;
    while (*dot && *ext)
        if (lower((unsigned char)*dot++) != lower((unsigned char)*ext++))
            return false;
    return !*dot && !*ext;
}

bool sonora_is_audio(const char *name)
{
    static const char *const exts[] = {
        "mp3", "m4a", "m4b", "mp4", "aac", "ogg", "oga", "opus", "flac",
        "wav", "wv", "ape", "mpc", "wma", "aiff", "aif", "spx", "mp2",
    };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
        if (sonora_ext_is(name, exts[i]))
            return true;
    return false;
}

void sonora_fmt_time(char *buf, size_t len, unsigned long ms)
{
    unsigned long s = ms / 1000;
    if (s >= 3600)
        snprintf(buf, len, "%lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
    else
        snprintf(buf, len, "%lu:%02lu", s / 60, s % 60);
}

void sonora_basename_noext(const char *path, char *out, size_t len)
{
    const char *slash = strrchr(path, '/');
    strlcpy(out, slash ? slash + 1 : path, len);
    char *dot = strrchr(out, '.');
    if (dot && dot != out)
        *dot = '\0';
}

void sonora_safe_name(const char *in, char *out, size_t len)
{
    size_t o = 0;
    if (!len)
        return;
    for (; *in && o + 1 < len; in++)
    {
        unsigned char c = (unsigned char)*in;
        if (c < 32 || strchr("/\\:*?\"<>|", c))
            c = '_';
        out[o++] = (char)c;
    }
    /* no trailing dots or spaces, and never "." / ".." */
    while (o > 0 && (out[o - 1] == '.' || out[o - 1] == ' '))
        o--;
    out[o] = '\0';
    if (!o)
        strlcpy(out, "untitled", len);
}

void sonora_lines_init(struct sonora_lines *r, int fd)
{
    r->fd = fd;
    r->pos = r->end = 0;
}

bool sonora_lines_next(struct sonora_lines *r, char *line, size_t len)
{
    size_t o = 0;
    bool got = false;
    while (1)
    {
        if (r->pos >= r->end)
        {
            int n = r->fd >= 0 ? (int)read(r->fd, r->buf, sizeof(r->buf)) : 0;
            if (n <= 0)
                break;
            r->pos = 0;
            r->end = n;
        }
        char c = r->buf[r->pos++];
        got = true;
        if (c == '\n')
            break;
        if (c == '\r')
            continue;
        if (o + 1 < len)
            line[o++] = c;
    }
    if (len)
        line[o] = '\0';
    return got;
}
