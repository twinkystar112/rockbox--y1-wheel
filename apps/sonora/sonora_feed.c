/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - OPML and RSS reading. Not a general XML parser: it looks for the
 * handful of elements podcasts use, tolerates sloppy feeds and never
 * allocates. Network access lives elsewhere (sonora_net); this file only
 * reads text that is already on the card.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
#include "sonora_feed.h"
#include "sonora_util.h"

static int low(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* case-insensitive search for needle in [p, end) */
static const char *find(const char *p, const char *end, const char *needle)
{
    size_t l = strlen(needle);
    for (; p + l <= end; p++)
    {
        size_t i = 0;
        while (i < l && low((unsigned char)p[i]) == low((unsigned char)needle[i]))
            i++;
        if (i == l)
            return p;
    }
    return NULL;
}

/* "<name" followed by '>', '/', or white space (so "<title" != "<titles") */
static const char *find_tag(const char *p, const char *end, const char *name)
{
    char open[48];
    snprintf(open, sizeof(open), "<%s", name);
    size_t l = strlen(open);
    while ((p = find(p, end, open)))
    {
        char c = p + l < end ? p[l] : '\0';
        if (c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
            return p;
        p += l;
    }
    return NULL;
}

/* text of the first <name>...</name> inside [p, end) */
static bool element(const char *p, const char *end, const char *name,
                    char *out, size_t len)
{
    const char *t = find_tag(p, end, name);
    if (!t)
        return false;
    const char *gt = memchr(t, '>', end - t);
    if (!gt)
        return false;
    if (gt[-1] == '/')
    {   /* <name/> */
        out[0] = '\0';
        return true;
    }
    char close[48];
    snprintf(close, sizeof(close), "</%s", name);
    const char *c = gt + 1;
    /* a CDATA block may contain "</name" text; skip over CDATA first */
    const char *scan = c;
    while (1)
    {
        const char *cd = find(scan, end, "<![CDATA[");
        const char *cl = find(scan, end, close);
        if (!cl)
            return false;
        if (cd && cd < cl)
        {
            const char *cde = find(cd + 9, end, "]]>");
            if (!cde)
                return false;
            scan = cde + 3;
            continue;
        }
        sonora_xml_text(c, cl - c, out, len);
        return true;
    }
}

/* value of attr="..." (or '...') inside one tag [t, tag_end) */
static bool attribute(const char *t, const char *tag_end, const char *attr,
                      char *out, size_t len)
{
    size_t l = strlen(attr);
    const char *p = t;
    while ((p = find(p, tag_end, attr)))
    {
        bool word_start = p > t && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r');
        const char *q = p + l;
        while (q < tag_end && (*q == ' ' || *q == '\t')) q++;
        if (word_start && q < tag_end && *q == '=')
        {
            q++;
            while (q < tag_end && (*q == ' ' || *q == '\t')) q++;
            if (q < tag_end && (*q == '"' || *q == '\''))
            {
                char quote = *q++;
                const char *e = memchr(q, quote, tag_end - q);
                if (!e)
                    return false;
                sonora_xml_text(q, e - q, out, len);
                return true;
            }
        }
        p += l;
    }
    return false;
}

int sonora_opml_parse(const char *buf, size_t n, sonora_opml_cb cb, void *ctx)
{
    const char *p = buf, *end = buf + n;
    int count = 0;
    while ((p = find_tag(p, end, "outline")))
    {
        const char *gt = memchr(p, '>', end - p);
        if (!gt)
            break;
        char url[512], title[160];
        if (attribute(p, gt, "xmlUrl", url, sizeof(url)) && url[0])
        {
            if (!attribute(p, gt, "title", title, sizeof(title)) || !title[0])
                if (!attribute(p, gt, "text", title, sizeof(title)) || !title[0])
                    strlcpy(title, url, sizeof(title));
            cb(title, url, ctx);
            count++;
        }
        p = gt + 1;
    }
    return count;
}

/* ------------------------------- times ------------------------------- */

static long days_from_civil(long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

long sonora_rfc822_time(const char *s)
{
    static const char months[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    while (*s && !isdigit((unsigned char)*s)) s++; /* skip "Tue, " */
    char *e;
    long day = strtol(s, &e, 10);
    if (e == s || day < 1 || day > 31)
        return 0;
    s = e;
    while (*s == ' ') s++;
    int mon = -1;
    for (int i = 0; i < 12; i++)
        if (low((unsigned char)s[0]) == months[i * 3] && low((unsigned char)s[1]) == months[i * 3 + 1] &&
            low((unsigned char)s[2]) == months[i * 3 + 2])
            mon = i + 1;
    if (mon < 0)
        return 0;
    while (*s && *s != ' ') s++;
    long year = strtol(s, &e, 10);
    if (e == s)
        return 0;
    if (year < 100)
        year += year < 70 ? 2000 : 1900;
    s = e;
    long hh = 0, mm = 0, ss = 0;
    while (*s == ' ') s++;
    if (isdigit((unsigned char)*s))
    {
        hh = strtol(s, &e, 10); s = e;
        if (*s == ':') { mm = strtol(s + 1, &e, 10); s = e; }
        if (*s == ':') { ss = strtol(s + 1, &e, 10); s = e; }
    }
    while (*s == ' ') s++;
    long off = 0;
    if (*s == '+' || *s == '-')
    {
        long z = strtol(s + 1, NULL, 10);
        off = (z / 100) * 3600 + (z % 100) * 60;
        if (*s == '-') off = -off;
    }
    else if (!strncmp(s, "EST", 3)) off = -5 * 3600;
    else if (!strncmp(s, "EDT", 3)) off = -4 * 3600;
    else if (!strncmp(s, "CST", 3)) off = -6 * 3600;
    else if (!strncmp(s, "CDT", 3)) off = -5 * 3600;
    else if (!strncmp(s, "MST", 3)) off = -7 * 3600;
    else if (!strncmp(s, "MDT", 3)) off = -6 * 3600;
    else if (!strncmp(s, "PST", 3)) off = -8 * 3600;
    else if (!strncmp(s, "PDT", 3)) off = -7 * 3600;
    return days_from_civil(year, mon, day) * 86400 + hh * 3600 + mm * 60 + ss - off;
}

long sonora_duration_secs(const char *s)
{
    long parts[3] = { 0, 0, 0 };
    int n = 0;
    while (*s == ' ') s++;
    if (!isdigit((unsigned char)*s))
        return 0;
    while (*s && n < 3)
    {
        char *e;
        parts[n++] = strtol(s, &e, 10);
        if (*e != ':')
            break;
        s = e + 1;
    }
    if (n == 3) return parts[0] * 3600 + parts[1] * 60 + parts[2];
    if (n == 2) return parts[0] * 60 + parts[1];
    return parts[0];
}

/* -------------------------------- RSS -------------------------------- */

static bool is_audio_type(const char *type, const char *url)
{
    if (!strncmp(type, "audio/", 6))
        return true;
    if (type[0] && strncmp(type, "application/octet-stream", 24))
        return false;
    /* no or generic type: trust the file name (query string cut) */
    char tmp[512];
    strlcpy(tmp, url, sizeof(tmp));
    char *q = strpbrk(tmp, "?#");
    if (q) *q = '\0';
    return sonora_is_audio(tmp);
}

int sonora_rss_parse(const char *buf, size_t n, char *channel_title, size_t tlen,
                     sonora_rss_cb cb, void *ctx)
{
    const char *end = buf + n;
    const char *first_item = find_tag(buf, end, "item");
    if (channel_title && tlen)
    {
        channel_title[0] = '\0';
        element(buf, first_item ? first_item : end, "title", channel_title, tlen);
    }
    int count = 0;
    static struct sonora_rss_item it;
    const char *p = first_item;
    while (p)
    {
        const char *close = find(p, end, "</item>");
        const char *item_end = close ? close : end;
        memset(&it, 0, sizeof(it));
        element(p, item_end, "title", it.title, sizeof(it.title));
        element(p, item_end, "guid", it.guid, sizeof(it.guid));
        char tmp[64];
        if (element(p, item_end, "pubDate", tmp, sizeof(tmp)))
            it.pub_epoch = sonora_rfc822_time(tmp);
        if (element(p, item_end, "itunes:duration", tmp, sizeof(tmp)))
            it.duration_s = sonora_duration_secs(tmp);
        if (!element(p, item_end, "itunes:summary", it.desc, sizeof(it.desc)) || !it.desc[0])
            element(p, item_end, "description", it.desc, sizeof(it.desc));
        const char *enc = find_tag(p, item_end, "enclosure");
        if (enc)
        {
            const char *gt = memchr(enc, '>', item_end - enc);
            if (gt)
            {
                attribute(enc, gt, "url", it.url, sizeof(it.url));
                attribute(enc, gt, "type", it.type, sizeof(it.type));
                if (attribute(enc, gt, "length", tmp, sizeof(tmp)))
                    it.length = strtoll(tmp, NULL, 10);
            }
        }
        if (it.url[0] && is_audio_type(it.type, it.url))
        {
            if (!it.guid[0])
                strlcpy(it.guid, it.url, sizeof(it.guid));
            if (!it.title[0])
                sonora_basename_noext(it.url, it.title, sizeof(it.title));
            count++;
            if (!cb(&it, ctx))
                break;
        }
        if (!close)
            break;
        p = find_tag(close + 7, end, "item");
    }
    return count;
}
