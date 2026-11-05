/***************************************************************************
 * SONORA - OPML subscription lists and RSS podcast feeds (text only).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef SONORA_FEED_H
#define SONORA_FEED_H

#include "sonora_port.h"

/* OPML: one call per <outline> that has an xmlUrl */
typedef void (*sonora_opml_cb)(const char *title, const char *url, void *ctx);
int sonora_opml_parse(const char *buf, size_t n, sonora_opml_cb cb, void *ctx);

struct sonora_rss_item {
    char title[160];
    char guid[200];
    char url[512];       /* the enclosure: the audio file */
    char type[40];
    long long length;    /* bytes, as the feed claims (may be 0 or wrong) */
    long pub_epoch;      /* seconds since 1970 (UTC), 0 if unknown */
    long duration_s;     /* itunes:duration, 0 if unknown */
    char desc[300];      /* show notes, plain text, cut */
};

/* RSS 2.0: the channel title (may be NULL) and one call per <item> with
 * an audio enclosure. Returns the number of items reported. */
typedef bool (*sonora_rss_cb)(const struct sonora_rss_item *it, void *ctx);
int sonora_rss_parse(const char *buf, size_t n, char *channel_title, size_t tlen,
                     sonora_rss_cb cb, void *ctx);

/* "Tue, 02 Nov 2026 10:00:00 +0100" -> seconds since 1970 (UTC) */
long sonora_rfc822_time(const char *s);
/* "1:02:03", "62:03" or "3723" -> seconds */
long sonora_duration_secs(const char *s);

#endif
