/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - progress, bookmarks, shows and the download manifest, kept as
 * tab-separated text in .rockbox/sonora/. Saves are atomic (temporary file,
 * previous version kept as .bak, rename) and loads fall back to the .bak.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
#include "sonora_data.h"
#include "sonora_util.h"

static char data_dir[MAX_PATH];

void sonora_data_init(const char *dir)
{
    strlcpy(data_dir, dir, sizeof(data_dir));
}

const char *sonora_data_dir(void)
{
    return data_dir;
}

void sonora_data_path(const char *name, char *out, size_t len)
{
    snprintf(out, len, "%s/%s", data_dir, name);
}

/* ------------------------------ generic ----------------------------- */

static bool load_one(const char *path, sonora_row_cb cb, void *ctx)
{
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0)
        return false;
    static char line[2048];
    static char *f[16];
    struct sonora_lines r;
    sonora_lines_init(&r, fd);
    bool first = true, ok = true;
    while (sonora_lines_next(&r, line, sizeof(line)))
    {
        if (first)
        {
            first = false;
            if (strncmp(line, "# sonora-format=", 16))
            {
                ok = false; /* not ours: leave it alone */
                break;
            }
            if (atoi(line + 16) > 1)
            {
                ok = false; /* written by a newer SONORA: read nothing */
                break;
            }
            continue;
        }
        if (!line[0] || line[0] == '#')
            continue;
        int nf = sonora_split(line, f, 16);
        cb(f, nf, ctx);
    }
    close(fd);
    return ok && !first;
}

bool sonora_tsv_load(const char *path, sonora_row_cb cb, void *ctx)
{
    if (load_one(path, cb, ctx))
        return true;
    char bak[MAX_PATH];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    return load_one(bak, cb, ctx);
}

int sonora_tsv_begin(const char *path, char *tmp, size_t tlen)
{
    snprintf(tmp, tlen, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fd >= 0)
        fdprintf(fd, "%s\n", SONORA_FORMAT_LINE);
    return fd;
}

bool sonora_tsv_commit(int fd, const char *path, const char *tmp)
{
    if (fd < 0)
        return false;
    close(fd);
    char bak[MAX_PATH];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    int test = open(path, O_RDONLY);
    if (test >= 0)
    {
        close(test);
        remove(bak);
        if (rename(path, bak) < 0)
        {
            remove(tmp);
            return false;
        }
    }
    if (rename(tmp, path) < 0)
    {
        rename(bak, path); /* put the old one back */
        return false;
    }
    return true;
}

static void field(char *dst, const char *src, size_t len)
{
    sonora_clean(dst, src ? src : "", len);
}

/* ----------------------------- progress ----------------------------- */

static struct sonora_progress progress[SONORA_MAX_PROGRESS];
static int n_progress;

static void progress_row(char **f, int nf, void *ctx)
{
    (void)ctx;
    if (nf < 11 || n_progress >= SONORA_MAX_PROGRESS || !f[0][0])
        return;
    struct sonora_progress *p = &progress[n_progress++];
    memset(p, 0, sizeof(*p));
    p->kind = f[0][0];
    field(p->key, f[1], sizeof(p->key));
    p->state = f[2][0] ? f[2][0] : SP_NEW;
    field(p->cur, f[3], sizeof(p->cur));
    p->pos_ms = strtoul(f[4], NULL, 10);
    p->done_ms = strtoul(f[5], NULL, 10);
    p->total_ms = strtoul(f[6], NULL, 10);
    p->speed = atoi(f[7]);
    p->when = strtoul(f[8], NULL, 10);
    p->sig = strtoul(f[9], NULL, 10);
    field(p->title, f[10], sizeof(p->title));
    if (nf > 11)
        p->index = atoi(f[11]);
    if (!p->key[0])
        n_progress--;
}

bool sonora_progress_load(void)
{
    char path[MAX_PATH];
    n_progress = 0;
    sonora_data_path("progress.tsv", path, sizeof(path));
    return sonora_tsv_load(path, progress_row, NULL);
}

bool sonora_progress_save(void)
{
    char path[MAX_PATH], tmp[MAX_PATH];
    sonora_data_path("progress.tsv", path, sizeof(path));
    int fd = sonora_tsv_begin(path, tmp, sizeof(tmp));
    if (fd < 0)
        return false;
    fdprintf(fd, "# kind\tkey\tstate\tcurrent file\tpos ms\tdone ms\ttotal ms\tspeed\tlast played\tfingerprint\ttitle\tindex\n");
    for (int i = 0; i < n_progress; i++)
    {
        struct sonora_progress *p = &progress[i];
        fdprintf(fd, "%c\t%s\t%c\t%s\t%lu\t%lu\t%lu\t%ld\t%lu\t%lu\t%s\t%ld\n",
                 p->kind, p->key, p->state, p->cur, (unsigned long)p->pos_ms,
                 (unsigned long)p->done_ms, (unsigned long)p->total_ms, (long)p->speed,
                 (unsigned long)p->when, (unsigned long)p->sig, p->title, (long)p->index);
    }
    return sonora_tsv_commit(fd, path, tmp);
}

int sonora_progress_count(void) { return n_progress; }

struct sonora_progress *sonora_progress_at(int i)
{
    return (i >= 0 && i < n_progress) ? &progress[i] : NULL;
}

struct sonora_progress *sonora_progress_find(const char *key)
{
    for (int i = 0; i < n_progress; i++)
        if (!strcmp(progress[i].key, key))
            return &progress[i];
    return NULL;
}

struct sonora_progress *sonora_progress_get(const char *key, char kind)
{
    struct sonora_progress *p = sonora_progress_find(key);
    if (p)
        return p;
    if (n_progress >= SONORA_MAX_PROGRESS)
    {   /* full: forget the oldest finished one, else the oldest */
        int victim = -1;
        for (int i = 0; i < n_progress; i++)
            if (progress[i].kind != SK_MUSIC_C &&
                (victim < 0 || (progress[i].state == SP_DONE && progress[victim].state != SP_DONE) ||
                 ((progress[i].state == SP_DONE) == (progress[victim].state == SP_DONE) &&
                  progress[i].when < progress[victim].when)))
                victim = i;
        if (victim < 0)
            return NULL;
        progress[victim] = progress[--n_progress];
    }
    p = &progress[n_progress++];
    memset(p, 0, sizeof(*p));
    field(p->key, key, sizeof(p->key));
    p->kind = kind;
    p->state = SP_NEW;
    return p;
}

void sonora_progress_remove(const char *key)
{
    for (int i = 0; i < n_progress; i++)
        if (!strcmp(progress[i].key, key))
        {
            progress[i] = progress[--n_progress];
            return;
        }
}

static int recent_cmp(const void *a, const void *b)
{
    uint32_t wa = progress[*(const int *)a].when, wb = progress[*(const int *)b].when;
    return wa > wb ? -1 : (wa < wb ? 1 : 0);
}

int sonora_progress_recent(char kind, char state, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < n_progress && n < max; i++)
        if ((!kind || progress[i].kind == kind) && (!state || progress[i].state == state))
            out[n++] = i;
    qsort(out, n, sizeof(int), recent_cmp);
    return n;
}

/* ----------------------------- bookmarks ---------------------------- */

static struct sonora_bookmark bookmarks[SONORA_MAX_BOOKMARKS];
static int n_bookmarks;

static void bookmark_row(char **f, int nf, void *ctx)
{
    (void)ctx;
    if (nf < 6 || n_bookmarks >= SONORA_MAX_BOOKMARKS || !f[0][0])
        return;
    struct sonora_bookmark *b = &bookmarks[n_bookmarks++];
    memset(b, 0, sizeof(*b));
    field(b->key, f[0], sizeof(b->key));
    field(b->cur, f[1], sizeof(b->cur));
    b->pos_ms = strtoul(f[2], NULL, 10);
    b->done_ms = strtoul(f[3], NULL, 10);
    b->when = strtoul(f[4], NULL, 10);
    field(b->label, f[5], sizeof(b->label));
}

bool sonora_bookmarks_load(void)
{
    char path[MAX_PATH];
    n_bookmarks = 0;
    sonora_data_path("bookmarks.tsv", path, sizeof(path));
    return sonora_tsv_load(path, bookmark_row, NULL);
}

bool sonora_bookmarks_save(void)
{
    char path[MAX_PATH], tmp[MAX_PATH];
    sonora_data_path("bookmarks.tsv", path, sizeof(path));
    int fd = sonora_tsv_begin(path, tmp, sizeof(tmp));
    if (fd < 0)
        return false;
    fdprintf(fd, "# key\tcurrent file\tpos ms\tdone ms\tcreated\tlabel\n");
    for (int i = 0; i < n_bookmarks; i++)
    {
        struct sonora_bookmark *b = &bookmarks[i];
        fdprintf(fd, "%s\t%s\t%lu\t%lu\t%lu\t%s\n", b->key, b->cur,
                 (unsigned long)b->pos_ms, (unsigned long)b->done_ms,
                 (unsigned long)b->when, b->label);
    }
    return sonora_tsv_commit(fd, path, tmp);
}

static int bm_cmp(const void *a, const void *b)
{
    const struct sonora_bookmark *x = &bookmarks[*(const int *)a], *y = &bookmarks[*(const int *)b];
    return x->done_ms < y->done_ms ? -1 : (x->done_ms > y->done_ms ? 1 : 0);
}

int sonora_bookmarks_for(const char *key, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < n_bookmarks && n < max; i++)
        if (!strcmp(bookmarks[i].key, key))
            out[n++] = i;
    qsort(out, n, sizeof(int), bm_cmp);
    return n;
}

struct sonora_bookmark *sonora_bookmark_at(int i)
{
    return (i >= 0 && i < n_bookmarks) ? &bookmarks[i] : NULL;
}

struct sonora_bookmark *sonora_bookmark_add(const char *key)
{
    if (n_bookmarks >= SONORA_MAX_BOOKMARKS)
        return NULL;
    struct sonora_bookmark *b = &bookmarks[n_bookmarks++];
    memset(b, 0, sizeof(*b));
    field(b->key, key, sizeof(b->key));
    b->when = sonora_now();
    return b;
}

void sonora_bookmark_delete(int i)
{
    if (i < 0 || i >= n_bookmarks)
        return;
    memmove(&bookmarks[i], &bookmarks[i + 1], (n_bookmarks - i - 1) * sizeof(bookmarks[0]));
    n_bookmarks--;
}

/* ------------------------------ shows ------------------------------- */

static struct sonora_show shows[SONORA_MAX_SHOWS];
static int n_shows;

static void show_row(char **f, int nf, void *ctx)
{
    (void)ctx;
    if (nf < 5 || n_shows >= SONORA_MAX_SHOWS || !f[0][0])
        return;
    struct sonora_show *s = &shows[n_shows++];
    memset(s, 0, sizeof(*s));
    field(s->folder, f[0], sizeof(s->folder));
    field(s->title, f[1], sizeof(s->title));
    field(s->url, f[2], sizeof(s->url));
    s->speed = atoi(f[3]);
    s->keep = atoi(f[4]);
    if (nf > 5)
        s->last_pub = strtol(f[5], NULL, 10);
}

bool sonora_shows_load(void)
{
    char path[MAX_PATH];
    n_shows = 0;
    sonora_data_path("podcasts.tsv", path, sizeof(path));
    return sonora_tsv_load(path, show_row, NULL);
}

bool sonora_shows_save(void)
{
    char path[MAX_PATH], tmp[MAX_PATH];
    sonora_data_path("podcasts.tsv", path, sizeof(path));
    int fd = sonora_tsv_begin(path, tmp, sizeof(tmp));
    if (fd < 0)
        return false;
    fdprintf(fd, "# folder\ttitle\tfeed\tspeed\tkeep\tnewest offered\n");
    for (int i = 0; i < n_shows; i++)
        fdprintf(fd, "%s\t%s\t%s\t%ld\t%ld\t%ld\n", shows[i].folder, shows[i].title,
                 shows[i].url, (long)shows[i].speed, (long)shows[i].keep, shows[i].last_pub);
    return sonora_tsv_commit(fd, path, tmp);
}

int sonora_shows_count(void) { return n_shows; }

struct sonora_show *sonora_show_at(int i)
{
    return (i >= 0 && i < n_shows) ? &shows[i] : NULL;
}

struct sonora_show *sonora_show_find(const char *folder)
{
    for (int i = 0; i < n_shows; i++)
        if (!strcmp(shows[i].folder, folder))
            return &shows[i];
    return NULL;
}

struct sonora_show *sonora_show_find_url(const char *url)
{
    for (int i = 0; url && url[0] && i < n_shows; i++)
        if (!strcmp(shows[i].url, url))
            return &shows[i];
    return NULL;
}

struct sonora_show *sonora_show_add(const char *folder)
{
    struct sonora_show *s = sonora_show_find(folder);
    if (s)
        return s;
    if (n_shows >= SONORA_MAX_SHOWS)
        return NULL;
    s = &shows[n_shows++];
    memset(s, 0, sizeof(*s));
    field(s->folder, folder, sizeof(s->folder));
    field(s->title, folder, sizeof(s->title));
    return s;
}

/* ----------------------------- downloads ---------------------------- */

static struct sonora_download downloads[SONORA_MAX_DOWNLOADS];
static int n_downloads;

static void download_row(char **f, int nf, void *ctx)
{
    (void)ctx;
    if (nf < 6 || n_downloads >= SONORA_MAX_DOWNLOADS || !f[0][0])
        return;
    struct sonora_download *d = &downloads[n_downloads++];
    memset(d, 0, sizeof(*d));
    field(d->path, f[0], sizeof(d->path));
    d->size = strtoll(f[1], NULL, 10);
    field(d->show, f[2], sizeof(d->show));
    field(d->guid, f[3], sizeof(d->guid));
    d->when = strtoul(f[4], NULL, 10);
    d->pub_epoch = strtol(f[5], NULL, 10);
}

bool sonora_downloads_load(void)
{
    char path[MAX_PATH];
    n_downloads = 0;
    sonora_data_path("downloads.tsv", path, sizeof(path));
    return sonora_tsv_load(path, download_row, NULL);
}

bool sonora_downloads_save(void)
{
    char path[MAX_PATH], tmp[MAX_PATH];
    sonora_data_path("downloads.tsv", path, sizeof(path));
    int fd = sonora_tsv_begin(path, tmp, sizeof(tmp));
    if (fd < 0)
        return false;
    fdprintf(fd, "# path\tsize\tshow\tguid\tdownloaded\tpublished\n");
    for (int i = 0; i < n_downloads; i++)
    {
        struct sonora_download *d = &downloads[i];
        fdprintf(fd, "%s\t%lld\t%s\t%s\t%lu\t%ld\n", d->path, d->size, d->show,
                 d->guid, (unsigned long)d->when, d->pub_epoch);
    }
    return sonora_tsv_commit(fd, path, tmp);
}

int sonora_downloads_count(void) { return n_downloads; }

struct sonora_download *sonora_download_at(int i)
{
    return (i >= 0 && i < n_downloads) ? &downloads[i] : NULL;
}

struct sonora_download *sonora_download_find(const char *path)
{
    for (int i = 0; i < n_downloads; i++)
        if (!strcmp(downloads[i].path, path))
            return &downloads[i];
    return NULL;
}

struct sonora_download *sonora_download_find_guid(const char *guid)
{
    for (int i = 0; guid && guid[0] && i < n_downloads; i++)
        if (!strcmp(downloads[i].guid, guid))
            return &downloads[i];
    return NULL;
}

struct sonora_download *sonora_download_add(const char *path)
{
    struct sonora_download *d = sonora_download_find(path);
    if (d)
        return d;
    if (n_downloads >= SONORA_MAX_DOWNLOADS)
        return NULL;
    d = &downloads[n_downloads++];
    memset(d, 0, sizeof(*d));
    field(d->path, path, sizeof(d->path));
    d->when = sonora_now();
    return d;
}

void sonora_download_forget(const char *path)
{
    for (int i = 0; i < n_downloads; i++)
        if (!strcmp(downloads[i].path, path))
        {
            downloads[i] = downloads[--n_downloads];
            return;
        }
}

bool sonora_download_may_delete(const char *path, const char *managed_dir)
{
    struct sonora_download *d = sonora_download_find(path);
    if (!d || !managed_dir || !managed_dir[0])
        return false;
    size_t l = strlen(managed_dir);
    while (l > 1 && managed_dir[l - 1] == '/')
        l--;
    /* inside the managed folder, never the folder itself, no ".." tricks */
    if (strncmp(path, managed_dir, l) || path[l] != '/' || !path[l + 1] ||
        strstr(path, "/../") || strstr(path, "/./"))
        return false;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    long long size = sonora_filesize(fd);
    close(fd);
    return size == d->size;
}
