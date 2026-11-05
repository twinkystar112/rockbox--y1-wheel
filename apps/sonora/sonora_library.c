/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - the audiobook and podcast libraries, built from folders.
 *
 * Audiobooks folder:
 *   Book.m4b                  one file = one book
 *   Book/01.mp3 02.mp3 ...    one folder = one book (files in natural order)
 *   Author/Book/...           an author folder holding books
 *   Author/Book.m4b
 * Podcasts folder: one folder per show, episodes inside.
 *
 * Titles, authors and lengths come from the tags (else the names) and are
 * cached in .rockbox/sonora/books.tsv, keyed by a size fingerprint so a
 * changed book is read again. Only directory listings and tags are read.
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
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "dir.h"
#include "file.h"
#include "lang.h"
#include "splash.h"
#include "metadata.h"
#include "string-extra.h"
#include "sonora.h"
#include "sonora_util.h"

static struct sonora_book books[SONORA_MAX_BOOKS];
static int n_books;
static bool books_valid;

void sonora_library_invalidate(void)
{
    books_valid = false;
}

int sonora_books_count(void) { return n_books; }

struct sonora_book *sonora_book_at(int i)
{
    return (i >= 0 && i < n_books) ? &books[i] : NULL;
}

struct sonora_book *sonora_book_find(const char *path)
{
    for (int i = 0; i < n_books; i++)
        if (!strcmp(books[i].path, path))
            return &books[i];
    return NULL;
}

struct sonora_book *sonora_book_for_file(const char *file)
{
    if (!books_valid)
        sonora_books_scan(false);
    struct sonora_book *best = NULL;
    size_t best_len = 0;
    for (int i = 0; i < n_books; i++)
    {
        size_t l = strlen(books[i].path);
        if (!strcmp(books[i].path, file))
            return &books[i];
        if (!books[i].single && !strncmp(books[i].path, file, l) && file[l] == '/' &&
            l > best_len)
        {
            best = &books[i];
            best_len = l;
        }
    }
    return best;
}

/* ------------------------------- cache ------------------------------- */

struct cached { char path[MAX_PATH]; char title[96]; char author[64]; uint32_t total; uint32_t sig; };
static struct cached cache[SONORA_MAX_BOOKS];
static int n_cache;

static void cache_row(char **f, int nf, void *ctx)
{
    (void)ctx;
    if (nf < 5 || n_cache >= SONORA_MAX_BOOKS)
        return;
    struct cached *c = &cache[n_cache++];
    strlcpy(c->path, f[0], sizeof(c->path));
    c->sig = strtoul(f[1], NULL, 10);
    c->total = strtoul(f[2], NULL, 10);
    strlcpy(c->title, f[3], sizeof(c->title));
    strlcpy(c->author, f[4], sizeof(c->author));
}

static void cache_save(void)
{
    char path[MAX_PATH], tmp[MAX_PATH];
    sonora_data_path("books.tsv", path, sizeof(path));
    int fd = sonora_tsv_begin(path, tmp, sizeof(tmp));
    if (fd < 0)
        return;
    fdprintf(fd, "# path\tfingerprint\ttotal ms\ttitle\tauthor  (a cache: safe to delete)\n");
    for (int i = 0; i < n_books; i++)
        fdprintf(fd, "%s\t%lu\t%lu\t%s\t%s\n", books[i].path, (unsigned long)books[i].sig,
                 (unsigned long)books[i].total_ms, books[i].title, books[i].author);
    sonora_tsv_commit(fd, path, tmp);
}

/* ------------------------------- tags -------------------------------- */

static struct mp3entry tag_id3;

static bool read_tags(const char *file)
{
    int fd = open(file, O_RDONLY);
    if (fd < 0)
        return false;
    memset(&tag_id3, 0, sizeof(tag_id3));
    bool ok = get_metadata(&tag_id3, fd, file);
    close(fd);
    return ok;
}

uint32_t sonora_file_length(const char *path)
{
    return read_tags(path) ? (uint32_t)tag_id3.length : 0;
}

void sonora_episode_title(const char *path, char *out, size_t len)
{
    if (read_tags(path) && tag_id3.title && tag_id3.title[0])
        sonora_clean(out, tag_id3.title, len);
    else
        sonora_basename_noext(path, out, len);
}

/* --------------------------------- scan ------------------------------- */

/* audio files directly inside dir: count and a size fingerprint */
static int audio_in(const char *dir, uint32_t *sig)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int n = 0;
    uint32_t s = 0;
    struct dirent *e;
    while ((e = readdir(d)))
    {
        struct dirinfo info = dir_get_info(d, e);
        if (!(info.attribute & ATTR_DIRECTORY) && e->d_name[0] != '.' &&
            sonora_is_audio(e->d_name))
        {
            n++;
            s = s * 31u + (uint32_t)info.size;
        }
    }
    closedir(d);
    if (sig)
        *sig = s + (uint32_t)n;
    return n;
}

static void fill_book(struct sonora_book *b, const char *author_dir, bool progress)
{
    for (int i = 0; i < n_cache; i++)
        if (cache[i].sig == b->sig && !strcmp(cache[i].path, b->path))
        {
            strlcpy(b->title, cache[i].title, sizeof(b->title));
            strlcpy(b->author, cache[i].author, sizeof(b->author));
            b->total_ms = cache[i].total;
            return;
        }
    if (progress)
    {
        const char *slash = strrchr(b->path, '/');
        splashf(0, "%s %s", str(LANG_SONORA_SCANNING), slash ? slash + 1 : b->path);
    }
    static char files[SONORA_MAX_BOOK_FILES][MAX_PATH];
    int n = sonora_book_files(b, files, SONORA_MAX_BOOK_FILES);
    b->total_ms = 0;
    b->title[0] = b->author[0] = '\0';
    for (int i = 0; i < n; i++)
    {
        if (!read_tags(files[i]))
            continue;
        b->total_ms += tag_id3.length;
        if (i == 0)
        {
            const char *album = tag_id3.album;
            const char *who = tag_id3.albumartist && tag_id3.albumartist[0]
                              ? tag_id3.albumartist : tag_id3.artist;
            if (b->single && (!album || !album[0]))
                album = tag_id3.title;
            if (album && album[0])
                sonora_clean(b->title, album, sizeof(b->title));
            if (who && who[0])
                sonora_clean(b->author, who, sizeof(b->author));
        }
        if (progress && i % 10 == 9)
            yield();
    }
    if (!b->title[0])
        sonora_basename_noext(b->path, b->title, sizeof(b->title));
    if (!b->author[0] && author_dir)
        sonora_clean(b->author, author_dir, sizeof(b->author));
}

static void add_book(const char *path, bool single, uint32_t sig, int nfiles,
                     const char *author_dir, bool progress)
{
    if (n_books >= SONORA_MAX_BOOKS)
        return;
    struct sonora_book *b = &books[n_books++];
    memset(b, 0, sizeof(*b));
    strlcpy(b->path, path, sizeof(b->path));
    b->single = single;
    b->sig = sig;
    b->nfiles = (uint16_t)nfiles;
    fill_book(b, author_dir, progress);
}

static void scan_dir(const char *dir, const char *author, int depth, bool progress)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    static char sub[2][MAX_PATH];
    char *path = sub[depth & 1];
    while ((e = readdir(d)) && n_books < SONORA_MAX_BOOKS)
    {
        if (e->d_name[0] == '.')
            continue;
        struct dirinfo info = dir_get_info(d, e);
        snprintf(path, MAX_PATH, "%s/%s", dir, e->d_name);
        if (info.attribute & ATTR_DIRECTORY)
        {
            uint32_t sig = 0;
            int n = audio_in(path, &sig);
            if (n > 0)
                add_book(path, false, sig, n, author, progress);
            else if (depth == 0)
            {   /* an author folder */
                char who[64];
                strlcpy(who, e->d_name, sizeof(who));
                char keep[MAX_PATH];
                strlcpy(keep, path, sizeof(keep));
                scan_dir(keep, who, 1, progress);
            }
        }
        else if (sonora_is_audio(e->d_name))
            add_book(path, true, (uint32_t)info.size + 1, 1, author, progress);
    }
    closedir(d);
}

static int book_cmp(const void *a, const void *b)
{
    const struct sonora_book *x = a, *y = b;
    int c = sonora_natcmp(x->author, y->author);
    return c ? c : sonora_natcmp(x->title, y->title);
}

int sonora_books_scan(bool show_progress)
{
    if (books_valid)
        return n_books;
    char path[MAX_PATH];
    n_cache = 0;
    sonora_data_path("books.tsv", path, sizeof(path));
    sonora_tsv_load(path, cache_row, NULL);
    n_books = 0;
    scan_dir(sonora_books_root(), NULL, 0, show_progress);
    qsort(books, n_books, sizeof(books[0]), book_cmp);
    books_valid = true;
    cache_save();
    return n_books;
}

/* ------------------------------ book files ---------------------------- */

static int path_cmp(const void *a, const void *b)
{
    const char *x = strrchr((const char *)a, '/'), *y = strrchr((const char *)b, '/');
    return sonora_natcmp(x ? x + 1 : a, y ? y + 1 : b);
}

int sonora_book_files(const struct sonora_book *b, char (*files)[MAX_PATH], int max)
{
    if (b->single)
    {
        if (max < 1)
            return 0;
        strlcpy(files[0], b->path, MAX_PATH);
        return 1;
    }
    DIR *d = opendir(b->path);
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max)
    {
        struct dirinfo info = dir_get_info(d, e);
        if (!(info.attribute & ATTR_DIRECTORY) && e->d_name[0] != '.' &&
            sonora_is_audio(e->d_name))
            snprintf(files[n++], MAX_PATH, "%s/%s", b->path, e->d_name);
    }
    closedir(d);
    qsort(files, n, MAX_PATH, path_cmp);
    return n;
}

/* ------------------------------- podcasts ----------------------------- */

int sonora_shows_scan(void)
{
    DIR *d = opendir(sonora_podcasts_root());
    bool changed = false;
    if (d)
    {
        struct dirent *e;
        while ((e = readdir(d)))
        {
            if (e->d_name[0] == '.')
                continue;
            struct dirinfo info = dir_get_info(d, e);
            if ((info.attribute & ATTR_DIRECTORY) && !sonora_show_find(e->d_name))
            {
                char p[MAX_PATH];
                snprintf(p, sizeof(p), "%s/%s", sonora_podcasts_root(), e->d_name);
                if (audio_in(p, NULL) > 0 && sonora_show_add(e->d_name))
                    changed = true;
            }
        }
        closedir(d);
    }
    if (changed)
        sonora_shows_save();
    return sonora_shows_count();
}

static int ep_cmp(const void *a, const void *b)
{
    const struct sonora_episode *x = a, *y = b;
    if (x->mtime != y->mtime)
        return x->mtime > y->mtime ? -1 : 1;
    return -sonora_natcmp(x->path, y->path);
}

static int list_show(const char *folder, struct sonora_episode *out, int max)
{
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s/%s", sonora_podcasts_root(), folder);
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max)
    {
        struct dirinfo info = dir_get_info(d, e);
        if ((info.attribute & ATTR_DIRECTORY) || e->d_name[0] == '.' ||
            !sonora_is_audio(e->d_name))
            continue;
        struct sonora_episode *ep = &out[n++];
        snprintf(ep->path, sizeof(ep->path), "%s/%s", dir, e->d_name);
        strlcpy(ep->show, folder, sizeof(ep->show));
        ep->mtime = (uint32_t)info.mtime;
        ep->size = info.size;
        /* the manifest knows when it was published */
        struct sonora_download *dl = sonora_download_find(ep->path);
        if (dl && dl->pub_epoch > 0)
            ep->mtime = (uint32_t)dl->pub_epoch;
        sonora_basename_noext(ep->path, ep->title, sizeof(ep->title));
    }
    closedir(d);
    return n;
}

int sonora_show_episodes(const char *folder, struct sonora_episode *out, int max)
{
    int n = list_show(folder, out, max);
    qsort(out, n, sizeof(*out), ep_cmp);
    return n;
}

int sonora_new_episodes(struct sonora_episode *out, int max)
{
    int n = 0;
    sonora_shows_scan();
    for (int s = 0; s < sonora_shows_count() && n < max; s++)
    {
        int k = list_show(sonora_show_at(s)->folder, out + n, max - n);
        /* keep only the ones not played to the end */
        for (int i = 0; i < k; i++)
        {
            struct sonora_progress *p = sonora_progress_find(out[n + i].path);
            if (p && p->state == SP_DONE)
            {
                out[n + i] = out[n + k - 1];
                k--;
                i--;
            }
        }
        n += k;
    }
    qsort(out, n, sizeof(*out), ep_cmp);
    return n;
}
