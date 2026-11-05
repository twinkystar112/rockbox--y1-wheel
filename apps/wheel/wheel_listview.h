/* Y1 Wheel edition - the playlist views. GPLv2+ */
#ifndef WHEEL_LISTVIEW_H
#define WHEEL_LISTVIEW_H
#include <stdbool.h>
#include <stddef.h>
#include "wheel_library.h"

enum wheel_lv_kind { LV_MIX, LV_PLAYLIST_FILE, LV_QUEUE };

struct wheel_lv {
    const char *title;
    const char *subtitle;     /* small line above the title */
    enum wheel_lv_kind kind;
    const char *file;         /* LV_PLAYLIST_FILE */
    struct wheel_list *list;
    bool fixed;               /* always the Wheel list (albums, folders) */
};

/* how playlists, mixes and Up Next are shown (Settings > Appearance) */
enum { PLV_WHEEL, PLV_CLASSIC, PLV_FLOW, PLV_DISC, PLV_COUNT };
const char *wheel_playlist_view_name(int view);
/* asks; true when the user picked a different view */
bool wheel_pick_playlist_view(void);

/* the shared big list (cleared) for building mixes */
struct wheel_list *wheel_lv_list(void);

/* all return GO_TO_WPS when playback started, GO_TO_ROOT / GO_TO_FILEBROWSER
 * when the user went somewhere else, 0 when they just backed out */
int  wheel_listview_show(struct wheel_lv *lv);
int  wheel_queue_view(void);
int  wheel_open_playlist_file(const char *file);
int  wheel_playlists_screen(void);
int  wheel_open_folder(const char *dir, const char *title, const char *subtitle);

bool wheel_play_list(struct wheel_list *l, int start, bool shuffle);
int  wheel_load_playlist_file(const char *file, struct wheel_list *l);
bool wheel_playlist_cached(const char *file, int *count, unsigned long *secs);
void wheel_format_duration(unsigned long secs, char *buf, size_t len);
int  wheel_pick(const char *title, const char * const *items, int n, int sel);

#endif
