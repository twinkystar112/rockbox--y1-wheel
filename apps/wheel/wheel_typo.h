/***************************************************************************
 * Y1 Wheel edition - typography & font themes.
 *
 *  Font Registry        every font the player can use (bundled OFL fonts,
 *                       the classic bitmap fonts, fonts the user imports)
 *  Font Role Config     Display / Primary / Secondary / Metadata / Numeric:
 *                       family + weight + size each
 *  Font Theme Manager   presets (fonts/typo/presets.cfg), profiles, apply
 *  Font Fallback        missing glyphs borrowed from Inter at build time;
 *                       missing fonts -> next candidate -> Inter
 *  Profile Persistence  /.rockbox/typography/<profile>.cfg
 *  (Preview renderer and settings screens: wheel_typo_ui.c)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef WHEEL_TYPO_H
#define WHEEL_TYPO_H

#include <stdbool.h>
#include "config.h"
#include "rbpaths.h"

#define TYPO_DIR        ROCKBOX_DIR "/typography"
#define TYPO_ACTIVE     TYPO_DIR "/active.cfg"
#define TYPO_FONTS      FONT_DIR "/typo"
#define TYPO_IMPORT     FONT_DIR "/import"
#define TYPO_CACHE      FONT_DIR "/cache"
#define TYPO_PRESETS    TYPO_FONTS "/presets.cfg"
#define TYPO_FALLBACK   "Inter"

enum typo_role { TR_DISPLAY, TR_PRIMARY, TR_SECONDARY, TR_META, TR_NUMERIC, TR_COUNT };
enum typo_size { TS_XS, TS_S, TS_M, TS_L, TS_XL, TS_COUNT };
#define TS_AUTO (-1)

/* the fonts a profile turns into (one .fnt each) */
enum typo_use {
    TU_DISPLAY_MENU,  /* title bar */
    TU_DISPLAY_NP,    /* Now Playing title, big headers */
    TU_ROW,           /* list rows */
    TU_ROWSEL,        /* selected list row (one weight up) */
    TU_UI,            /* Rockbox UI font (screens without a theme font) */
    TU_SECONDARY,     /* artist / album */
    TU_META,          /* small grey details */
    TU_NUMERIC,       /* times, counts, battery */
    TU_LYRICS,        /* current lyric line */
    TU_JUMBO,         /* letter jump, Wrapped numbers */
    TU_COUNT
};

struct typo_role_cfg {
    char family[96];   /* candidates, best first: "SF Pro Text, Inter" */
    short weight;      /* 100..900 */
    signed char size;  /* enum typo_size */
};

struct typo_profile {
    char id[32];       /* preset id or profile file name (no .cfg) */
    char name[32];
    char note[64];     /* presets: where the fonts come from */
    bool preset;
    struct typo_role_cfg role[TR_COUNT];
    signed char spacing;    /* -1 compact, 0 normal, 1 wide */
    signed char line;       /* line spacing style: enum typo_line_style */
    short row_h;            /* menus: list row height in pixels */
    short ly_pct;           /* lyrics line spacing, % */
    short np_pct;           /* Now Playing text block spacing, % */
    short list_pct;         /* two-line lists (playlists, albums), % */
    signed char render;     /* 0 smooth, 1 sharp, 2 pixel (no anti-aliasing) */
    bool tabular;           /* tabular figures */
    bool ui_font;           /* Primary also becomes the UI font of other themes */
    bool fallback;          /* fill missing characters from Inter */
    signed char ov[3];      /* per-screen title size: menus, now playing, lyrics (TS_AUTO) */
};
enum { TOV_MENUS, TOV_NOWPLAYING, TOV_LYRICS };

/* line spacing styles: each sets the four values below; changing one of
 * them makes the style "custom" */
enum typo_line_style { TLS_COMPACT, TLS_NORMAL, TLS_COMFORT, TLS_AIRY, TLS_CUSTOM, TLS_COUNT };
void typo_line_style_apply(struct typo_profile *p, int style);
#define TYPO_ROW_MIN 28
#define TYPO_ROW_MAX 60

/* ------------------------------ registry ------------------------------ */
struct typo_face {
    char family[40];
    char path[96];     /* full path (/.rockbox/fonts/...) */
    long bytes;
    short weight;
    short height, cap; /* bitmap fonts */
    short cap1000;     /* outline fonts: capital height per 1000 em */
    bool outline;
    bool imported;
    bool variable;
};

int  typo_registry_scan(bool force);         /* number of faces */
int  typo_face_count(void);
const struct typo_face *typo_face(int i);
int  typo_family_list(const char **names, int max); /* distinct families, sorted */
int  typo_family_weights(const char *family, short *weights, int max);
bool typo_family_exists(const char *family);
/* first available family of a candidate list ("A, B, C"); NULL if none */
const char *typo_resolve_family(const char *candidates, char *buf, int len);

/* ------------------------------ profiles ------------------------------ */
int  typo_preset_count(void);
bool typo_preset_get(int i, struct typo_profile *out);
int  typo_profile_list(char names[][32], int max);   /* saved profiles (ids) */
bool typo_profile_load(const char *id, struct typo_profile *out);
bool typo_profile_save(const struct typo_profile *p);  /* to TYPO_DIR/<id>.cfg */
bool typo_profile_delete(const char *id);
bool typo_profile_rename(const char *id, const char *new_name);
void typo_profile_default(struct typo_profile *out); /* the stock look */
const char *typo_slug(const char *name, char *buf, int len);

/* the profile in use (stock look if none was applied) */
const struct typo_profile *typo_active(void);
bool typo_is_custom(void);   /* a profile was applied (not the stock fonts) */

/* ------------------------------- build ------------------------------- */
struct typo_build_info {
    char file[TU_COUNT][64];   /* relative to FONT_DIR */
    long bytes[TU_COUNT];
    int glyphs, fallback, missing;
    int built, cached;
    char face[TR_COUNT][40];   /* family actually used per role */
    bool emboldened[TR_COUNT];
};

/* make (or find in the cache) every font of a profile. progress may be NULL */
bool typo_build(const struct typo_profile *p, struct typo_build_info *bi,
                void (*progress)(int done, int total));
/* one font of a profile; file relative to FONT_DIR */
bool typo_build_one(const struct typo_profile *p, enum typo_use u, char *file, int len);
/* build + theme files + settings: the profile becomes the active one */
bool typo_apply(const struct typo_profile *p);
/* back to the stock fonts */
void typo_restore_default(void);
/* writes wps/Wheel-Pal.sbs/.wps: the Wheel skins in the active palette's
 * colours, with the active typography */
bool typo_generate_palette(void);

/* called after a Wheel theme config was loaded (light/dark switch):
 * points the theme at the generated files of the active profile */
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
void typo_patch_settings(void);
void typo_init(void);
#endif
bool typo_patch_settings_changed(void);   /* same, true if a setting changed */
int  typo_row_height(const struct typo_profile *p);
void typo_np_positions(int pct, int y[5]);
/* boot: fall back to the stock theme if generated files went missing
 * (typo_init, declared above) */

/* font id of a role for screens drawn in C (lyrics, stats, playlists...);
 * FONT_UI if it can't be loaded. Don't keep the id across screens. */
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
int  typo_font(enum typo_use u);
void typo_fonts_unload(void);
#else
#include "font.h"
static inline int typo_font(enum typo_use u) { (void)u; return FONT_UI; }
static inline void typo_fonts_unload(void) { }
static inline void typo_patch_settings(void) { }
static inline void typo_init(void) { }
static inline void typo_update_fallbacks(bool force) { (void)force; }
#endif

/* Chinese, Japanese and Korean (and Latin/Greek/Cyrillic) glyph fallback
 * fonts, ordered for the UI language; call when the language changes */
void typo_update_fallbacks(bool force);
int  typo_cjk_fonts(void);   /* how many CJK fallback fonts are installed */

/* sizes */
int  typo_em(enum typo_role r, int size);
const char *typo_size_name(int size);
long typo_cache_bytes(int *files);
void typo_cache_clear(void);
long typo_loaded_bytes(int *fonts);

#endif
