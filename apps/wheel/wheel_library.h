/* Y1 Wheel edition - mixes built from the music database. GPLv2+ */
#ifndef WHEEL_LIBRARY_H
#define WHEEL_LIBRARY_H
#include <stdbool.h>
#include "file.h"


struct wheel_track {
    char path[MAX_PATH];
    char title[96];
    char artist[64];
    unsigned long len_ms;
    bool meta;        /* title/artist/len filled */
};

struct wheel_list {
    int n, max;
    int total;                 /* entries in the source (may exceed max) */
    struct wheel_track *t;
};
#define WHEEL_LIST_DEFINE(name, cap) \
    static struct wheel_track name##_t[cap]; \
    static struct wheel_list name = { .n = 0, .max = (cap), .t = name##_t }

enum wheel_mood {
    MOOD_ENERGY = 0, MOOD_CALM, MOOD_PARTY, MOOD_FOCUS, MOOD_MELANCHOLY, MOOD_COUNT
};

enum wheel_rule_kind {
    RULE_ANY = 0, RULE_NEVER_PLAYED, RULE_FORGOTTEN, RULE_RECENT, RULE_FAVOURITES
};

struct wheel_rule {
    enum wheel_rule_kind kind;
    int family;      /* genre family, -1 = any */
    int decade;      /* e.g. 1990, 0 = any */
    int min_len_s;   /* 0 = no limit */
    int max_len_s;   /* 0 = no limit */
};

/* genre families shown in the rule builder */
int  wheel_family_count(void);
const char *wheel_family_name(int fam);

bool wheel_mix_similar(const char *seed_path, struct wheel_list *out);
bool wheel_mix_mood(enum wheel_mood mood, unsigned long max_ms, struct wheel_list *out);
bool wheel_mix_rule(const struct wheel_rule *r, struct wheel_list *out);
bool wheel_mix_daily(struct wheel_list *out);
int  wheel_library_count(void);
int  wheel_library_decades(int *out, int max);
int  wheel_library_families(int *out, int max);
/* fill title/artist/length from the database in one pass */
void wheel_list_fill_meta(struct wheel_list *l);
/* one track from its tags (slow: opens the file) */
void wheel_track_fill_meta(struct wheel_track *t);
void wheel_library_invalidate(void);

#endif
