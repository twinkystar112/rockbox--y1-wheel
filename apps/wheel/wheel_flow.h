/* Y1 Wheel edition - Album Flow (a 3D cover carousel) and disc stories.
 * GPLv2+ */
#ifndef WHEEL_FLOW_H
#define WHEEL_FLOW_H
#include <stdbool.h>
#include <stddef.h>

/* besides GO_TO_* values, callbacks and views may return these */
#define WHEEL_FLOW_CHANGED 1000  /* items moved or went away: read them again */
#define WHEEL_VIEW_SWITCH  1001  /* the user picked another playlist view */

struct wheel_flow_text {
    char title[96];
    char sub[96];             /* artist */
    char meta[64];            /* small third line (year, length...) */
};

/* what the cover views show: n items, each with its text and a track
 * whose cover stands for it */
struct wheel_flow_src {
    int n;
    int sel;                  /* in: item to start on; out: the last one */
    const char *heading;      /* small line at the top */
    void *data;
    void (*text)(struct wheel_flow_src *s, int i, struct wheel_flow_text *t);
    bool (*track)(struct wheel_flow_src *s, int i, char *path, size_t len);
    /* centre button: 0 stays in the view, anything else leaves it with that
     * value. stay: the caller wants to remain (disc stories) */
    int  (*ok)(struct wheel_flow_src *s, int i, bool stay);
    /* long press: same convention; may change s->n */
    int  (*options)(struct wheel_flow_src *s, int i);
    /* item playing now or -1; may change s->n (the queue grew) */
    int  (*playing)(struct wheel_flow_src *s);
};

bool wheel_flow_supported(void);
int  wheel_flow_show(struct wheel_flow_src *s);   /* the cover carousel */
int  wheel_disc_show(struct wheel_flow_src *s);   /* one disc per card */
/* main menu: every album in the database */
int  wheel_album_flow(void);

#endif
