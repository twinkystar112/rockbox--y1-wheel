/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - podcast refresh and download clean-up.
 *
 * Off unless the user turns on "Network features". A refresh switches the
 * network on, reads each show's feed, offers the episodes newer than the
 * ones offered before and downloads the ones the user accepts into
 * <podcasts>/<show>/, through a ".part" file that is renamed when complete.
 * Every finished download is recorded in downloads.tsv, the manifest.
 *
 * Clean-up deletes only files that are in the manifest, inside the podcasts
 * folder, still the size SONORA wrote, and played to the end. Episodes the
 * user copied in are never deleted, and neither are unplayed ones.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "action.h"
#include "splash.h"
#include "yesno.h"
#include "settings.h"
#include "dir.h"
#include "file.h"
#include "string-extra.h"
#include "sonora.h"
#include "sonora_util.h"
#include "sonora_feed.h"

/* ------------------- the platform layer, off Android ------------------- */
#if !(CONFIG_PLATFORM & PLATFORM_ANDROID)
/* The desktop simulator has no network on purpose. So the whole refresh ->
 * download -> manifest -> clean-up path can still be exercised there, it
 * accepts file:// URLs (a local feed and local "episodes") and nothing else.
 * Native players without a network: unsupported. */
#if (CONFIG_PLATFORM & PLATFORM_SDL)
bool sonora_net_supported(void) { return true; }
bool sonora_net_begin(void) { return true; }
void sonora_net_end(void) { }
int sonora_net_fetch(const char *url, const char *out_path,
                     bool (*progress)(long long done, long long total, void *ctx),
                     void *ctx)
{
    if (strncmp(url, "file://", 7))
        return -1;
    int in = open(url + 7, O_RDONLY);
    if (in < 0)
        return 404;
    int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0)
    {
        close(in);
        return -1;
    }
    long long total = ffilesize(in), done = 0;
    static char buf[16 * 1024];
    int n, rc = 200;
    while ((n = read(in, buf, sizeof(buf))) > 0)
    {
        if (write(out, buf, n) != n)
        {
            rc = -1;
            break;
        }
        done += n;
        if (progress && !progress(done, total, ctx))
        {
            rc = -2;
            break;
        }
    }
    close(in);
    close(out);
    return rc;
}
#else
bool sonora_net_supported(void) { return false; }
bool sonora_net_begin(void) { return false; }
void sonora_net_end(void) { }
int sonora_net_fetch(const char *url, const char *out_path,
                     bool (*progress)(long long done, long long total, void *ctx),
                     void *ctx)
{
    (void)url; (void)out_path; (void)progress; (void)ctx;
    return -1;
}
#endif
#endif /* !ANDROID */

/* ------------------------------- refresh ------------------------------- */

#define MAX_CANDS 64
struct cand {
    int show;
    long pub;
    long long length;
    char url[512];
    char title[160];
    char guid[200];
    char type[40];
};
static struct cand cands[MAX_CANDS];
static int n_cands;

struct feed_ctx {
    int show;
    long last_pub, newest;
    int first;            /* index of this show's first candidate */
};

static bool feed_item(const struct sonora_rss_item *it, void *ctx)
{
    struct feed_ctx *f = ctx;
    if (it->pub_epoch > f->newest)
        f->newest = it->pub_epoch;
    if (f->last_pub && it->pub_epoch && it->pub_epoch <= f->last_pub)
        return true;     /* offered before */
    if (sonora_download_find_guid(it->guid))
        return true;     /* already here */
    if (n_cands >= MAX_CANDS)
        return false;
    struct cand *c = &cands[n_cands++];
    c->show = f->show;
    c->pub = it->pub_epoch;
    c->length = it->length;
    strlcpy(c->url, it->url, sizeof(c->url));
    strlcpy(c->title, it->title, sizeof(c->title));
    strlcpy(c->guid, it->guid, sizeof(c->guid));
    strlcpy(c->type, it->type, sizeof(c->type));
    return true;
}

static int cand_cmp(const void *a, const void *b)
{
    const struct cand *x = a, *y = b;
    if (x->show != y->show)
        return x->show - y->show;
    return x->pub > y->pub ? -1 : (x->pub < y->pub ? 1 : 0);
}

struct prog { int i, n; long last; };

static bool on_progress(long long done, long long total, void *ctx)
{
    struct prog *p = ctx;
    if (TIME_AFTER(current_tick, p->last + HZ / 2))
    {
        p->last = current_tick;
        if (total > 0)
            splashf(0, str(LANG_SONORA_DOWNLOADING_PCT), p->i, p->n,
                    (int)(done * 100 / total));
        else
            splashf(0, str(LANG_SONORA_DOWNLOADING_MB), p->i, p->n, (int)(done >> 20));
    }
    return !action_userabort(TIMEOUT_NOBLOCK);
}

static char *read_small(const char *path, long *len, long max)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    long size = ffilesize(fd);
    char *buf = (size > 0 && size <= max) ? malloc(size + 1) : NULL;
    if (buf)
    {
        *len = read(fd, buf, size);
        if (*len <= 0)
        {
            free(buf);
            buf = NULL;
        }
        else
            buf[*len] = '\0';
    }
    close(fd);
    return buf;
}

static void episode_ext(const struct cand *c, char *ext, size_t len)
{
    char tmp[512];
    strlcpy(tmp, c->url, sizeof(tmp));
    char *q = strpbrk(tmp, "?#");
    if (q) *q = '\0';
    if (sonora_is_audio(tmp))
    {
        strlcpy(ext, strrchr(tmp, '.') + 1, len);
        return;
    }
    if (strstr(c->type, "mp4") || strstr(c->type, "m4a") || strstr(c->type, "aac"))
        strlcpy(ext, "m4a", len);
    else if (strstr(c->type, "ogg"))
        strlcpy(ext, "ogg", len);
    else if (strstr(c->type, "opus"))
        strlcpy(ext, "opus", len);
    else
        strlcpy(ext, "mp3", len);
}

static bool download(const struct cand *c, int i, int n)
{
    struct sonora_show *sh = sonora_show_at(c->show);
    if (!sh)
        return false;
    char dir[MAX_PATH], name[96], ext[8], path[MAX_PATH], part[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s/%s", sonora_podcasts_root(), sh->folder);
    if (!dir_exists(sonora_podcasts_root()))
        mkdir(sonora_podcasts_root());
    if (!dir_exists(dir))
        mkdir(dir);
    sonora_safe_name(c->title, name, 80);
    episode_ext(c, ext, sizeof(ext));
    snprintf(path, sizeof(path), "%s/%s.%s", dir, name, ext);
    for (int k = 2; file_exists(path) && k < 100; k++)
        snprintf(path, sizeof(path), "%s/%s (%d).%s", dir, name, k, ext);
    snprintf(part, sizeof(part), "%s.part", path);
    struct prog p = { i, n, current_tick - HZ };
    int rc = sonora_net_fetch(c->url, part, on_progress, &p);
    if (rc != 200)
    {
        remove(part);
        return false;
    }
    if (rename(part, path) < 0)
    {
        remove(part);
        return false;
    }
    int fd = open(path, O_RDONLY);
    long long size = fd >= 0 ? ffilesize(fd) : 0;
    if (fd >= 0)
        close(fd);
    struct sonora_download *d = sonora_download_add(path);
    if (d)
    {
        d->size = size;
        d->pub_epoch = c->pub;
        strlcpy(d->guid, c->guid, sizeof(d->guid));
        strlcpy(d->show, sh->folder, sizeof(d->show));
    }
    sonora_downloads_save();
    return true;
}

int sonora_refresh_screen(void)
{
    sonora_init();
    if (!global_settings.sonora_network)
    {
        splash(HZ * 3, ID2P(LANG_SONORA_NETWORK_OFF));
        return 0;
    }
    int feeds = 0;
    for (int s = 0; s < sonora_shows_count(); s++)
        if (sonora_show_at(s)->url[0])
            feeds++;
    if (!feeds)
    {
        splash(HZ * 3, ID2P(LANG_SONORA_NO_FEEDS));
        return 0;
    }
    splash(0, ID2P(LANG_SONORA_CONNECTING));
    if (!sonora_net_begin())
    {
        splash(HZ * 3, ID2P(LANG_SONORA_NO_WIFI));
        return 0;
    }
    char feeddir[MAX_PATH];
    sonora_data_path("feeds", feeddir, sizeof(feeddir));
    if (!dir_exists(feeddir))
        mkdir(feeddir);

    n_cands = 0;
    int failed = 0;
    for (int s = 0; s < sonora_shows_count(); s++)
    {
        struct sonora_show *sh = sonora_show_at(s);
        if (!sh->url[0])
            continue;
        splashf(0, str(LANG_SONORA_CHECKING), sh->title);
        char path[MAX_PATH], part[MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s.xml", feeddir, sh->folder);
        snprintf(part, sizeof(part), "%s.part", path);
        if (sonora_net_fetch(sh->url, part, NULL, NULL) != 200)
        {   /* keep the copy from last time */
            remove(part);
            failed++;
            continue;
        }
        remove(path);
        rename(part, path);
        long len = 0;
        char *buf = read_small(path, &len, 8 * 1024 * 1024);
        if (!buf)
        {
            failed++;
            continue;
        }
        struct feed_ctx f = { s, sh->last_pub, sh->last_pub, n_cands };
        char title[96];
        sonora_rss_parse(buf, len, title, sizeof(title), feed_item, &f);
        free(buf);
        if (title[0] && !strcmp(sh->title, sh->folder))
            sonora_clean(sh->title, title, sizeof(sh->title));
        /* the first time, offer only the newest few, not the whole archive */
        qsort(cands + f.first, n_cands - f.first, sizeof(cands[0]), cand_cmp);
        if (!sh->last_pub)
        {
            int keep = sh->keep > 0 ? sh->keep : 3;
            if (n_cands - f.first > keep)
                n_cands = f.first + keep;
        }
        sh->last_pub = f.newest;
    }
    sonora_shows_save();

    if (!n_cands)
    {
        sonora_net_end();
        splash(HZ * 2, failed ? ID2P(LANG_SONORA_REFRESH_FAILED) : ID2P(LANG_SONORA_NOTHING_NEW));
        return 0;
    }
    char q[96];
    snprintf(q, sizeof(q), str(LANG_SONORA_DOWNLOAD_N), n_cands);
    int ok = 0;
    if (yesno_pop(q))
    {
        for (int i = 0; i < n_cands; i++)
        {
            if (download(&cands[i], i + 1, n_cands))
                ok++;
            else if (action_userabort(TIMEOUT_NOBLOCK))
                break;
        }
    }
    sonora_net_end();
    sonora_library_invalidate();
    splashf(HZ * 2, str(LANG_SONORA_DOWNLOADED_N), ok, n_cands);
    sonora_cleanup_downloads(false);
    return 0;
}

/* ------------------------------- clean-up ------------------------------ */

static bool played(const char *path)
{
    struct sonora_progress *p = sonora_progress_find(path);
    return p && p->state == SP_DONE;
}

static int dl_newer(const void *a, const void *b)
{
    const struct sonora_download *x = sonora_download_at(*(const int *)a);
    const struct sonora_download *y = sonora_download_at(*(const int *)b);
    long px = x->pub_epoch ? x->pub_epoch : (long)x->when;
    long py = y->pub_epoch ? y->pub_epoch : (long)y->when;
    return px > py ? -1 : (px < py ? 1 : 0);
}

int sonora_cleanup_downloads(bool ask)
{
    sonora_init();
    static char victims[SONORA_MAX_DOWNLOADS][MAX_PATH];
    int nv = 0;
    /* played and ours */
    for (int i = 0; i < sonora_downloads_count(); i++)
    {
        struct sonora_download *d = sonora_download_at(i);
        if (played(d->path) && sonora_download_may_delete(d->path, sonora_podcasts_root()))
            strlcpy(victims[nv++], d->path, MAX_PATH);
    }
    if (!ask)
    {   /* automatic: only beyond each show's "keep newest N" */
        nv = 0;
        static int idx[SONORA_MAX_DOWNLOADS];
        for (int s = 0; s < sonora_shows_count(); s++)
        {
            struct sonora_show *sh = sonora_show_at(s);
            if (sh->keep <= 0)
                continue;
            int n = 0;
            for (int i = 0; i < sonora_downloads_count(); i++)
                if (!strcmp(sonora_download_at(i)->show, sh->folder))
                    idx[n++] = i;
            qsort(idx, n, sizeof(int), dl_newer);
            for (int k = sh->keep; k < n; k++)
            {
                struct sonora_download *d = sonora_download_at(idx[k]);
                if (played(d->path) && sonora_download_may_delete(d->path, sonora_podcasts_root()))
                    strlcpy(victims[nv++], d->path, MAX_PATH);
            }
        }
    }
    if (!nv)
    {
        if (ask)
            splash(HZ * 2, ID2P(LANG_SONORA_NOTHING_TO_REMOVE));
        return 0;
    }
    if (ask)
    {
        char q[96];
        snprintf(q, sizeof(q), str(LANG_SONORA_REMOVE_N), nv);
        if (!yesno_pop(q))
            return 0;
    }
    int removed = 0;
    for (int i = 0; i < nv; i++)
    {
        /* checked again right before: the gate is the only way in */
        if (sonora_download_may_delete(victims[i], sonora_podcasts_root()) &&
            remove(victims[i]) == 0)
        {
            sonora_download_forget(victims[i]);
            removed++;
        }
    }
    sonora_downloads_save();
    sonora_library_invalidate();
    if (ask || removed)
        splashf(HZ * 2, str(LANG_SONORA_REMOVED_N), removed);
    return removed;
}
