/***************************************************************************
 * SONORA - music, audiobooks and podcasts on one playback engine.
 *
 * Everything here runs inside Rockbox and uses its playback engine, queue
 * and DSP. SONORA only adds views and small text files in .rockbox/sonora;
 * it never writes into, renames or deletes the user's audio files (the one
 * exception: podcast downloads SONORA made itself, listed in its manifest).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef SONORA_H
#define SONORA_H

#include <stdbool.h>
#include <stdint.h>
#include "sonora_data.h"
#include "sonora_chapters.h"

enum sonora_kind { SK_MUSIC = 0, SK_BOOK, SK_EPISODE };

/* ------------------------------- core ------------------------------- */
void sonora_init(void);                 /* once, from wheel_init() */
void sonora_tick(void);                 /* often, from get_action() */
void sonora_shutdown(void);             /* from clean_shutdown() */
const char *sonora_books_root(void);    /* real paths, no trailing slash */
void sonora_reload_roots(void);
const char *sonora_podcasts_root(void);
enum sonora_kind sonora_kind_of_path(const char *path);
enum sonora_kind sonora_current_kind(void);

/* ------------------------------ library ----------------------------- */
#define SONORA_MAX_BOOKS 400
#define SONORA_MAX_BOOK_FILES 500

struct sonora_book {
    char path[MAX_PATH];    /* the folder, or the single file */
    char title[96];
    char author[64];
    uint32_t total_ms;
    uint32_t sig;           /* number of files + bytes, to spot changes */
    uint16_t nfiles;
    bool single;            /* one file (usually .m4b) */
};

void sonora_library_invalidate(void);
int  sonora_books_scan(bool show_progress);     /* count */
int  sonora_books_count(void);
struct sonora_book *sonora_book_at(int i);
struct sonora_book *sonora_book_find(const char *path);
struct sonora_book *sonora_book_for_file(const char *file);
/* files of a book in playing order; returns count */
int  sonora_book_files(const struct sonora_book *b, char (*files)[MAX_PATH], int max);

struct sonora_episode {
    char path[MAX_PATH];
    char title[96];
    char show[96];          /* folder name */
    uint32_t mtime;         /* sort key: newest first */
    long long size;
};
int  sonora_shows_scan(void);                   /* merges folders + podcasts.tsv */
int  sonora_show_episodes(const char *folder, struct sonora_episode *out, int max);
int  sonora_new_episodes(struct sonora_episode *out, int max);
void sonora_episode_title(const char *path, char *out, size_t len);
uint32_t sonora_file_length(const char *path);

/* ------------------------------ playback ---------------------------- */
enum { SONORA_RESUME = 0, SONORA_FROM_START, SONORA_AT };
bool sonora_play_book(struct sonora_book *b, int how, const char *file, uint32_t pos_ms);
bool sonora_play_episode(const char *path, int how);
bool sonora_play_episode_at(const char *path, int how, uint32_t at_ms);
bool sonora_resume_music(void);
bool sonora_music_saved(void);
void sonora_save_now(void);
uint32_t sonora_item_speed(const char *key, enum sonora_kind kind);
bool sonora_set_item_speed(const char *key, enum sonora_kind kind, int32_t speed);
const char *sonora_current_key(void);
bool sonora_current_position(char *file, size_t flen, uint32_t *pos, uint32_t *done);
int  sonora_current_file_index(void);
int  sonora_current_chapters(struct sonora_chapter **ch);
bool sonora_seek_chapter(int i);
void sonora_seek(long ms);               /* within the current file */
/* sleep: minutes > 0, or SONORA_SLEEP_CHAPTER / _FILE, 0 = off */
#define SONORA_SLEEP_CHAPTER (-1)
#define SONORA_SLEEP_FILE    (-2)
void sonora_sleep_set(int minutes);
int  sonora_sleep_left(void);                   /* seconds, -1 chapter/file, 0 off */
int  sonora_sleep_mode(void);
void sonora_mark(const char *key, char kind, char state);

/* Now Playing hooks: true = handled, skip the default */
bool sonora_wps_skip(int dir);
bool sonora_wps_hotkey(void);

/* ------------------------------ screens ----------------------------- */
int  sonora_continue_screen(void);
bool sonora_continue_available(void);
int  sonora_books_screen(void);
int  sonora_podcasts_screen(void);
int  sonora_settings_screen(void);
int  sonora_chapters_screen(void);
int  sonora_bookmark_add_current(void);
int  sonora_bookmarks_screen(void);
int  sonora_speed_screen(void);
int  sonora_sleep_screen(void);
int  sonora_mark_current(void);

/* ------------------------------ network ----------------------------- */
/* all of it is off unless the user turns "Network features" on, and then
 * only works where the port can reach the internet (Wi-Fi) */
bool sonora_net_supported(void);
bool sonora_net_begin(void);           /* Wi-Fi on if needed; false = no network */
void sonora_net_end(void);             /* after every begin, even a failed one */
/* blocking fetch to a file; returns HTTP-like status (200 ok), <0 error.
 * progress(done, total) may be NULL; returning false cancels */
int  sonora_net_fetch(const char *url, const char *out_path,
                      bool (*progress)(long long done, long long total, void *ctx),
                      void *ctx);
int  sonora_refresh_screen(void);
int  sonora_cleanup_downloads(bool ask);

#endif
