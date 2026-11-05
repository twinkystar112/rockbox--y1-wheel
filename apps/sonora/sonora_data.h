/***************************************************************************
 * SONORA - the small text files SONORA keeps in .rockbox/sonora/.
 *
 * Each file starts with "# sonora-format=1", one record per line, fields
 * separated by tabs. Saving writes a temporary file, keeps the previous
 * version as .bak and renames, so a crash leaves either the old or the new
 * file. Nothing here ever touches an audio file.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef SONORA_DATA_H
#define SONORA_DATA_H

#include "sonora_port.h"

#define SONORA_FORMAT_LINE "# sonora-format=1"

/* seconds since 1970, from the player's clock (provided by the caller) */
unsigned long sonora_now(void);

/* where the files live; must exist (created by the caller) */
void sonora_data_init(const char *dir);
const char *sonora_data_dir(void);
void sonora_data_path(const char *name, char *out, size_t len);

/* ----------------------------- progress ----------------------------- */
#define SONORA_MAX_PROGRESS 300
enum { SP_NEW = 'n', SP_PLAYING = 'p', SP_DONE = 'f' };
enum { SK_BOOK_C = 'b', SK_EPISODE_C = 'e', SK_MUSIC_C = 'm' };

struct sonora_progress {
    char key[MAX_PATH];   /* the book (file or folder) or the episode file */
    char cur[MAX_PATH];   /* file being played (books with many files) */
    char title[96];
    char kind;            /* 'b', 'e', or 'm' for the saved music queue */
    char state;           /* 'n' new, 'p' in progress, 'f' finished */
    uint32_t pos_ms;      /* inside cur */
    uint32_t done_ms;     /* on the whole book's timeline */
    uint32_t total_ms;
    int32_t speed;        /* percent x 100 (10000 = 1.0x), 0 = default */
    uint32_t when;        /* last played */
    uint32_t sig;         /* size fingerprint, to find moved files again */
    int32_t index;        /* music snapshot: queue index */
};

bool sonora_progress_load(void);
bool sonora_progress_save(void);
int  sonora_progress_count(void);
struct sonora_progress *sonora_progress_at(int i);
struct sonora_progress *sonora_progress_find(const char *key);
/* find or create (new rows start as 'n') */
struct sonora_progress *sonora_progress_get(const char *key, char kind);
void sonora_progress_remove(const char *key);
/* most recently played first; only rows matching kind (0 = any) and
 * state (0 = any); returns how many indexes were written */
int  sonora_progress_recent(char kind, char state, int *out, int max);

/* ----------------------------- bookmarks ---------------------------- */
#define SONORA_MAX_BOOKMARKS 300
struct sonora_bookmark {
    char key[MAX_PATH];
    char cur[MAX_PATH];
    char label[48];
    uint32_t pos_ms;
    uint32_t done_ms;
    uint32_t when;
};
bool sonora_bookmarks_load(void);
bool sonora_bookmarks_save(void);
int  sonora_bookmarks_for(const char *key, int *out, int max); /* by position */
struct sonora_bookmark *sonora_bookmark_at(int i);
struct sonora_bookmark *sonora_bookmark_add(const char *key);
void sonora_bookmark_delete(int i);

/* ------------------------------ shows ------------------------------- */
#define SONORA_MAX_SHOWS 100
struct sonora_show {
    char folder[96];      /* folder name under the podcasts folder */
    char title[96];
    char url[512];        /* feed, may be empty for local-only shows */
    int32_t speed;        /* percent x 100, 0 = 1.0x */
    int32_t keep;         /* downloads to keep, 0 = all */
    long last_pub;        /* newest episode already offered (feed refresh) */
};
bool sonora_shows_load(void);
bool sonora_shows_save(void);
int  sonora_shows_count(void);
struct sonora_show *sonora_show_at(int i);
struct sonora_show *sonora_show_find(const char *folder);
struct sonora_show *sonora_show_find_url(const char *url);
struct sonora_show *sonora_show_add(const char *folder);

/* ----------------------------- downloads ---------------------------- */
/* The only files SONORA may ever delete are the ones listed here, and
 * only when the size on the card still matches. */
#define SONORA_MAX_DOWNLOADS 500
struct sonora_download {
    char path[MAX_PATH];
    char guid[200];
    char show[96];
    long long size;
    uint32_t when;
    long pub_epoch;
};
bool sonora_downloads_load(void);
bool sonora_downloads_save(void);
int  sonora_downloads_count(void);
struct sonora_download *sonora_download_at(int i);
struct sonora_download *sonora_download_find(const char *path);
struct sonora_download *sonora_download_find_guid(const char *guid);
struct sonora_download *sonora_download_add(const char *path);
void sonora_download_forget(const char *path);
/* true only if path is in the manifest, inside managed_dir, and its size on
 * the card is the recorded size */
bool sonora_download_may_delete(const char *path, const char *managed_dir);

/* ------------------------------ generic ----------------------------- */
typedef void (*sonora_row_cb)(char **f, int nf, void *ctx);
bool sonora_tsv_load(const char *path, sonora_row_cb cb, void *ctx);
int  sonora_tsv_begin(const char *path, char *tmp, size_t tlen);
bool sonora_tsv_commit(int fd, const char *path, const char *tmp);

#endif
