/***************************************************************************
 * Y1 Wheel edition - typography & font themes (engine; see wheel_typo.h).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "file.h"
#include "dir.h"
#include "font.h"
#include "lang.h"
#include "settings.h"
#include "splash.h"
#include "misc.h"
#include "string-extra.h"
#include "pathfuncs.h"
#include "rbpaths.h"
#include "wheel_fontforge.h"
#include "wheel_typo.h"
#include "wheel_themes.h"
#include "wheel_screens.h"
#include "wheel_palette.h"

#define FORGE_VERSION 4          /* bump when the forge output changes */
#define MAX_FACES 96
#define MAX_PRESETS 12

/* ------------------------------ helpers ------------------------------ */

static char *read_all(const char *path, long *size, long max)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    long n = ffilesize(fd);
    if (n <= 0 || n > max)
    {
        close(fd);
        return NULL;
    }
    char *buf = malloc(n + 1);
    if (buf && read(fd, buf, n) != n)
    {
        free(buf);
        buf = NULL;
    }
    close(fd);
    if (buf)
    {
        buf[n] = '\0';
        if (size) *size = n;
    }
    return buf;
}

static bool exists(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    close(fd);
    return true;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return s;
}

static unsigned long fnv(unsigned long h, const void *d, size_t n)
{
    const unsigned char *p = d;
    while (n--)
        h = (h ^ *p++) * 16777619UL;
    return h & 0xffffffffUL;
}

/* ------------------------------ registry ------------------------------ */

static struct typo_face faces[MAX_FACES];
static int n_faces;
static bool scanned;

static void family_from_name(const char *base, char *out, int len, short *weight)
{
    /* "ClassicBitmap-700-21" (ours) or "18-Nimbus-Bold" (Rockbox fonts) */
    if (!strncmp(base, "ClassicBitmap-", 14))
    {
        strlcpy(out, "Classic Bitmap", len);
        *weight = atoi(base + 14);
        return;
    }
    const char *s = base;
    while (isdigit((unsigned char)*s)) s++;
    if (*s == '-' && s != base) s++;
    char tmp[64];
    strlcpy(tmp, s, sizeof(tmp));
    *weight = 400;
    char *b = strstr(tmp, "-Bold");
    if (b) { *b = '\0'; *weight = 700; }
    for (char *p = tmp; *p; p++)
        if (*p == '-' || *p == '_') *p = ' ';
    strlcpy(out, trim(tmp), len);
}

static void add_face(const char *dir, const char *base, const char *ext, bool imported)
{
    if (n_faces >= MAX_FACES)
        return;
    struct typo_face *f = &faces[n_faces];
    snprintf(f->path, sizeof(f->path), "%s/%s%s", dir, base, ext);
    for (int i = 0; i < n_faces; i++)
        if (!strcmp(faces[i].path, f->path))
            return;
    long size = 0;
    char *data = read_all(f->path, &size, 8L << 20);
    if (!data)
        return;
    struct forge_src src = { (const unsigned char *)data, size };
    struct forge_info fi;
    bool ok = forge_info(&src, &fi);
    free(data);
    if (!ok)
        return;
    f->bytes = size;
    f->outline = fi.outline;
    f->variable = fi.variable;
    f->imported = imported;
    f->height = fi.height;
    f->cap = fi.cap;
    f->cap1000 = fi.cap1000;
    if (fi.outline && fi.family[0])
    {
        strlcpy(f->family, fi.family, sizeof(f->family));
        f->weight = fi.weight;
    }
    else
    {
        family_from_name(base, f->family, sizeof(f->family), &f->weight);
        if (fi.outline)
            f->weight = fi.weight;
    }
    if (!f->family[0])
        return;
    n_faces++;
}

static void scan_dir(const char *dir, bool imported)
{
    static const char * const exts[] = { ".ttf", ".otf", ".fnt" };
    static char names[64][64];
    for (unsigned e = 0; e < ARRAYLEN(exts); e++)
    {
        int n = wheel_dir_list(dir, exts[e], names, 64);
        for (int i = 0; i < n; i++)
            add_face(dir, names[i], exts[e], imported);
    }
}

int typo_registry_scan(bool force)
{
    if (scanned && !force)
        return n_faces;
    n_faces = 0;
    scan_dir(TYPO_FONTS, false);
    scan_dir(TYPO_IMPORT, true);
    scanned = true;
    return n_faces;
}

int typo_face_count(void) { typo_registry_scan(false); return n_faces; }
const struct typo_face *typo_face(int i) { return i >= 0 && i < n_faces ? &faces[i] : NULL; }

bool typo_family_exists(const char *family)
{
    typo_registry_scan(false);
    for (int i = 0; i < n_faces; i++)
        if (!strcasecmp(faces[i].family, family))
            return true;
    return false;
}

int typo_family_list(const char **names, int max)
{
    typo_registry_scan(false);
    int n = 0;
    for (int i = 0; i < n_faces && n < max; i++)
    {
        int k;
        for (k = 0; k < n && strcasecmp(names[k], faces[i].family); k++);
        if (k == n)
            names[n++] = faces[i].family;
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--)
        {
            const char *t = names[j]; names[j] = names[j - 1]; names[j - 1] = t;
        }
    return n;
}

int typo_family_weights(const char *family, short *weights, int max)
{
    typo_registry_scan(false);
    int n = 0;
    for (int i = 0; i < n_faces && n < max; i++)
    {
        if (strcasecmp(faces[i].family, family))
            continue;
        int k;
        for (k = 0; k < n && weights[k] != faces[i].weight; k++);
        if (k == n)
            weights[n++] = faces[i].weight;
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && weights[j - 1] > weights[j]; j--)
        {
            short t = weights[j]; weights[j] = weights[j - 1]; weights[j - 1] = t;
        }
    return n;
}

const char *typo_resolve_family(const char *candidates, char *buf, int len)
{
    typo_registry_scan(false);
    char tmp[96];
    strlcpy(tmp, candidates, sizeof(tmp));
    char *save = NULL;
    for (char *t = strtok_r(tmp, ",", &save); t; t = strtok_r(NULL, ",", &save))
    {
        t = trim(t);
        for (int i = 0; i < n_faces; i++)
            if (!strcasecmp(faces[i].family, t))
            {
                strlcpy(buf, faces[i].family, len);
                return buf;
            }
    }
    return NULL;
}

/* nearest weight; among bitmap sizes of it, the nearest capital height */
static int pick_face(const char *family, int weight, float cap, bool outline_only)
{
    int best = -1, best_dw = 1 << 20;
    for (int i = 0; i < n_faces; i++)
    {
        const struct typo_face *f = &faces[i];
        if (strcasecmp(f->family, family) || (outline_only && !f->outline))
            continue;
        int dw = abs(f->weight - weight) * 2 + (f->weight < weight); /* ties: heavier */
        if (dw < best_dw ||
            (dw == best_dw && !f->outline && best >= 0 &&
             abs(f->cap * 4 - (int)(cap * 4)) < abs(faces[best].cap * 4 - (int)(cap * 4))))
        {
            best = i;
            best_dw = dw;
        }
    }
    return best;
}

/* ------------------------------ profiles ------------------------------ */

static const char * const size_names[TS_COUNT] = { "XS", "S", "M", "L", "XL" };
static const char * const role_keys[TR_COUNT] = { "display", "primary", "secondary", "metadata", "numeric" };
static const char * const ov_keys[3] = { "menus title", "now playing title", "lyrics" };

const char *typo_size_name(int size)
{
    return size >= 0 && size < TS_COUNT ? size_names[size] : "Auto";
}

static int parse_size(const char *s)
{
    for (int i = 0; i < TS_COUNT; i++)
        if (!strcasecmp(s, size_names[i]))
            return i;
    return TS_AUTO;
}

void typo_profile_default(struct typo_profile *p)
{
    memset(p, 0, sizeof(*p));
    strcpy(p->id, "default");
    strcpy(p->name, "Wheel");
    p->preset = true;
    static const short w[TR_COUNT] = { 700, 500, 500, 400, 500 };
    for (int r = 0; r < TR_COUNT; r++)
    {
        strcpy(p->role[r].family, "Inter");
        p->role[r].weight = w[r];
        p->role[r].size = TS_M;
    }
    p->tabular = true;
    p->fallback = true;
    p->ov[0] = p->ov[1] = p->ov[2] = TS_AUTO;
    typo_line_style_apply(p, TLS_NORMAL);
}

static const struct { short row, ly, np, list; } line_styles[TLS_CUSTOM] = {
    { 34, 115,  85,  90 },   /* compact */
    { 40, 135, 100, 100 },   /* normal */
    { 46, 155, 112, 110 },   /* comfortable */
    { 52, 180, 125, 125 },   /* airy */
};
static const char * const line_style_keys[TLS_COUNT] = {
    "compact", "normal", "comfortable", "airy", "custom" };

void typo_line_style_apply(struct typo_profile *p, int style)
{
    p->line = style;
    if (style < 0 || style >= TLS_CUSTOM)
        return;
    p->row_h = line_styles[style].row;
    p->ly_pct = line_styles[style].ly;
    p->np_pct = line_styles[style].np;
    p->list_pct = line_styles[style].list;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void parse_kv(struct typo_profile *p, const char *k, char *v)
{
    for (int r = 0; r < TR_COUNT; r++)
        if (!strcasecmp(k, role_keys[r]))
        {   /* "Family, Fallback | 700 | M" */
            char *a = strchr(v, '|');
            char *b = a ? strchr(a + 1, '|') : NULL;
            if (a) *a++ = '\0';
            if (b) *b++ = '\0';
            strlcpy(p->role[r].family, trim(v), sizeof(p->role[r].family));
            if (a) p->role[r].weight = atoi(trim(a));
            if (b) { int s = parse_size(trim(b)); p->role[r].size = s == TS_AUTO ? TS_M : s; }
            return;
        }
    for (int o = 0; o < 3; o++)
        if (!strcasecmp(k, ov_keys[o]))
        {
            p->ov[o] = parse_size(v);
            return;
        }
    if (!strcasecmp(k, "name")) strlcpy(p->name, v, sizeof(p->name));
    else if (!strcasecmp(k, "note")) strlcpy(p->note, v, sizeof(p->note));
    else if (!strcasecmp(k, "id")) strlcpy(p->id, v, sizeof(p->id));
    else if (!strcasecmp(k, "spacing"))
        p->spacing = !strcasecmp(v, "compact") ? -1 : !strcasecmp(v, "wide") ? 1 : 0;
    else if (!strcasecmp(k, "line height"))
    {   /* "relaxed" is the old name of "comfortable" */
        int st = TLS_NORMAL;
        for (int i = 0; i < TLS_COUNT; i++)
            if (!strcasecmp(v, line_style_keys[i])) st = i;
        if (!strcasecmp(v, "relaxed")) st = TLS_COMFORT;
        if (st == TLS_CUSTOM) p->line = TLS_CUSTOM;
        else typo_line_style_apply(p, st);
    }
    else if (!strcasecmp(k, "menu rows"))
        p->row_h = clampi(atoi(v), TYPO_ROW_MIN, TYPO_ROW_MAX) & ~1;
    else if (!strcasecmp(k, "lyrics spacing"))
        p->ly_pct = clampi(atoi(v), 100, 220);
    else if (!strcasecmp(k, "now playing spacing"))
        p->np_pct = clampi(atoi(v), 75, 140);
    else if (!strcasecmp(k, "list spacing"))
        p->list_pct = clampi(atoi(v), 80, 150);
    else if (!strcasecmp(k, "numerals"))
        p->tabular = strcasecmp(v, "proportional") != 0;
    else if (!strcasecmp(k, "rendering"))
        p->render = !strcasecmp(v, "sharp") ? 1 : !strcasecmp(v, "pixel") ? 2 : 0;
    else if (!strcasecmp(k, "ui font"))
        p->ui_font = !strcasecmp(v, "on");
    else if (!strcasecmp(k, "fallback"))
        p->fallback = strcasecmp(v, "off") != 0;
}

/* parses one profile, or the [section] "want" of a multi-profile file */
static bool parse_profile(char *text, const char *want, struct typo_profile *p,
                          char ids[][32], int *n_ids)
{
    bool in = want == NULL, found = want == NULL;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
    {
        char *l = trim(line);
        if (!*l || *l == '#')
            continue;
        if (*l == '[')
        {
            char *e = strchr(l, ']');
            if (e) *e = '\0';
            if (ids && *n_ids < MAX_PRESETS)
                strlcpy(ids[(*n_ids)++], l + 1, 32);
            in = want && !strcmp(l + 1, want);
            if (in)
            {
                found = true;
                strlcpy(p->id, l + 1, sizeof(p->id));
            }
            continue;
        }
        if (!in || !p)
            continue;
        char *c = strchr(l, ':');
        if (!c)
            continue;
        *c = '\0';
        parse_kv(p, trim(l), trim(c + 1));
    }
    return found;
}

static char preset_ids[MAX_PRESETS][32];
static int n_presets = -1;

int typo_preset_count(void)
{
    if (n_presets < 0)
    {
        n_presets = 0;
        char *t = read_all(TYPO_PRESETS, NULL, 32768);
        if (t)
        {
            parse_profile(t, "", NULL, preset_ids, &n_presets);
            free(t);
        }
    }
    return n_presets;
}

bool typo_preset_get(int i, struct typo_profile *out)
{
    if (i < 0 || i >= typo_preset_count())
        return false;
    char *t = read_all(TYPO_PRESETS, NULL, 32768);
    if (!t)
        return false;
    typo_profile_default(out);
    out->name[0] = '\0';
    bool ok = parse_profile(t, preset_ids[i], out, NULL, NULL);
    free(t);
    out->preset = true;
    if (!out->name[0])
        strlcpy(out->name, out->id, sizeof(out->name));
    return ok;
}

const char *typo_slug(const char *name, char *buf, int len)
{
    int o = 0;
    for (const char *s = name; *s && o < len - 1; s++)
    {
        unsigned char c = *s;
        if (isalnum(c)) buf[o++] = tolower(c);
        else if (o && buf[o - 1] != '-') buf[o++] = '-';
    }
    while (o && buf[o - 1] == '-') o--;
    buf[o] = '\0';
    if (!o) strlcpy(buf, "profile", len);
    return buf;
}

int typo_profile_list(char names[][32], int max)
{
    static char tmp[32][64];
    int n = wheel_dir_list(TYPO_DIR, ".cfg", tmp, MIN(max, 32));
    int o = 0;
    for (int i = 0; i < n; i++)
        if (strcasecmp(tmp[i], "active"))
            strlcpy(names[o++], tmp[i], 32);
    return o;
}

bool typo_profile_load(const char *id, struct typo_profile *out)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), TYPO_DIR "/%s.cfg", id);
    char *t = read_all(path, NULL, 16384);
    if (!t)
        return false;
    typo_profile_default(out);
    out->preset = false;
    out->name[0] = '\0';
    parse_profile(t, NULL, out, NULL, NULL);
    free(t);
    strlcpy(out->id, id, sizeof(out->id));
    if (!out->name[0])
        strlcpy(out->name, id, sizeof(out->name));
    return true;
}

static void fdputs(int fd, const char *s) { write(fd, s, strlen(s)); }

static void write_profile(int fd, const struct typo_profile *p)
{
    char line[160];
    static const char * const sp[] = { "compact", "normal", "wide" };
    static const char * const rn[] = { "smooth", "sharp", "pixel" };
    snprintf(line, sizeof(line), "name: %s\n", p->name); fdputs(fd, line);
    if (p->note[0]) { snprintf(line, sizeof(line), "note: %s\n", p->note); fdputs(fd, line); }
    for (int r = 0; r < TR_COUNT; r++)
    {
        snprintf(line, sizeof(line), "%s: %s | %d | %s\n", role_keys[r], p->role[r].family,
                 p->role[r].weight, typo_size_name(p->role[r].size));
        fdputs(fd, line);
    }
    snprintf(line, sizeof(line), "line height: %s\nmenu rows: %d\nlyrics spacing: %d\n"
             "now playing spacing: %d\nlist spacing: %d\n",
             line_style_keys[p->line >= 0 && p->line < TLS_COUNT ? p->line : TLS_NORMAL],
             p->row_h, p->ly_pct, p->np_pct, p->list_pct);
    fdputs(fd, line);
    snprintf(line, sizeof(line), "spacing: %s\nnumerals: %s\nrendering: %s\n"
             "ui font: %s\nfallback: %s\n",
             sp[p->spacing + 1], p->tabular ? "tabular" : "proportional",
             rn[p->render < 0 || p->render > 2 ? 0 : p->render],
             p->ui_font ? "on" : "off", p->fallback ? "on" : "off");
    fdputs(fd, line);
    for (int o = 0; o < 3; o++)
    {
        snprintf(line, sizeof(line), "%s: %s\n", ov_keys[o], typo_size_name(p->ov[o]));
        fdputs(fd, line);
    }
}

bool typo_profile_save(const struct typo_profile *p)
{
    mkdir(TYPO_DIR);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), TYPO_DIR "/%s.cfg", p->id);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    fdputs(fd, "# Wheel typography profile\n");
    write_profile(fd, p);
    close(fd);
    return true;
}

bool typo_profile_delete(const char *id)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), TYPO_DIR "/%s.cfg", id);
    return remove(path) == 0;
}

bool typo_profile_rename(const char *id, const char *new_name)
{
    struct typo_profile p;
    if (!typo_profile_load(id, &p))
        return false;
    char slug[32];
    typo_slug(new_name, slug, sizeof(slug));
    strlcpy(p.name, new_name, sizeof(p.name));
    strlcpy(p.id, slug, sizeof(p.id));
    if (!typo_profile_save(&p))
        return false;
    if (strcmp(slug, id))
        typo_profile_delete(id);
    return true;
}

/* ----------------------------- the active one ----------------------------- */

static struct typo_profile active;
static char active_files[TU_COUNT][64];
static bool active_loaded, custom;

/* fonts of the stock theme (fonts/prebuilt, made by make_fonts.sh) */
static const char * const stock_files[TU_COUNT] = {
    "Wheel-Title.fnt", "Wheel-Display.fnt", "40-Wheel-Row.fnt", "40-Wheel-RowSel.fnt",
    "Wheel-UI.fnt", "Wheel-Secondary.fnt", "Wheel-Meta.fnt", "Wheel-Numeric.fnt",
    "Wheel-Lyrics.fnt", "64-Wheel-Letters.fnt",
};
static const char * const use_names[TU_COUNT] = {
    "title", "display", "row", "rowsel", "ui", "secondary", "meta", "numeric", "lyrics", "jumbo",
};

static void load_active(void)
{
    if (active_loaded)
        return;
    active_loaded = true;
    custom = false;
    typo_profile_default(&active);
    for (int u = 0; u < TU_COUNT; u++)
        strlcpy(active_files[u], stock_files[u], sizeof(active_files[u]));
    char *t = read_all(TYPO_ACTIVE, NULL, 16384);
    if (!t)
        return;
    active.name[0] = '\0';
    /* "file <use>: cache/x.fnt" lines, then the profile */
    char *copy = strdup(t);
    if (copy)
    {
        char *save = NULL;
        for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
        {
            if (strncmp(line, "file ", 5))
                continue;
            char *c = strchr(line, ':');
            if (!c) continue;
            *c = '\0';
            for (int u = 0; u < TU_COUNT; u++)
                if (!strcmp(line + 5, use_names[u]))
                    strlcpy(active_files[u], trim(c + 1), sizeof(active_files[u]));
        }
        free(copy);
    }
    parse_profile(t, NULL, &active, NULL, NULL);
    free(t);
    custom = true;
}

const struct typo_profile *typo_active(void) { load_active(); return &active; }
bool typo_is_custom(void) { load_active(); return custom; }

/* -------------------------------- sizes -------------------------------- */

static const unsigned char em_table[TR_COUNT][TS_COUNT] = {
    { 18, 20, 23, 25, 27 },   /* display */
    { 17, 18, 20, 22, 24 },   /* primary (list rows) */
    { 15, 16, 18, 19, 20 },   /* secondary */
    { 12, 13, 15, 16, 17 },   /* metadata */
    { 12, 13, 15, 16, 17 },   /* numeric */
};
static const unsigned char lyric_em[TS_COUNT] = { 20, 22, 24, 26, 28 };

int typo_em(enum typo_role r, int size)
{
    if (size < 0) size = TS_M;
    if (size >= TS_COUNT) size = TS_XL;
    return em_table[r][size];
}

static int clamp_size(int s) { return s < TS_XS ? TS_XS : s > TS_XL ? TS_XL : s; }

static enum typo_role use_role(enum typo_use u)
{
    switch (u)
    {
        case TU_ROW: case TU_ROWSEL: case TU_UI: return TR_PRIMARY;
        case TU_SECONDARY: return TR_SECONDARY;
        case TU_META: return TR_META;
        case TU_NUMERIC: return TR_NUMERIC;
        default: return TR_DISPLAY;
    }
}

static void use_metrics(const struct typo_profile *p, enum typo_use u, int *em, int *band)
{
    int s = p->role[use_role(u)].size;
    switch (u)
    {
        case TU_DISPLAY_MENU:
            s = p->ov[TOV_MENUS] != TS_AUTO ? p->ov[TOV_MENUS] : clamp_size(s - 2);
            *em = em_table[TR_DISPLAY][s]; *band = 32; break;
        case TU_DISPLAY_NP:
            s = p->ov[TOV_NOWPLAYING] != TS_AUTO ? p->ov[TOV_NOWPLAYING] : s;
            *em = em_table[TR_DISPLAY][s]; *band = 32; break;
        case TU_LYRICS:
            s = p->ov[TOV_LYRICS] != TS_AUTO ? p->ov[TOV_LYRICS] : clamp_size(s + 1);
            *em = lyric_em[s]; *band = *em * 5 / 4 + 4; break;
        case TU_JUMBO:
            *em = 52; *band = 65; break;
        case TU_ROW: case TU_ROWSEL:
            *em = em_table[TR_PRIMARY][s]; *band = typo_row_height(p); break;
        case TU_UI:
            *em = em_table[TR_PRIMARY][clamp_size(s - 1)]; *band = 26; break;
        case TU_SECONDARY:
            *em = em_table[TR_SECONDARY][s]; *band = 26; break;
        default: /* meta, numeric */
            *em = em_table[use_role(u)][s]; *band = 22; break;
    }
}

int typo_row_height(const struct typo_profile *p)
{
    return p->row_h >= TYPO_ROW_MIN && p->row_h <= TYPO_ROW_MAX ? p->row_h : 40;
}

/* -------------------------------- build -------------------------------- */

static bool wr_fd(void *ctx, const void *buf, size_t len)
{
    return write(*(int *)ctx, buf, len) == (ssize_t)len;
}

static void mkdirs(void)
{
    mkdir(FONT_DIR);
    mkdir(TYPO_CACHE);
    mkdir(TYPO_DIR);
}

static bool build_use(const struct typo_profile *p, enum typo_use u, struct typo_build_info *bi)
{
    enum typo_role role = use_role(u);
    const struct typo_role_cfg *rc = &p->role[role];
    char fam[40];
    if (!typo_resolve_family(rc->family, fam, sizeof(fam)))
        strlcpy(fam, TYPO_FALLBACK, sizeof(fam));       /* graceful fallback */
    int em, band;
    use_metrics(p, u, &em, &band);
    int weight = rc->weight + (u == TU_ROWSEL ? 100 : 0);
    if (u == TU_JUMBO && weight < 600)
        weight = 600;
    int fi = pick_face(fam, weight, em * 0.72f, false);
    if (fi < 0)
    {
        strlcpy(fam, TYPO_FALLBACK, sizeof(fam));
        fi = pick_face(fam, weight, em * 0.72f, false);
        if (fi < 0)
            return false;
    }
    const struct typo_face *f = &faces[fi];
    strlcpy(bi->face[role], f->family, sizeof(bi->face[role]));

    struct forge_params prm;
    memset(&prm, 0, sizeof(prm));
    /* sizes are for Inter-like proportions: scale other designs so their
     * capitals come out as tall (VT323 or Playfair would look small) */
    prm.px = em;
    if (f->outline && f->cap1000 > 300 && f->cap1000 < 1000)
    {
        int px = (em * 727 + f->cap1000 / 2) / f->cap1000;
        prm.px = MAX(em * 3 / 4, MIN(px, em * 3 / 2));
    }
    prm.band = band;
    if (p->spacing < 0) prm.tracking = em >= 15 ? -1 : 0;
    else if (p->spacing > 0) prm.tracking = em >= 22 ? 2 : 1;
    if (u == TU_JUMBO) prm.tracking *= 2;
    prm.tabular = p->tabular && (u == TU_NUMERIC || u == TU_META || u == TU_JUMBO);
    prm.crisp = p->render == 2 || !f->outline;
    prm.contrast = p->render == 1 ? 3 : 1;
    /* a weight the family doesn't have: fake it (+1 px) if it's far off */
    if (f->outline && weight - f->weight >= 200 && u != TU_ROWSEL)
    {
        prm.embolden = 1;
        bi->emboldened[role] = true;
    }
    prm.last = u == TU_JUMBO ? 0x7e : 0x2122;
    prm.extras = (u == TU_ROW || u == TU_ROWSEL) ? FORGE_EXTRA_ROW : FORGE_EXTRA_NONE;

    int fb = -1;
    if (p->fallback && strcasecmp(fam, TYPO_FALLBACK) && u != TU_JUMBO)
        fb = pick_face(TYPO_FALLBACK, weight, em * 0.72f, true);

    unsigned long h = 2166136261UL;
    int ver = FORGE_VERSION;
    h = fnv(h, &ver, sizeof(ver));
    h = fnv(h, f->path, strlen(f->path));
    h = fnv(h, &f->bytes, sizeof(f->bytes));
    if (fb >= 0)
        h = fnv(h, faces[fb].path, strlen(faces[fb].path));
    int v[10] = { prm.px, prm.band, prm.tracking, prm.tabular, prm.crisp, prm.embolden,
                  prm.contrast, (int)prm.last, prm.extras, fb >= 0 };
    h = fnv(h, v, sizeof(v));
    snprintf(bi->file[u], sizeof(bi->file[u]), "cache/%s-%08lx.fnt", use_names[u], h);

    char path[MAX_PATH];
    snprintf(path, sizeof(path), FONT_DIR "/%s", bi->file[u]);
    int fd = open(path, O_RDONLY);
    if (fd >= 0)
    {
        bi->bytes[u] = ffilesize(fd);
        close(fd);
        bi->cached++;
        return true;
    }

    long n = 0, nf = 0;
    char *data = read_all(f->path, &n, 8L << 20);
    if (!data)
        return false;
    char *fbdata = fb >= 0 ? read_all(faces[fb].path, &nf, 8L << 20) : NULL;
    struct forge_src src = { (const unsigned char *)data, n };
    struct forge_src fsrc = { (const unsigned char *)fbdata, nf };

    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), TYPO_CACHE "/building.tmp");
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    int r = -3;
    struct forge_stats st;
    if (fd >= 0)
    {
        r = forge_build(&src, fbdata ? &fsrc : NULL, &prm, wr_fd, &fd, &st);
        close(fd);
    }
    free(data);
    free(fbdata);
    if (r < 0)
    {
        remove(tmp);
        return false;
    }
    if (rename(tmp, path) != 0)
    {
        remove(tmp);
        return false;
    }
    bi->bytes[u] = st.bytes;
    bi->glyphs += st.glyphs;
    bi->fallback += st.fallback;
    bi->missing += st.missing;
    bi->built++;
    return true;
}

bool typo_build_one(const struct typo_profile *p, enum typo_use u, char *file, int len)
{
    static struct typo_build_info bi;
    typo_registry_scan(false);
    memset(&bi, 0, sizeof(bi));
    mkdirs();
    if (!build_use(p, u, &bi))
        return false;
    strlcpy(file, bi.file[u], len);
    return true;
}

bool typo_build(const struct typo_profile *p, struct typo_build_info *bi,
                void (*progress)(int done, int total))
{
    typo_registry_scan(false);
    memset(bi, 0, sizeof(*bi));
    mkdirs();
    cpu_boost(true);
    bool ok = true;
    for (int u = 0; u < TU_COUNT && ok; u++)
    {
        if (progress)
            progress(u, TU_COUNT);
        ok = build_use(p, u, bi);
    }
    cpu_boost(false);
    return ok;
}

/* ---------------------------- theme files ---------------------------- */

/* Wheel skins mark the lines to adapt with a comment line before them:
 * "#@ old=new ..." - every "old" on the next line becomes "new", where
 * ${var} is a font of the profile or ${row_h}. Without a profile the skins
 * are used as they are (stock fonts). */
/* "\s" in a marker is a space (pairs are separated by spaces) */
static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++)
    {
        if (s[0] == '\\' && s[1] == 's') { *o++ = ' '; s++; }
        else *o++ = *s;
    }
    *o = '\0';
}

/* width of a character in a .fnt (for padding made of spaces) */
static int fnt_char_width(const char *rel, unsigned ch)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), FONT_DIR "/%s", rel);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    unsigned char h[36];
    int w = 0;
    if (read(fd, h, 36) == 36 && !memcmp(h, "RB12", 4))
    {
        long first = h[12] | (h[13] << 8) | ((long)h[14] << 16) | ((long)h[15] << 24);
        long count = h[20] | (h[21] << 8) | ((long)h[22] << 16) | ((long)h[23] << 24);
        long nbits = h[24] | (h[25] << 8) | ((long)h[26] << 16) | ((long)h[27] << 24);
        long noff = h[28] | (h[29] << 8) | ((long)h[30] << 16) | ((long)h[31] << 24);
        long nwid = h[32] | (h[33] << 8) | ((long)h[34] << 16) | ((long)h[35] << 24);
        bool off32 = nbits >= 0xFFDB;
        long p = 36 + nbits;
        if (noff)
            p = ((p + (off32 ? 3 : 1)) & ~(long)(off32 ? 3 : 1)) + noff * (off32 ? 4 : 2);
        unsigned char c;
        if ((long)ch >= first && (long)ch < first + count && nwid &&
            lseek(fd, p + (ch - first), SEEK_SET) >= 0 && read(fd, &c, 1) == 1)
            w = c;
        else
            w = h[4] | (h[5] << 8);
    }
    close(fd);
    return w;
}

static int skin_pad_px[2];   /* left / right padding of list rows, in spaces */
static int skin_np[5];       /* Now Playing text block: tops of its 5 lines */

/* Now Playing: track number, title, artist, album, details. The gaps
 * between them follow the spacing %, never overlapping, kept centred
 * next to the cover and above the shuffle/repeat line */
void typo_np_positions(int pct, int y[5])
{
    static const short gap[4] = { 28, 37, 28, 33 };   /* at 100 % */
    static const short hgt[5] = { 22, 32, 26, 26, 22 };
    int g[4], total = hgt[4];
    for (int i = 0; i < 4; i++)
    {
        g[i] = (gap[i] * pct + 50) / 100;
        if (g[i] < hgt[i]) g[i] = hgt[i];
        total += g[i];
    }
    int start = 131 - total / 2;      /* 57..205 at 100 %: centre 131 */
    if (start < 42) start = 42;
    if (start + total > 222) start = 222 - total;
    y[0] = start;
    for (int i = 0; i < 4; i++)
        y[i + 1] = y[i] + g[i];
}

static const char *skin_var(const char *name, const char files[TU_COUNT][64], int row_h)
{
    static char num[8];
    static char pad[12];
    if (name[0] == 'n' && name[1] == 'p' && name[2] >= '1' && name[2] <= '5' && !name[3])
    {
        snprintf(num, sizeof(num), "%d", skin_np[name[2] - '1']);
        return num;
    }
    if (!strcmp(name, "pad") || !strcmp(name, "rpad"))
    {   /* ~14 px / ~10 px of spaces in the row font */
        int n = name[0] == 'p' ? skin_pad_px[0] : skin_pad_px[1];
        if (n < 1) n = 1;
        if (n > 10) n = 10;
        memset(pad, ' ', n);
        pad[n] = '\0';
        return pad;
    }
    static const char * const vars[] = {
        "title", "display", "row", "rowsel", "ui", "secondary", "meta", "numeric", "lyrics", "jumbo" };
    for (int u = 0; u < TU_COUNT; u++)
        if (!strcmp(name, vars[u]))
            return files[u];
    if (!strcmp(name, "row_h"))
    {
        snprintf(num, sizeof(num), "%d", row_h);
        return num;
    }
    return NULL;
}

/* colours: every 6-digit hex word the palette knows is replaced */
static void recolour(char *out, size_t size)
{
    char res[512];
    size_t o = 0;
    for (const char *s = out; *s && o < sizeof(res) - 1; )
    {
        bool word = isxdigit((unsigned char)s[0]) && (s == out || !isalnum((unsigned char)s[-1]));
        int n = 0;
        while (word && n < 7 && isxdigit((unsigned char)s[n])) n++;
        const char *m = (word && n == 6 && !isalnum((unsigned char)s[6])) ? wheel_palette_map(s) : NULL;
        if (m && o + 6 < sizeof(res) - 1)
        {
            memcpy(res + o, m, 6);
            o += 6;
            s += 6;
        }
        else
            res[o++] = *s++;
    }
    res[o] = '\0';
    strlcpy(out, res, size);
}

static const char *gen_prelude; /* extra first line of the generated skin */
static bool gen_aero;           /* apply the "#@aero" lines too */

static bool gen_skin(const char *src, const char *dst, const char files[TU_COUNT][64], int row_h,
                     bool colour)
{
    char *t = read_all(src, NULL, 65536);
    if (!t)
        return false;
    int fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        free(t);
        return false;
    }
    fdputs(fd, "# generated by the Wheel typography settings - edit the original instead\n");
    if (gen_prelude)
        fdputs(fd, gen_prelude);
    char *line = t;
    bool ok = true;
    char pending[256] = "";
    while (line && *line && ok)
    {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char out[512];
        strlcpy(out, line, sizeof(out));
        bool aero_marker = !strncmp(line, "#@aero ", 7);
        if (aero_marker || !strncmp(line, "#@", 2))
        {   /* replacements for the next line ("#@aero": glossy finish only) */
            if (!aero_marker || gen_aero)
            {
                if (pending[0])
                    strlcat(pending, " ", sizeof(pending));
                strlcat(pending, line + (aero_marker ? 7 : 2), sizeof(pending));
            }
        }
        else if (pending[0])
        {
            char *save = NULL;
            for (char *pair = strtok_r(pending, " ", &save); pair; pair = strtok_r(NULL, " ", &save))
            {
                char *eq = strchr(pair, '=');
                if (!eq) continue;
                *eq = '\0';
                unescape(pair);
                unescape(eq + 1);
                /* the replacement may use ${var} */
                char val[160];
                size_t vo = 0;
                for (const char *q = eq + 1; *q && vo < sizeof(val) - 1; )
                {
                    const char *e = (q[0] == '$' && q[1] == '{') ? strchr(q, '}') : NULL;
                    if (e)
                    {
                        char name[24];
                        strlcpy(name, q + 2, MIN((size_t)(e - q - 1), sizeof(name)));
                        const char *v = skin_var(name, files, row_h);
                        if (v)
                            vo += strlcpy(val + vo, v, sizeof(val) - vo);
                        if (vo >= sizeof(val)) vo = sizeof(val) - 1;
                        q = e + 1;
                    }
                    else
                        val[vo++] = *q++;
                }
                val[vo] = '\0';
                if (!*pair) continue;
                char res[512];
                size_t o = 0, plen = strlen(pair), vlen = strlen(val);
                for (const char *s = out; *s && o < sizeof(res) - 1; )
                {
                    if (!strncmp(s, pair, plen) && o + vlen < sizeof(res) - 1)
                    {
                        memcpy(res + o, val, vlen);
                        o += vlen;
                        s += plen;
                    }
                    else
                        res[o++] = *s++;
                }
                res[o] = '\0';
                strlcpy(out, res, sizeof(out));
            }
            pending[0] = '\0';
        }
        if (colour && out[0] != '#')
            recolour(out, sizeof(out));
        size_t l = strlen(out);
        out[l] = '\n';
        ok = write(fd, out, l + 1) == (ssize_t)(l + 1);
        line = nl ? nl + 1 : NULL;
    }
    close(fd);
    free(t);
    if (!ok)
        remove(dst);
    return ok;
}

static void skin_vars(const char files[TU_COUNT][64], int np_pct)
{
    typo_np_positions(np_pct, skin_np);
    int sp = fnt_char_width(files[TU_ROW], ' ');
    skin_pad_px[0] = sp > 0 ? (14 + sp / 2) / sp : 3;
    skin_pad_px[1] = sp > 0 ? (10 + sp / 2) / sp : 2;
}

static bool gen_theme_files(const char files[TU_COUNT][64], int row_h, int np_pct)
{
    skin_vars(files, np_pct);
    static const char * const skins[] = { "Wheel", "Wheel-Dark" };
    static const char * const exts[] = { "sbs", "wps" };
    mkdir(WPS_DIR);
    for (unsigned i = 0; i < ARRAYLEN(skins); i++)
        for (unsigned e = 0; e < ARRAYLEN(exts); e++)
        {
            char src[MAX_PATH], dst[MAX_PATH];
            snprintf(src, sizeof(src), WPS_DIR "/%s.%s", skins[i], exts[e]);
            snprintf(dst, sizeof(dst), WPS_DIR "/%s-Type.%s", skins[i], exts[e]);
            if (!gen_skin(src, dst, files, row_h, false))
                return false;
        }
    return true;
}

static bool generated_present(void)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), WPS_DIR "/Wheel-Type.sbs");
    if (!exists(path))
        return false;
    for (int u = 0; u < TU_COUNT; u++)
    {
        snprintf(path, sizeof(path), FONT_DIR "/%s", active_files[u]);
        if (!exists(path))
            return false;
    }
    return true;
}

/* --------------------- colour schemes (generated) ---------------------
 * Wheel-Pal        the palette chosen in Appearance > Palette
 * Wheel-Aero       the classic light colours with the glossy finish
 * Wheel-Dark-Aero  the classic dark colours with the glossy finish
 * all made from the Wheel.sbs/.wps templates with the typography profile. */
#define GEN_FORMAT 3

static const struct wheel_palette *scheme_for(const char *name)
{
    if (!strcmp(name, "Wheel-Pal"))
        return wheel_palette_active();
    if (!strcmp(name, "Wheel-Aero"))
        return wheel_palette_classic(false);
    if (!strcmp(name, "Wheel-Dark-Aero"))
        return wheel_palette_classic(true);
    return NULL;
}

/* first line of a generated scheme: what it was made from */
static void scheme_signature(const char *name, char *buf, size_t size)
{
    unsigned h = 2166136261u;
    for (int u = 0; u < TU_COUNT; u++)
        for (const char *c = active_files[u]; *c; c++)
            h = (h ^ (unsigned char)*c) * 16777619u;
    h = (h ^ (unsigned)typo_row_height(&active)) * 16777619u;
    h = (h ^ (unsigned)active.np_pct) * 16777619u;
    h = (h ^ (unsigned)active.list_pct) * 16777619u;
    bool pal = !strcmp(name, "Wheel-Pal");
    snprintf(buf, size, "# wheel-gen format=%d skin=%s palette=%d aero=%d typo=%08x\n",
             GEN_FORMAT, name, pal ? global_settings.wheel_palette : 0,
             pal ? global_settings.wheel_aero : 1, h);
}

static bool scheme_current(const char *name)
{
    char want[160], have[320], path[MAX_PATH];
    scheme_signature(name, want, sizeof(want));
    snprintf(path, sizeof(path), WPS_DIR "/%s.sbs", name);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    ssize_t n = read(fd, have, sizeof(have) - 1);
    close(fd);
    have[n > 0 ? n : 0] = '\0';
    return strstr(have, want) != NULL;
}

static bool gen_scheme(const char *name)
{
    static const char * const exts[] = { "sbs", "wps" };
    const struct wheel_palette *p = scheme_for(name);
    if (!p)
        return false;
    bool aero = strcmp(name, "Wheel-Pal") || global_settings.wheel_aero;
    wheel_palette_use(p);
    skin_vars((const char (*)[64])active_files, active.np_pct);
    mkdir(WPS_DIR);
    /* the glossy finish: a backdrop with the gel bar and the page sheen */
    char bmp[40], prelude[224];
    scheme_signature(name, prelude, sizeof(prelude));
    if (aero && wheel_palette_backdrop(name, bmp, sizeof(bmp)))
        snprintf(prelude + strlen(prelude), sizeof(prelude) - strlen(prelude),
                 "%%X(%s)\n", bmp);
    gen_prelude = prelude;
    gen_aero = aero;
    bool ok = true;
    for (unsigned e = 0; e < ARRAYLEN(exts) && ok; e++)
    {
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, sizeof(src), WPS_DIR "/Wheel.%s", exts[e]);
        snprintf(dst, sizeof(dst), WPS_DIR "/%s.%s", name, exts[e]);
        ok = gen_skin(src, dst, (const char (*)[64])active_files, typo_row_height(&active), true);
    }
    gen_prelude = NULL;
    gen_aero = false;
    wheel_palette_use(NULL);
    if (!ok)
    {   /* never leave half a theme behind */
        char path[MAX_PATH];
        snprintf(path, sizeof(path), WPS_DIR "/%s.sbs", name);
        remove(path);
    }
    return ok;
}

static bool ensure_scheme(const char *name)
{
    return scheme_current(name) || gen_scheme(name);
}

bool typo_generate_palette(void)
{
    load_active();
    return wheel_palette_active() ? ensure_scheme("Wheel-Pal") : true;
}

/* ----------------------------- settings ----------------------------- */

static bool set_text(unsigned char *dst, const char *v, size_t len)
{
    if (!strcmp((char *)dst, v))
        return false;
    strlcpy((char *)dst, v, len);
    return true;
}

static void strip_fnt(const char *in, char *out, size_t len)
{
    strlcpy(out, in, len);
    char *d = strrchr(out, '.');
    if (d && !strcasecmp(d, ".fnt"))
        *d = '\0';
}

/* the names of the Wheel skins (and what they are generated as) */
static bool wheel_family(const char *name)
{
    static const char * const names[] = {
        "Wheel", "Wheel-Dark", "Wheel-Type", "Wheel-Dark-Type",
        "Wheel-Pal", "Wheel-Aero", "Wheel-Dark-Aero",
    };
    for (unsigned i = 0; i < ARRAYLEN(names); i++)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}

static const char *wheel_skin_for(bool dark)
{
    if (wheel_palette_active())
        return "Wheel-Pal";
    if (global_settings.wheel_aero)
        return dark ? "Wheel-Dark-Aero" : "Wheel-Aero";
    if (custom)
        return dark ? "Wheel-Dark-Type" : "Wheel-Type";
    return dark ? "Wheel-Dark" : "Wheel";
}

bool typo_patch_settings_changed(void)
{
    load_active();
    bool changed = false;
    char buf[MAX_FILENAME + 1];
    bool wheel = wheel_family((char *)global_settings.sbs_file);
    if (wheel)
    {   /* the Wheel skins: stock, typography (-Type), glossy (-Aero) or a
         * palette, for the light or dark variant that was loaded */
        const char *want = wheel_skin_for(strstr((char *)global_settings.sbs_file, "Dark") != NULL);
        if (scheme_for(want) && !ensure_scheme(want))
        {   /* can't write the generated files: plain skins */
            global_settings.wheel_palette = 0;
            want = strstr((char *)global_settings.sbs_file, "Dark") ?
                   (custom ? "Wheel-Dark-Type" : "Wheel-Dark") : (custom ? "Wheel-Type" : "Wheel");
        }
        changed |= set_text(global_settings.sbs_file, want, sizeof(global_settings.sbs_file));
        if (wheel_family((char *)global_settings.wps_file))
            changed |= set_text(global_settings.wps_file, want, sizeof(global_settings.wps_file));
        changed |= wheel_palette_patch_colours();
    }
    /* the UI font: the Wheel themes always use the profile's, other
     * themes only if asked to */
    bool cache_font = !strncmp((char *)global_settings.font_file, "cache/", 6);
    if (custom && (wheel || active.ui_font))
    {
        strip_fnt(active_files[TU_UI], buf, sizeof(buf));
        changed |= set_text(global_settings.font_file, buf, sizeof(global_settings.font_file));
    }
    else if (cache_font)
    {
        strip_fnt(stock_files[TU_UI], buf, sizeof(buf));
        changed |= set_text(global_settings.font_file, buf, sizeof(global_settings.font_file));
    }
    return changed;
}

void typo_patch_settings(void)
{
    typo_patch_settings_changed();
}

static void reload_everything(void)
{
    typo_fonts_unload();
    settings_apply(true);
    wheel_reload_skins();
    settings_save();
}

static void write_active(const struct typo_profile *p, const char files[TU_COUNT][64])
{
    mkdirs();
    int fd = open(TYPO_ACTIVE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    char line[128];
    fdputs(fd, "# Wheel typography: the profile in use and its fonts\n");
    snprintf(line, sizeof(line), "id: %s\n", p->id);
    fdputs(fd, line);
    for (int u = 0; u < TU_COUNT; u++)
    {
        snprintf(line, sizeof(line), "file %s: %s\n", use_names[u], files[u]);
        fdputs(fd, line);
    }
    write_profile(fd, p);
    close(fd);
}

static void build_progress(int done, int total)
{
    splash_progress(done, total, "%s", str(LANG_WHEEL_TYPO_PREPARING));
}

bool typo_apply(const struct typo_profile *p)
{
    static struct typo_build_info bi;
    if (!typo_build(p, &bi, build_progress))
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_TYPO_FAILED));
        return false;
    }
    if (!gen_theme_files((const char (*)[64])bi.file, typo_row_height(p), p->np_pct))
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_TYPO_FAILED));
        return false;
    }
    write_active(p, (const char (*)[64])bi.file);
    active = *p;
    for (int u = 0; u < TU_COUNT; u++)
        strlcpy(active_files[u], bi.file[u], sizeof(active_files[u]));
    active_loaded = true;
    custom = true;
    typo_patch_settings_changed(); /* (regenerates the colour schemes) */
    reload_everything();
    return true;
}

void typo_restore_default(void)
{
    remove(TYPO_ACTIVE);
    active_loaded = false;
    load_active();
    typo_patch_settings_changed(); /* (regenerates the colour schemes) */
    reload_everything();
}

void typo_init(void)
{
    typo_update_fallbacks(true);
    load_active();
    if (scheme_for((char *)global_settings.sbs_file) &&
        !scheme_current((char *)global_settings.sbs_file))
    {   /* first start after an update, or the files are gone */
        if (wheel_palette_active() && !ensure_scheme("Wheel-Pal"))
            global_settings.wheel_palette = 0;
        if (typo_patch_settings_changed())
            settings_apply(true);
        settings_apply_skins();
    }
    if (custom && !generated_present())
    {   /* cache or theme files gone (card wiped?): back to the stock look */
        remove(TYPO_ACTIVE);
        active_loaded = false;
        load_active();
        if (typo_patch_settings_changed())
        {
            settings_apply(true);
            settings_apply_skins();
            settings_save();
        }
    }
}

/* ------------------------- fonts for C screens ------------------------- */

static int loaded_ids[TU_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static bool ids_init;

int typo_font(enum typo_use u)
{
    if (!ids_init)
    {
        for (int i = 0; i < TU_COUNT; i++) loaded_ids[i] = -1;
        ids_init = true;
    }
    load_active();
    if (u < 0 || u >= TU_COUNT)
        return FONT_UI;
    if (loaded_ids[u] < 0)
    {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), FONT_DIR "/%s", active_files[u]);
        loaded_ids[u] = font_load(path);
        if (loaded_ids[u] < 0)
        {   /* stock font as a fallback */
            snprintf(path, sizeof(path), FONT_DIR "/%s", stock_files[u]);
            loaded_ids[u] = font_load(path);
        }
    }
    return loaded_ids[u] >= 0 ? loaded_ids[u] : FONT_UI;
}

void typo_fonts_unload(void)
{
    for (int u = 0; u < TU_COUNT; u++)
        if (ids_init && loaded_ids[u] >= 0)
        {
            font_unload(loaded_ids[u]);
            loaded_ids[u] = -1;
        }
}

long typo_loaded_bytes(int *fonts)
{
    /* font_get() falls back to a lower slot for an empty one: count each
     * loaded font once */
    struct font *seen[MAXFONTS];
    int n = 0;
    long total = 0;
    for (int f = FONT_FIRSTUSERFONT; f < MAXFONTS; f++)
    {
        struct font *pf = font_get(f);
        if (!pf || pf == font_get(FONT_SYSFIXED))
            continue;
        int k;
        for (k = 0; k < n && seen[k] != pf; k++);
        if (k < n)
            continue;
        seen[n++] = pf;
        total += pf->buffer_size;
    }
    if (fonts) *fonts = n;
    return total;
}

/* ---------------------------- glyph fallback ---------------------------- */

/* fonts/cjk/CJK-<script>-<em>.fnt (Noto Sans SC/JP/KR, SIL OFL), plus the
 * stock Inter fonts for Latin/Greek/Cyrillic that a theme's font lacks */
#define CJK_DIR FONT_DIR "/cjk"
static const char * const cjk_scripts[3] = { "JP", "SC", "KR" };
static const int cjk_sizes[2] = { 22, 17 };
static int cjk_ids[3][2] = { { -1, -1 }, { -1, -1 }, { -1, -1 } };
static int latin_ids[2] = { -1, -1 };
static bool fb_loaded;
static char fb_lang[MAX_FILENAME + 1] = "\x01";

int typo_cjk_fonts(void)
{
    int n = 0;
    for (int s = 0; s < 3; s++)
        for (int z = 0; z < 2; z++)
            n += cjk_ids[s][z] >= 0;
    return n;
}

void typo_update_fallbacks(bool force)
{
    const char *lang = (const char *)global_settings.lang_file;
    if (!force && fb_loaded && !strcmp(lang, fb_lang))
        return;
    strlcpy(fb_lang, lang, sizeof(fb_lang));
    if (!fb_loaded)
    {   /* glyph caches: these stay open, only the glyphs in use are in RAM */
        for (int s = 0; s < 3; s++)
            for (int z = 0; z < 2; z++)
            {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), CJK_DIR "/CJK-%s-%d.fnt", cjk_scripts[s], cjk_sizes[z]);
                cjk_ids[s][z] = font_load(path);
            }
        latin_ids[0] = font_load(FONT_DIR "/Wheel-Secondary.fnt");
        latin_ids[1] = font_load(FONT_DIR "/Wheel-Meta.fnt");
        fb_loaded = true;
    }
    /* Han characters look different in each language: the UI language's
     * script first, then the others */
    int order[3] = { 0, 1, 2 };                   /* JP, SC, KR */
    if (!strncmp(lang, "chinese", 7)) { order[0] = 1; order[1] = 0; }
    else if (!strcmp(lang, "korean")) { order[0] = 2; order[1] = 0; order[2] = 1; }
    int ids[MAX_FALLBACK_FONTS], n = 0;
    for (int i = 0; i < 3; i++)
        for (int z = 0; z < 2; z++)
            if (cjk_ids[order[i]][z] >= 0 && n < MAX_FALLBACK_FONTS)
                ids[n++] = cjk_ids[order[i]][z];
    for (int z = 0; z < 2; z++)
        if (latin_ids[z] >= 0 && n < MAX_FALLBACK_FONTS)
            ids[n++] = latin_ids[z];
    font_set_fallbacks(ids, n);
}

/* ------------------------------- cache ------------------------------- */

long typo_cache_bytes(int *files)
{
    static char names[128][64];
    int n = wheel_dir_list(TYPO_CACHE, ".fnt", names, 128);
    long total = 0;
    for (int i = 0; i < n; i++)
    {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), TYPO_CACHE "/%s.fnt", names[i]);
        int fd = open(path, O_RDONLY);
        if (fd >= 0)
        {
            total += ffilesize(fd);
            close(fd);
        }
    }
    if (files) *files = n;
    return total;
}

/* removes every cached font the active profile doesn't use */
void typo_cache_clear(void)
{
    load_active();
    static char names[128][64];
    int n = wheel_dir_list(TYPO_CACHE, ".fnt", names, 128);
    for (int i = 0; i < n; i++)
    {
        char rel[80];
        snprintf(rel, sizeof(rel), "cache/%s.fnt", names[i]);
        bool used = false;
        for (int u = 0; u < TU_COUNT && custom; u++)
            if (!strcmp(rel, active_files[u]))
                used = true;
        if (!used)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), FONT_DIR "/%s", rel);
            remove(path);
        }
    }
}
