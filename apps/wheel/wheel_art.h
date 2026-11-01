/* Y1 Wheel edition - album art for non-WPS screens. GPLv2+ */
#ifndef WHEEL_ART_H
#define WHEEL_ART_H
#include <stdbool.h>
#include "lcd.h"
#include "screen_access.h"

struct wheel_art {
    struct bitmap bm;
    void *buf;          /* caller-provided pixel buffer */
    int bufsize;
    int max;            /* square box the art is scaled into */
    bool valid;
    unsigned tint;      /* darkened average colour, for backgrounds */
    char path[MAX_PATH];
};

/* room for max*max pixels plus the JPEG decoder's scratch space */
#define WHEEL_ART_INIT(name, size)                                               \
    static long name##_px[((size) * (size) * sizeof(fb_data) + 48*1024) / sizeof(long)]; \
    static struct wheel_art name = { .buf = name##_px,                           \
        .bufsize = sizeof(name##_px), .max = (size) }

bool wheel_art_load(struct wheel_art *a, const char *track_path);
void wheel_art_draw(struct wheel_art *a, struct screen *d, int x, int y, int box);
#endif
