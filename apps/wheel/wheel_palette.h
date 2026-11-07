/***************************************************************************
 * Y1 Wheel edition - colour palettes (Settings > Appearance > Palette).
 *
 * The Wheel skins are written in "template" colours (the light theme);
 * a palette maps each of them to its own colour and sets the list colours
 * and the selection gradient. Palette 0 is the classic Wheel look, which
 * follows Appearance (light/dark/auto) and the accent colour.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef WHEEL_PALETTE_H
#define WHEEL_PALETTE_H

#include <stdbool.h>

/* the template colours of wps/Wheel.sbs / .wps, by role */
enum wheel_pal_role {
    PR_BAR,        /* f7f7f9 top bar */
    PR_BARTEXT,    /* 141415 top bar title */
    PR_BARICONS,   /* 3a3a3e top bar icons, battery % */
    PR_TEXT,       /* 141416 main text */
    PR_TEXT2,      /* 6e6e73 secondary text */
    PR_TEXT3,      /* 8e8e93 details */
    PR_ICONS,      /* 3a3a3f icons in the page */
    PR_BG,         /* ffffff list background */
    PR_PANE,       /* f2f2f5 side pane of the main menu */
    PR_DIVIDER,    /* d1d1d6 pane divider */
    PR_HAIRLINE,   /* c8c8ce line under the top bar */
    PR_TILE,       /* cfcfd6 cover placeholder */
    PR_NOTE,       /* fffffe music note on the placeholder */
    PR_TRACK,      /* dcdce1 progress bar track */
    PR_TRACK2,     /* d6d6dc mini progress track */
    PR_SHADOW,     /* e6e6ea cover shadow (outer) */
    PR_SHADOW2,    /* d4d4da cover shadow (inner) */
    PR_COUNT
};

struct wheel_palette {
    const char *name;          /* a proper name, the same in every language */
    int desc_id;               /* LANG_ id of the one-line description */
    bool dark;
    unsigned c[PR_COUNT];      /* 0xRRGGBB */
    unsigned sel_start, sel_end, sel_text;  /* list selection gradient, top -> bottom */
};

/* the "Vinyl" Now Playing layout: where the record is (wps/Wheel-NP-Vinyl.wps
 * puts the round cover label at its centre) */
#define VINYL_CX      142
#define VINYL_CY      199
#define VINYL_R       128
#define VINYL_LABEL_R 76

int wheel_palette_count(void);
/* the palettes after the fixed ones: the cover's, and "My palette" */
int wheel_palette_cover_index(void);
int wheel_palette_mine_index(void);
/* the active palette changes with the music (colour roles, see wheel_gel.h) */
bool wheel_palette_is_dynamic(void);
/* a whole palette from a top bar, page and accent colour (0xRRGGBB) */
void wheel_palette_derive(struct wheel_palette *p, unsigned bar, unsigned bg, unsigned accent);
void wheel_rgb_to_hsv(unsigned c, int *h, int *s, int *v);
unsigned wheel_hsv_to_rgb(int h, int s, int v);
/* called while skins update: follows the cover; true if colours changed */
bool wheel_palette_poll(void);
/* sets the background image if it can be read (false: kept the old one) */
bool wheel_wallpaper_check(const char *path);
/* "My palette" editor */
void wheel_palette_edit(void);
const struct wheel_palette *wheel_palette_get(int i);
/* the palette in use (NULL: classic Wheel colours) */
const struct wheel_palette *wheel_palette_active(void);
/* "f7f7f9" -> the active palette's colour for that template colour, or NULL */
const char *wheel_palette_map(const char *hex6);
/* the same for any word of a skin line (also "accent" etc.), or NULL */
const char *wheel_palette_map_word(const char *w, int len);
/* the top bar colours of the screen drawn in C (any palette) */
void wheel_palette_bar(unsigned *bg, unsigned *fg);
/* sets the list/screen colours of the active palette; true if changed */
bool wheel_palette_patch_colours(void);
/* the classic Wheel colours (light or dark) as a scheme */
const struct wheel_palette *wheel_palette_classic(bool dark);
/* the scheme the skin generator is writing (NULL: the active palette) */
void wheel_palette_use(const struct wheel_palette *p);
/* writes the backdrop of that scheme into wps/<skin>/ (gel bar, background
 * image, the vinyl record); name gets its file name */
bool wheel_palette_backdrop(const char *skin, bool aero, bool vinyl, char *name, size_t len);
/* chooses a palette: theme files, colours, reload */
void wheel_palette_set(int i);
/* the "Pick your palette" screen */
void wheel_palette_screen(void);

#endif
