/***************************************************************************
 * Y1 Wheel edition - font forge: turns a TrueType/OpenType font (or a
 * Rockbox bitmap font) into a Rockbox .fnt with the size, letter spacing,
 * line height, numerals and rendering asked for.
 *
 * Rockbox can only draw pre-rendered bitmap fonts, so every typography
 * choice is "baked" here, on the player, once; the result is cached.
 * Outlines are rasterised with stb_truetype (public domain / MIT).
 *
 * The forge works on memory buffers and a write callback only, so the
 * same code runs in the firmware (hosted builds) and in the PC tool that
 * makes the fonts shipped with the default theme (utils/wheel/typography).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef WHEEL_FONTFORGE_H
#define WHEEL_FONTFORGE_H

#include <stdbool.h>
#include <stddef.h>

/* a font file in memory: .ttf / .otf / .ttc, or a Rockbox .fnt */
struct forge_src {
    const unsigned char *data;
    long size;
};

enum forge_extra {
    FORGE_EXTRA_NONE = 0,
    FORGE_EXTRA_ROW  = 1,   /* list rows: drawn chevron (U+203A) and check (U+2713) */
};

struct forge_params {
    int px;           /* em size in pixels (outline fonts) */
    int band;         /* height of the result = line height, text centred in it */
    int tracking;     /* pixels added to every glyph (negative = tighter) */
    bool tabular;     /* 0-9 all as wide as the widest */
    bool crisp;       /* no anti-aliasing (bitmap look) */
    int embolden;     /* synthetic bold for a missing weight: 0..2 px */
    int contrast;     /* 0..3: darker anti-aliasing for thin strokes */
    unsigned last;    /* highest code point stored (0x7e = ASCII only) */
    int extras;       /* enum forge_extra */
};

struct forge_stats {
    int glyphs;       /* drawn from the font itself */
    int fallback;     /* borrowed from the fallback font */
    int missing;      /* in neither: shown as '?' */
    long bytes;       /* size of the .fnt */
    int height, ascent, maxwidth;
    unsigned last;
};

typedef bool (*forge_write_fn)(void *ctx, const void *buf, size_t len);

/* 0 on success; < 0: unreadable source (-1), no memory (-2), write failed (-3) */
int forge_build(const struct forge_src *src, const struct forge_src *fallback,
                const struct forge_params *p, forge_write_fn write, void *ctx,
                struct forge_stats *st);

enum forge_block {
    FB_LATIN, FB_LATIN1, FB_LATIN_EXT, FB_GREEK, FB_CYRILLIC, FB_PUNCT, FB_COUNT
};

struct forge_info {
    bool outline;          /* TrueType/OpenType; false: Rockbox bitmap font */
    bool variable;         /* variable font: only its default instance is drawn */
    char family[48];       /* outline fonts: from the name table */
    char style[32];
    int weight;            /* 100..900 (OS/2 usWeightClass) */
    int height, ascent;    /* bitmap fonts */
    int cap;               /* bitmap fonts: capital height in pixels */
    int cap1000;           /* outline fonts: capital height per 1000 em */
    int cover[FB_COUNT];   /* glyphs present per block ... */
    int cover_of[FB_COUNT];/* ... out of */
};

bool forge_info(const struct forge_src *src, struct forge_info *info);

/* true if the font has a glyph for this code point */
bool forge_has_glyph(const struct forge_src *src, unsigned cp);

#endif
