/***************************************************************************
 * Y1 Wheel edition - Settings > Appearance > Typography & Fonts.
 *
 *   Font themes   gallery: every preset/profile written in its own font
 *   Customize     roles (font, weight, size), spacing, line height,
 *                 numerals, rendering, per-screen sizes
 *   Preview       library rows, album list, Now Playing, artist info,
 *                 controls and times drawn with the candidate fonts,
 *                 without applying them (Apply / Keep editing / Reset)
 *   Profiles      apply, customise, rename, delete, new from current
 *   Font management  installed fonts, coverage, import, memory, cache
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "action.h"
#include "list.h"
#include "splash.h"
#include "yesno.h"
#include "keyboard.h"
#include "settings.h"
#include "misc.h"
#include "file.h"
#include "dir.h"
#include "font.h"
#include "screens.h"
#include "viewport.h"
#include "string-extra.h"
#include "rbpaths.h"
#include "wheel_typo.h"
#include "wheel_fontforge.h"
#include "wheel_screens.h"
#include "wheel_themes.h"

#define S(id) str(id)

/* --------------------------- small helpers --------------------------- */

static const char * const *pick_items;
static const char *pick_get_name(int i, void *d, char *b, size_t l)
{
    (void)d; (void)b; (void)l;
    return pick_items[i];
}
static int pick_action(int a, struct gui_synclist *l)
{
    (void)l;
    return a == ACTION_STD_MENU ? ACTION_STD_CANCEL : a;
}
/* a list to choose from; -1 if cancelled */
static int pick(const char *title, const char * const *items, int n, int sel)
{
    struct simplelist_info info;
    pick_items = items;
    simplelist_info_init(&info, (char *)title, n, NULL);
    info.get_name = pick_get_name;
    info.action_callback = pick_action;
    info.selection = sel;
    simplelist_show_list(&info);
    return info.selection;
}

static const char *role_label(int r)
{
    static const int ids[TR_COUNT] = { LANG_WHEEL_TYPO_DISPLAY, LANG_WHEEL_TYPO_PRIMARY,
                                       LANG_WHEEL_TYPO_SECONDARY, LANG_WHEEL_TYPO_META,
                                       LANG_WHEEL_TYPO_NUMERIC };
    return S(ids[r]);
}
static const char *spacing_label(int s)
{
    return s < 0 ? S(LANG_WHEEL_TYPO_COMPACT) : s > 0 ? S(LANG_WHEEL_TYPO_WIDE) : S(LANG_WHEEL_TYPO_NORMAL);
}
static const char *line_label(int s)
{
    switch (s)
    {
        case TLS_COMPACT: return S(LANG_WHEEL_TYPO_COMPACT);
        case TLS_COMFORT: return S(LANG_WHEEL_TYPO_COMFORT);
        case TLS_AIRY:    return S(LANG_WHEEL_TYPO_AIRY);
        case TLS_CUSTOM:  return S(LANG_WHEEL_TYPO_CUSTOM);
        default:          return S(LANG_WHEEL_TYPO_NORMAL);
    }
}
static const char *render_label(int r)
{
    return r == 1 ? S(LANG_WHEEL_TYPO_SHARP) : r == 2 ? S(LANG_WHEEL_TYPO_PIXEL) : S(LANG_WHEEL_TYPO_SMOOTH);
}
static const char *size_label(int s)
{
    return s == TS_AUTO ? (const char *)S(LANG_WHEEL_TYPO_AUTO) : typo_size_name(s);
}

/* the family a role will really use (first installed candidate) */
static const char *role_family(const struct typo_role_cfg *rc, char *buf, int len)
{
    if (!typo_resolve_family(rc->family, buf, len))
        strlcpy(buf, TYPO_FALLBACK, len);
    return buf;
}

static void progress(int done, int total)
{
    splash_progress(done, total, "%s", S(LANG_WHEEL_TYPO_PREPARING));
}

/* ------------------------------ preview ------------------------------ */

enum { PV_TITLE, PV_DISPLAY, PV_ROW, PV_ROWSEL, PV_SEC, PV_META, PV_NUM, PV_COUNT };
static const int pv_use[PV_COUNT] = { TU_DISPLAY_MENU, TU_DISPLAY_NP, TU_ROW, TU_ROWSEL,
                                      TU_SECONDARY, TU_META, TU_NUMERIC };

static unsigned mix(unsigned a, unsigned b, int pct)
{
    return LCD_RGBPACK((RGB_UNPACK_RED(a) * pct + RGB_UNPACK_RED(b) * (100 - pct)) / 100,
                       (RGB_UNPACK_GREEN(a) * pct + RGB_UNPACK_GREEN(b) * (100 - pct)) / 100,
                       (RGB_UNPACK_BLUE(a) * pct + RGB_UNPACK_BLUE(b) * (100 - pct)) / 100);
}

static void text(struct screen *d, struct viewport *vp, int font, unsigned col, int x, int y,
                 const char *s, int maxw)
{
    vp->font = font;
    d->set_foreground(col);
    if (maxw > 0)
    {   /* cut long text with an ellipsis */
        char buf[96];
        strlcpy(buf, s, sizeof(buf));
        int w, h;
        d->getstringsize((unsigned char *)buf, &w, &h);
        size_t l = strlen(buf);
        while (w > maxw && l > 1)
        {
            do l--; while (l > 1 && (buf[l] & 0xc0) == 0x80);
            strcpy(buf + l, "…");
            d->getstringsize((unsigned char *)buf, &w, &h);
        }
        d->putsxy(x, y, (unsigned char *)buf);
    }
    else
        d->putsxy(x, y, (unsigned char *)s);
}

static int text_w(struct screen *d, struct viewport *vp, int font, const char *s)
{
    int w, h;
    vp->font = font;
    d->getstringsize((unsigned char *)s, &w, &h);
    return w;
}

static void triangle(struct screen *d, int x, int y, int h, bool right)
{
    for (int i = 0; i < h; i++)
    {
        int len = (i < h / 2 ? i : h - 1 - i) + 1;
        if (right) d->hline(x, x + len - 1, y + i);
        else d->hline(x + h / 2 - len + 1, x + h / 2, y + i);
    }
}

static void draw_preview(const struct typo_profile *p, const int *f, int row_h)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    unsigned fg = global_settings.fg_color, bg = global_settings.bg_color;
    unsigned grey = mix(fg, bg, 55), pane = mix(fg, bg, 5), bar = mix(fg, bg, 4);
    unsigned accent = global_settings.lse_color;
    d->set_drawmode(DRMODE_SOLID);
    d->set_background(bg);
    d->clear_viewport();

    /* top bar */
    d->set_foreground(bar);
    d->fillrect(0, 0, LCD_WIDTH, 38);
    d->set_foreground(mix(fg, bg, 18));
    d->hline(0, LCD_WIDTH - 1, 37);
    d->set_drawmode(DRMODE_FG);
    text(d, &vp, f[PV_TITLE], fg, 16, 2, S(LANG_WHEEL_TYPO_PREVIEW), 300);
    text(d, &vp, f[PV_NUM], grey, LCD_WIDTH - 16 - text_w(d, &vp, f[PV_NUM], "87%"), 8, "87%", 0);

    /* left: library rows, then an album list */
    const char *rows[] = { S(LANG_WHEEL_FOR_YOU), S(LANG_WHEEL_MUSIC), S(LANG_PLAYLISTS) };
    int y = 38;
    for (int i = 0; i < 3; i++, y += row_h)
    {
        unsigned col = fg;
        if (i == 0)
        {
            d->set_drawmode(DRMODE_SOLID);
            d->set_foreground(accent);
            d->fillrect(0, y, 264, row_h);
            d->set_drawmode(DRMODE_FG);
            col = LCD_RGBPACK(255, 255, 255);
        }
        int fr = i == 0 ? f[PV_ROWSEL] : f[PV_ROW];
        text(d, &vp, fr, col, 14, y, rows[i], 210);
        text(d, &vp, fr, i == 0 ? col : grey, 264 - 22, y, "›", 0);
    }
    y += 6;
    text(d, &vp, f[PV_META], grey, 14, y, S(LANG_ALBUMS), 0);
    y += 22;
    static const char * const al[][2] = {
        { "Ciudad de Cristal", "Orquesta Imaginaria · 2019" },
        { "Mareas", "Clara Ruiz Trío · 12 · 48:20" },
    };
    int lr = 52 * p->list_pct / 100, lg = MAX(22, 26 * p->list_pct / 100);
    for (int i = 0; i < 2 && y + lg + 22 <= LCD_HEIGHT - 26; i++)
    {
        d->set_drawmode(DRMODE_SOLID);
        d->set_foreground(mix(fg, bg, 12));
        d->fillrect(14, y + (lg + 22 - 40) / 2, 40, 40);
        d->set_drawmode(DRMODE_FG);
        text(d, &vp, f[PV_SEC], fg, 64, y, al[i][0], 190);
        text(d, &vp, f[PV_META], grey, 64, y + lg, al[i][1], 190);
        y += lr;
    }

    /* right: now playing */
    d->set_drawmode(DRMODE_SOLID);
    d->set_foreground(pane);
    d->fillrect(264, 38, LCD_WIDTH - 264, LCD_HEIGHT - 38);
    d->set_foreground(mix(fg, bg, 14));
    d->fillrect(317, 50, 110, 110);
    d->set_drawmode(DRMODE_FG);
    /* title / artist / album with the Now Playing spacing of the profile */
    int ny[5];
    typo_np_positions(p->np_pct, ny);
    int t0 = 166, t1 = t0 + (ny[2] - ny[1]), t2 = t1 + (ny[3] - ny[2]);
    text(d, &vp, f[PV_DISPLAY], fg, 276, t0 - 4, "Luz de Agosto", 196);
    text(d, &vp, f[PV_SEC], accent, 276, t1, "Clara Ruiz Trío", 196);
    text(d, &vp, f[PV_META], grey, 276, t2, "Mareas · 2021 · FLAC", 196);
    int pb = MAX(t2 + 30, 270);
    d->set_drawmode(DRMODE_SOLID);
    d->set_foreground(mix(fg, bg, 14));
    d->fillrect(276, pb, 192, 4);
    d->set_foreground(accent);
    d->fillrect(276, pb, 74, 4);
    /* controls */
    int cy = MIN(pb + 32, LCD_HEIGHT - 20);
    d->set_foreground(fg);
    triangle(d, 318, cy, 15, false);
    d->fillrect(316, cy, 3, 15);
    d->fillrect(364, cy - 2, 4, 19);
    d->fillrect(372, cy - 2, 4, 19);
    triangle(d, 414, cy, 15, true);
    d->fillrect(422, cy, 3, 15);
    d->set_drawmode(DRMODE_FG);
    text(d, &vp, f[PV_NUM], grey, 276, pb + 6, "1:58", 0);
    text(d, &vp, f[PV_NUM], grey, 468 - text_w(d, &vp, f[PV_NUM], "-3:07"), pb + 6, "-3:07", 0);

    /* hint */
    text(d, &vp, f[PV_META], grey, 14, LCD_HEIGHT - 24, S(LANG_WHEEL_TYPO_PREVIEW_HINT), 240);
    (void)p;
    d->update_viewport();
    d->set_viewport(last);
}

enum { PVR_APPLY = 1, PVR_EDIT, PVR_RESET, PVR_CANCEL, PVR_CUSTOMIZE };
enum { PM_VIEW, PM_GALLERY, PM_EDIT };

/* draws the profile with its own fonts, without applying anything;
 * OK asks what to do */
static int preview(const struct typo_profile *p, int mode)
{
    static struct typo_build_info bi;
    if (!typo_build(p, &bi, progress))
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_TYPO_FAILED));
        return PVR_CANCEL;
    }
    int f[PV_COUNT];
    for (int i = 0; i < PV_COUNT; i++)
    {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), FONT_DIR "/%s", bi.file[pv_use[i]]);
        f[i] = font_load(path);
        if (f[i] < 0) f[i] = FONT_UI;
    }
    int ret = PVR_CANCEL;
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    while (1)
    {
        draw_preview(p, f, typo_row_height(p));
        int a = get_action(CONTEXT_STD, TIMEOUT_BLOCK);
        if (a == ACTION_STD_OK)
        {
            const char *items[4];
            int ids[4], n = 0;
            if (mode != PM_VIEW) { items[n] = S(LANG_WHEEL_TYPO_APPLY); ids[n++] = PVR_APPLY; }
            if (mode == PM_EDIT)
            {
                items[n] = S(LANG_WHEEL_TYPO_KEEP_EDITING); ids[n++] = PVR_EDIT;
                items[n] = S(LANG_WHEEL_TYPO_RESET); ids[n++] = PVR_RESET;
            }
            else { items[n] = S(LANG_WHEEL_TYPO_CUSTOMIZE); ids[n++] = PVR_CUSTOMIZE; }
            items[n] = S(LANG_WHEEL_TYPO_CANCEL); ids[n++] = PVR_CANCEL;
            FOR_NB_SCREENS(i)
                viewportmanager_theme_undo(i, false);
            int k = pick(p->name, items, n, 0);
            FOR_NB_SCREENS(i)
                viewportmanager_theme_enable(i, false, NULL);
            if (k >= 0)
            {
                ret = ids[k];
                break;
            }
        }
        else if (a == ACTION_STD_CANCEL || a == ACTION_STD_MENU)
        {
            ret = PVR_EDIT;
            break;
        }
        else if (default_event_handler(a) == SYS_USB_CONNECTED)
            break;
    }
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, true);
    for (int i = 0; i < PV_COUNT; i++)
        if (f[i] != FONT_UI)
            font_unload(f[i]);
    return ret;
}

/* ----------------------------- customise ----------------------------- */

static struct typo_profile edit;
static struct typo_profile edit_base;   /* for "Reset" */
static bool cu_done;                     /* applied: leave Customize */

static void save_as(void)
{
    char name[32];
    strlcpy(name, edit.preset ? "" : edit.name, sizeof(name));
    if (kbd_input(name, sizeof(name), NULL) < 0 || !name[0])
        return;
    strlcpy(edit.name, name, sizeof(edit.name));
    typo_slug(name, edit.id, sizeof(edit.id));
    edit.preset = false;
    edit.note[0] = '\0';
    if (typo_profile_save(&edit))
        splash(HZ, ID2P(LANG_WHEEL_TYPO_SAVED));
}

/* --- one role: font, weight, size --- */
static int er_role;
enum { ER_FAMILY, ER_WEIGHT, ER_SIZE, ER_COUNT };

static const char *er_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    struct typo_role_cfg *rc = &edit.role[er_role];
    char fam[40];
    switch (item)
    {
        case ER_FAMILY:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", S(LANG_WHEEL_TYPO_FAMILY),
                     role_family(rc, fam, sizeof(fam)));
            break;
        case ER_WEIGHT:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d", S(LANG_WHEEL_TYPO_WEIGHT), rc->weight);
            break;
        default:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", S(LANG_WHEEL_TYPO_SIZE),
                     typo_size_name(rc->size));
            break;
    }
    return buf;
}

static int er_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    struct typo_role_cfg *rc = &edit.role[er_role];
    char fam[40];
    role_family(rc, fam, sizeof(fam));
    switch (gui_synclist_get_sel_pos(lists))
    {
        case ER_FAMILY:
        {
            static const char *names[64];
            int n = typo_family_list(names, 64);
            int sel = 0;
            for (int i = 0; i < n; i++)
                if (!strcasecmp(names[i], fam)) sel = i;
            int k = pick(S(LANG_WHEEL_TYPO_FAMILY), names, n, sel);
            if (k >= 0)
            {
                strlcpy(rc->family, names[k], sizeof(rc->family));
                short w[16];
                int nw = typo_family_weights(names[k], w, 16);
                int best = nw ? w[0] : 400;
                for (int i = 0; i < nw; i++)
                    if (abs(w[i] - rc->weight) < abs(best - rc->weight)) best = w[i];
                rc->weight = best;
            }
            break;
        }
        case ER_WEIGHT:
        {
            short w[16];
            int nw = typo_family_weights(fam, w, 16);
            int i;
            for (i = 0; i < nw && w[i] <= rc->weight; i++);
            /* next heavier, wrapping; families with one weight get +300
             * steps (drawn with synthetic bold) */
            if (nw > 1)
                rc->weight = i < nw ? w[i] : w[0];
            else
                rc->weight = rc->weight >= 700 ? (nw ? w[0] : 400) : rc->weight + 300;
            break;
        }
        case ER_SIZE:
            rc->size = (rc->size + 1) % TS_COUNT;
            break;
    }
    gui_synclist_set_title(lists, (char *)role_label(er_role), Icon_NOICON);
    return ACTION_REDRAW;
}

static void edit_role(int r)
{
    struct simplelist_info info;
    er_role = r;
    simplelist_info_init(&info, (char *)role_label(r), ER_COUNT, NULL);
    info.get_name = er_get_name;
    info.action_callback = er_action;
    simplelist_show_list(&info);
}

/* --- per-screen sizes --- */
static const char *ps_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    static const int ids[3] = { LANG_WHEEL_TYPO_MENUS_TITLE, LANG_WHEEL_TYPO_NP_TITLE, LANG_WHEEL_TYPO_LYRICS };
    snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", S(ids[item]), size_label(edit.ov[item]));
    return buf;
}
static int ps_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    signed char *v = &edit.ov[gui_synclist_get_sel_pos(lists)];
    *v = *v >= TS_XL ? TS_AUTO : *v + 1;
    gui_synclist_set_title(lists, S(LANG_WHEEL_TYPO_SCREENS), Icon_NOICON);
    return ACTION_REDRAW;
}

/* --- line spacing: a style, or each part on its own --- */
enum { LS_STYLE, LS_MENUS, LS_LYRICS, LS_NP, LS_LISTS, LS_PREVIEW, LS_COUNT };

static const char *ls_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    switch (item)
    {
        case LS_STYLE:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", S(LANG_WHEEL_TYPO_LINE_STYLE), line_label(edit.line));
            break;
        case LS_MENUS:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d px", S(LANG_WHEEL_TYPO_LINE_MENUS), edit.row_h);
            break;
        case LS_LYRICS:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d %%", S(LANG_WHEEL_TYPO_LYRICS), edit.ly_pct);
            break;
        case LS_NP:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d %%", S(LANG_WHEEL_TYPO_LINE_NP), edit.np_pct);
            break;
        case LS_LISTS:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d %%", S(LANG_WHEEL_TYPO_LINE_LISTS), edit.list_pct);
            break;
        default:
            return S(LANG_WHEEL_TYPO_PREVIEW);
    }
    return buf;
}

static bool ls_get_nav(int item, void *data)
{
    (void)data;
    return item == LS_PREVIEW;
}

/* steps up, then wraps to the smallest */
static short step(short v, int lo, int hi, int by)
{
    v += by;
    return v > hi ? lo : v;
}

static int ls_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, ls_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    int sel = gui_synclist_get_sel_pos(lists);
    switch (sel)
    {
        case LS_STYLE:
            typo_line_style_apply(&edit, edit.line >= TLS_AIRY || edit.line < 0 ? TLS_COMPACT : edit.line + 1);
            break;
        case LS_MENUS:  edit.row_h = step(edit.row_h, 30, 56, 2); edit.line = TLS_CUSTOM; break;
        case LS_LYRICS: edit.ly_pct = step(edit.ly_pct, 100, 200, 10); edit.line = TLS_CUSTOM; break;
        case LS_NP:     edit.np_pct = step(edit.np_pct, 80, 135, 5); edit.line = TLS_CUSTOM; break;
        case LS_LISTS:  edit.list_pct = step(edit.list_pct, 80, 140, 10); edit.line = TLS_CUSTOM; break;
        case LS_PREVIEW:
        {
            int r = preview(&edit, PM_EDIT);
            if (r == PVR_RESET)
                edit = edit_base;
            else if (r == PVR_APPLY && typo_apply(&edit))
            {
                cu_done = true;
                return ACTION_STD_CANCEL;
            }
            break;
        }
    }
    gui_synclist_set_title(lists, S(LANG_WHEEL_TYPO_LINE), Icon_NOICON);
    return ACTION_REDRAW;
}

static void line_spacing(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, S(LANG_WHEEL_TYPO_LINE), LS_COUNT, NULL);
    info.get_name = ls_get_name;
    info.action_callback = ls_action;
    simplelist_show_list(&info);
}

/* --- the customise list --- */
enum { CU_NAME, CU_ROLE0, CU_SPACING = CU_ROLE0 + TR_COUNT, CU_LINE, CU_NUMERALS, CU_RENDER,
       CU_SCREENS, CU_UIFONT, CU_FALLBACK, CU_PREVIEW, CU_APPLY, CU_SAVE, CU_COUNT };

static const char *cu_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    const char *label = "", *value = "";
    char v[64];
    if (item >= CU_ROLE0 && item < CU_ROLE0 + TR_COUNT)
    {
        int r = item - CU_ROLE0;
        char fam[40];
        snprintf(v, sizeof(v), "%s %d %s", role_family(&edit.role[r], fam, sizeof(fam)),
                 edit.role[r].weight, typo_size_name(edit.role[r].size));
        label = role_label(r);
        value = v;
    }
    else switch (item)
    {
        case CU_NAME: label = S(LANG_WHEEL_TYPO_NAME); value = edit.name; break;
        case CU_SPACING: label = S(LANG_WHEEL_TYPO_SPACING); value = spacing_label(edit.spacing); break;
        case CU_LINE: label = S(LANG_WHEEL_TYPO_LINE); value = line_label(edit.line); break;
        case CU_NUMERALS:
            label = S(LANG_WHEEL_TYPO_NUMERALS);
            value = edit.tabular ? S(LANG_WHEEL_TYPO_TABULAR) : S(LANG_WHEEL_TYPO_PROPORTIONAL);
            break;
        case CU_RENDER: label = S(LANG_WHEEL_TYPO_RENDER); value = render_label(edit.render); break;
        case CU_SCREENS: return S(LANG_WHEEL_TYPO_SCREENS);
        case CU_UIFONT: label = S(LANG_WHEEL_TYPO_UI_FONT); value = edit.ui_font ? S(LANG_ON) : S(LANG_OFF); break;
        case CU_FALLBACK: label = S(LANG_WHEEL_TYPO_FALLBACK); value = edit.fallback ? S(LANG_ON) : S(LANG_OFF); break;
        case CU_PREVIEW: return S(LANG_WHEEL_TYPO_PREVIEW);
        case CU_APPLY: return S(LANG_WHEEL_TYPO_APPLY);
        case CU_SAVE: return S(LANG_WHEEL_TYPO_SAVE_AS);
    }
    snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", label, value);
    return buf;
}

static bool cu_get_nav(int item, void *data)
{
    (void)data;
    return (item >= CU_ROLE0 && item < CU_ROLE0 + TR_COUNT) || item == CU_SCREENS ||
           item == CU_PREVIEW || item == CU_LINE;
}

static int cu_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, cu_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    int sel = gui_synclist_get_sel_pos(lists);
    if (sel >= CU_ROLE0 && sel < CU_ROLE0 + TR_COUNT)
        edit_role(sel - CU_ROLE0);
    else switch (sel)
    {
        case CU_NAME:
        case CU_SAVE:
            save_as();
            break;
        case CU_SPACING: edit.spacing = edit.spacing >= 1 ? -1 : edit.spacing + 1; break;
        case CU_LINE: line_spacing(); break;
        case CU_NUMERALS: edit.tabular = !edit.tabular; break;
        case CU_RENDER: edit.render = (edit.render + 1) % 3; break;
        case CU_UIFONT: edit.ui_font = !edit.ui_font; break;
        case CU_FALLBACK: edit.fallback = !edit.fallback; break;
        case CU_SCREENS:
        {
            struct simplelist_info info;
            simplelist_info_init(&info, S(LANG_WHEEL_TYPO_SCREENS), 3, NULL);
            info.get_name = ps_get_name;
            info.action_callback = ps_action;
            simplelist_show_list(&info);
            break;
        }
        case CU_PREVIEW:
        {
            int r = preview(&edit, PM_EDIT);
            if (r == PVR_RESET)
                edit = edit_base;
            else if (r == PVR_APPLY && typo_apply(&edit))
                cu_done = true;
            break;
        }
        case CU_APPLY:
            if (typo_apply(&edit))
                cu_done = true;
            break;
    }
    if (cu_done)
        return ACTION_STD_CANCEL;
    gui_synclist_set_title(lists, S(LANG_WHEEL_TYPO_CUSTOMIZE), Icon_NOICON);
    return ACTION_REDRAW;
}

static void customize(const struct typo_profile *from)
{
    edit = *from;
    edit_base = *from;
    if (edit.preset)
    {   /* a preset is edited as a new profile */
        edit.preset = false;
        edit.id[0] = '\0';
    }
    cu_done = false;
    struct simplelist_info info;
    simplelist_info_init(&info, S(LANG_WHEEL_TYPO_CUSTOMIZE), CU_COUNT, NULL);
    info.get_name = cu_get_name;
    info.action_callback = cu_action;
    info.selection = CU_ROLE0;
    simplelist_show_list(&info);
}

/* ------------------------------ gallery ------------------------------ */

#define GAL_MAX 24
static struct typo_profile *gal;     /* presets, then saved profiles */
static int gal_n, gal_font[GAL_MAX];

static void gallery_load(void)
{
    if (!gal)
        gal = malloc(sizeof(*gal) * GAL_MAX);
    gal_n = 0;
    if (!gal)
        return;
    int np = typo_preset_count();
    for (int i = 0; i < np && gal_n < GAL_MAX; i++)
        if (typo_preset_get(i, &gal[gal_n]))
            gal_n++;
    static char ids[16][32];
    int n = typo_profile_list(ids, 16);
    for (int i = 0; i < n && gal_n < GAL_MAX; i++)
        if (typo_profile_load(ids[i], &gal[gal_n]))
            gal_n++;
}

/* every entry's name is drawn with its own title font (size L) */
static void gallery_fonts(bool load)
{
    for (int i = 0; i < gal_n; i++)
    {
        if (!load)
        {
            if (gal_font[i] >= 0 && gal_font[i] != FONT_UI)
                font_unload(gal_font[i]);
            gal_font[i] = -1;
            continue;
        }
        struct typo_profile s = gal[i];
        s.ov[TOV_MENUS] = s.role[TR_DISPLAY].size;   /* its own title size */
        char file[64];
        gal_font[i] = FONT_UI;
        if (typo_build_one(&s, TU_DISPLAY_MENU, file, sizeof(file)))
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), FONT_DIR "/%s", file);
            int id = font_load(path);
            if (id >= 0) gal_font[i] = id;
        }
        progress(i + 1, gal_n);
    }
}

static int gal_meta_font;

static void draw_gallery(int sel, int top)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    unsigned fg = global_settings.fg_color, bg = global_settings.bg_color;
    unsigned grey = mix(fg, bg, 55);
    d->set_background(bg);
    d->set_drawmode(DRMODE_SOLID);
    d->clear_viewport();
    d->set_foreground(mix(fg, bg, 4));
    d->fillrect(0, 0, LCD_WIDTH, 38);
    d->set_drawmode(DRMODE_FG);
    text(d, &vp, typo_font(TU_DISPLAY_MENU), fg, 16, 2, S(LANG_WHEEL_TYPO_GALLERY), 0);
    const struct typo_profile *act = typo_active();
    const int H = 64;
    for (int i = top, y = 38; i < gal_n && y < LCD_HEIGHT; i++, y += H)
    {
        unsigned col = fg, sub = grey;
        if (i == sel)
        {
            d->set_drawmode(DRMODE_SOLID);
            d->set_foreground(global_settings.lse_color);
            d->fillrect(0, y, LCD_WIDTH, H);
            d->set_drawmode(DRMODE_FG);
            col = sub = LCD_RGBPACK(255, 255, 255);
        }
        text(d, &vp, gal_font[i], col, 16, y + 2, gal[i].name, LCD_WIDTH - 60);
        /* the fonts it will really use (imported ones included) */
        char note[128] = "";
        char seen[TR_COUNT][40];
        int ns = 0;
        for (int r = 0; r < TR_COUNT; r++)
        {
            char fam[40];
            role_family(&gal[i].role[r], fam, sizeof(fam));
            int k;
            for (k = 0; k < ns && strcmp(seen[k], fam); k++);
            if (k < ns)
                continue;
            strlcpy(seen[ns++], fam, sizeof(seen[0]));
            size_t l = strlen(note);
            snprintf(note + l, sizeof(note) - l, "%s%s", l ? " + " : "", fam);
        }
        text(d, &vp, gal_meta_font, sub, 16, y + 38, note, LCD_WIDTH - 60);
        if (typo_is_custom() && !strcmp(act->id, gal[i].id))
            text(d, &vp, gal_meta_font, sub, LCD_WIDTH - 30, y + 20, "•", 0);
    }
    d->update_viewport();
    d->set_viewport(last);
}

static void gallery(void)
{
    gallery_load();
    if (!gal_n)
    {
        splash(HZ * 2, ID2P(LANG_WHEEL_TYPO_FAILED));
        return;
    }
    gal_meta_font = typo_font(TU_META);
    gallery_fonts(true);
    int sel = 0, top = 0;
    const struct typo_profile *act = typo_active();
    for (int i = 0; i < gal_n; i++)
        if (!strcmp(act->id, gal[i].id)) sel = i;
    const int visible = (LCD_HEIGHT - 38) / 64;
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    while (1)
    {
        if (sel < top) top = sel;
        if (sel >= top + visible) top = sel - visible + 1;
        draw_gallery(sel, top);
        int a = get_action(CONTEXT_STD, TIMEOUT_BLOCK);
        if (a == ACTION_STD_NEXT || a == ACTION_STD_NEXTREPEAT)
            sel = (sel + 1) % gal_n;
        else if (a == ACTION_STD_PREV || a == ACTION_STD_PREVREPEAT)
            sel = (sel + gal_n - 1) % gal_n;
        else if (a == ACTION_STD_OK)
        {
            FOR_NB_SCREENS(i)
                viewportmanager_theme_undo(i, false);
            int r = preview(&gal[sel], PM_GALLERY);
            bool leave = false;
            if (r == PVR_APPLY)
                leave = typo_apply(&gal[sel]);
            else if (r == PVR_CUSTOMIZE)
            {
                customize(&gal[sel]);
                leave = true;
            }
            if (leave)
                break;
            FOR_NB_SCREENS(i)
                viewportmanager_theme_enable(i, false, NULL);
        }
        else if (a == ACTION_STD_CANCEL || a == ACTION_STD_MENU)
        {
            FOR_NB_SCREENS(i)
                viewportmanager_theme_undo(i, true);
            break;
        }
        else if (a == ACTION_STD_CONTEXT)
        {   /* hold: customise this one */
            FOR_NB_SCREENS(i)
                viewportmanager_theme_undo(i, false);
            customize(&gal[sel]);
            break;
        }
        else if (default_event_handler(a) == SYS_USB_CONNECTED)
            break;
    }
    gallery_fonts(false);
}

/* ------------------------------ profiles ------------------------------ */

static char prof_ids[16][32];
static char prof_names[16][32];
static int prof_n;

static void prof_load(void)
{
    prof_n = typo_profile_list(prof_ids, 16);
    for (int i = 0; i < prof_n; i++)
    {
        struct typo_profile p;
        if (typo_profile_load(prof_ids[i], &p))
            strlcpy(prof_names[i], p.name, sizeof(prof_names[i]));
        else
            strlcpy(prof_names[i], prof_ids[i], sizeof(prof_names[i]));
    }
}

static const char *pr_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data; (void)buf; (void)len;
    if (item == 0)
        return S(LANG_WHEEL_TYPO_NEW_PROFILE);
    if (prof_n == 0)
        return S(LANG_WHEEL_TYPO_NO_PROFILES);
    return prof_names[item - 1];
}

static void profile_menu(int i)
{
    struct typo_profile p;
    if (!typo_profile_load(prof_ids[i], &p))
        return;
    const char *items[] = { S(LANG_WHEEL_TYPO_APPLY), S(LANG_WHEEL_TYPO_PREVIEW),
                            S(LANG_WHEEL_TYPO_CUSTOMIZE), S(LANG_WHEEL_TYPO_RENAME),
                            S(LANG_WHEEL_TYPO_DELETE) };
    switch (pick(p.name, items, 5, 0))
    {
        case 0: typo_apply(&p); break;
        case 1:
            switch (preview(&p, PM_GALLERY))
            {
                case PVR_APPLY: typo_apply(&p); break;
                case PVR_CUSTOMIZE: customize(&p); break;
            }
            break;
        case 2: customize(&p); break;
        case 3:
        {
            char name[32];
            strlcpy(name, p.name, sizeof(name));
            if (kbd_input(name, sizeof(name), NULL) >= 0 && name[0])
                typo_profile_rename(p.id, name);
            break;
        }
        case 4:
            if (yesno_pop(ID2P(LANG_WHEEL_TYPO_DELETE_ASK)))
                typo_profile_delete(p.id);
            break;
    }
}

static int pr_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    int sel = gui_synclist_get_sel_pos(lists);
    if (sel == 0)
    {   /* the profile in use, under a new name */
        struct typo_profile p = *typo_active();
        char name[32] = "";
        if (kbd_input(name, sizeof(name), NULL) >= 0 && name[0])
        {
            strlcpy(p.name, name, sizeof(p.name));
            typo_slug(name, p.id, sizeof(p.id));
            p.preset = false;
            p.note[0] = '\0';
            if (typo_profile_save(&p))
                splash(HZ, ID2P(LANG_WHEEL_TYPO_SAVED));
        }
    }
    else if (prof_n > 0)
        profile_menu(sel - 1);
    prof_load();
    gui_synclist_set_nb_items(lists, 1 + MAX(prof_n, 1));
    gui_synclist_select_item(lists, MIN(sel, prof_n));
    gui_synclist_set_title(lists, S(LANG_WHEEL_TYPO_PROFILES), Icon_NOICON);
    return ACTION_REDRAW;
}

static void profiles(void)
{
    prof_load();
    struct simplelist_info info;
    simplelist_info_init(&info, S(LANG_WHEEL_TYPO_PROFILES), 1 + MAX(prof_n, 1), NULL);
    info.get_name = pr_get_name;
    info.action_callback = pr_action;
    simplelist_show_list(&info);
}

/* -------------------------- font management -------------------------- */

static const char *fams[64];
static int n_fams;

static const char *fl_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    short w[16];
    int nw = typo_family_weights(fams[item], w, 16);
    bool imp = false, bitmap = false;
    for (int i = 0; i < typo_face_count(); i++)
    {
        const struct typo_face *f = typo_face(i);
        if (strcasecmp(f->family, fams[item])) continue;
        imp |= f->imported;
        bitmap |= !f->outline;
    }
    if (bitmap)
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "bitmap", fams[item]);
    else
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d%s", fams[item], nw, imp ? " · +" : "");
    return buf;
}

/* details of one family, as list lines */
static char det[12][64];
static int det_n;

static const char *det_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data; (void)buf; (void)len;
    return det[item];
}

static void family_details(const char *family)
{
    det_n = 0;
    short w[16];
    int nw = typo_family_weights(family, w, 16);
    char ws[48] = "";
    for (int i = 0; i < nw; i++)
    {
        size_t l = strlen(ws);
        snprintf(ws + l, sizeof(ws) - l, "%s%d", i ? ", " : "", w[i]);
    }
    const struct typo_face *first = NULL;
    long bytes = 0;
    bool variable = false;
    for (int i = 0; i < typo_face_count(); i++)
    {
        const struct typo_face *f = typo_face(i);
        if (strcasecmp(f->family, family)) continue;
        if (!first) first = f;
        bytes += f->bytes;
        variable |= f->variable;
    }
    if (!first)
        return;
    snprintf(det[det_n++], 64, "%s: %s", S(LANG_WHEEL_TYPO_SOURCE),
             first->imported ? S(LANG_WHEEL_TYPO_IMPORTED_SRC)
             : first->outline ? S(LANG_WHEEL_TYPO_BUNDLED) : S(LANG_WHEEL_TYPO_BITMAP));
    snprintf(det[det_n++], 64, "%s: %s", S(LANG_WHEEL_TYPO_WEIGHTS), ws);
    snprintf(det[det_n++], 64, "%s: %ld KB", S(LANG_WHEEL_TYPO_DISK), (bytes + 1023) / 1024);
    if (variable)
        strlcpy(det[det_n++], S(LANG_WHEEL_TYPO_VARIABLE), 64);
    /* coverage of the first file */
    long n = 0;
    int fd = open(first->path, O_RDONLY);
    char *data = NULL;
    if (fd >= 0)
    {
        n = ffilesize(fd);
        data = n > 0 && n < (8L << 20) ? malloc(n) : NULL;
        if (data && read(fd, data, n) != n) { free(data); data = NULL; }
        close(fd);
    }
    if (data)
    {
        struct forge_src s = { (const unsigned char *)data, n };
        struct forge_info fi;
        if (forge_info(&s, &fi))
        {
            static const char * const bn[FB_COUNT] = { "Latin", "Latin-1", "Latin Ext-A",
                                                        "Greek / Ελληνικά", "Cyrillic / Кириллица",
                                                        "Punctuation" };
            strlcpy(det[det_n++], S(LANG_WHEEL_TYPO_COVERAGE), 64);
            for (int b = 0; b < FB_COUNT && det_n < 12; b++)
                snprintf(det[det_n++], 64, "  %s" LIST_VALUE_SEP_STR "%d%%", bn[b],
                         fi.cover_of[b] ? fi.cover[b] * 100 / fi.cover_of[b] : 0);
        }
        free(data);
    }
    struct simplelist_info info;
    simplelist_info_init(&info, (char *)family, det_n, NULL);
    info.get_name = det_get_name;
    info.action_callback = pick_action;
    simplelist_show_list(&info);
}

static int fl_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    family_details(fams[gui_synclist_get_sel_pos(lists)]);
    gui_synclist_set_title(lists, S(LANG_WHEEL_TYPO_INSTALLED), Icon_NOICON);
    return ACTION_REDRAW;
}

static void installed_fonts(void)
{
    n_fams = typo_family_list(fams, 64);
    struct simplelist_info info;
    simplelist_info_init(&info, S(LANG_WHEEL_TYPO_INSTALLED), n_fams, NULL);
    info.get_name = fl_get_name;
    info.action_callback = fl_action;
    simplelist_show_list(&info);
}

static bool copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0)
        return false;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0)
    {
        close(in);
        return false;
    }
    static char buf[8192];
    ssize_t n;
    bool ok = true;
    while ((n = read(in, buf, sizeof(buf))) > 0)
        if (write(out, buf, n) != n) { ok = false; break; }
    close(in);
    close(out);
    if (!ok) remove(dst);
    return ok;
}

/* fonts lying in Download/ or Fonts/: copy the chosen one to fonts/import */
static void import_fonts(void)
{
    static const char * const places[] = { HOME_DIR "/Download", HOME_DIR "/Downloads",
                                           HOME_DIR "/Fonts", HOME_DIR "/fonts", HOME_DIR };
    static char found[24][MAX_PATH];
    static const char *labels[24];
    int n = 0;
    for (unsigned p = 0; p < ARRAYLEN(places) && n < 24; p++)
    {
        DIR *d = opendir(places[p]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) && n < 24)
        {
            const char *nm = (const char *)e->d_name;
            const char *x = strrchr(nm, '.');
            if (nm[0] == '.' || !x) continue;
            if (strcasecmp(x, ".ttf") && strcasecmp(x, ".otf") && strcasecmp(x, ".fnt")) continue;
            snprintf(found[n], MAX_PATH, "%s/%s", places[p], nm);
            labels[n] = strrchr(found[n], '/') + 1;
            n++;
        }
        closedir(d);
    }
    if (!n)
    {
        splash(HZ * 3, ID2P(LANG_WHEEL_TYPO_IMPORT_NONE));
        splash(HZ * 3, ID2P(LANG_WHEEL_TYPO_IMPORT_HINT));
        return;
    }
    int k = pick(S(LANG_WHEEL_TYPO_IMPORT), labels, n, 0);
    if (k < 0)
        return;
    mkdir(FONT_DIR);
    mkdir(TYPO_IMPORT);
    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), TYPO_IMPORT "/%s", labels[k]);
    struct forge_info fi;
    bool ok = copy_file(found[k], dst);
    if (ok)
    {   /* check it is a font we can read */
        int fd = open(dst, O_RDONLY);
        long sz = fd >= 0 ? ffilesize(fd) : 0;
        char *data = sz > 0 && sz < (8L << 20) ? malloc(sz) : NULL;
        if (data && read(fd, data, sz) == sz)
        {
            struct forge_src s = { (const unsigned char *)data, sz };
            ok = forge_info(&s, &fi);
        }
        else
            ok = false;
        free(data);
        if (fd >= 0) close(fd);
        if (!ok) remove(dst);
    }
    typo_registry_scan(true);
    splash(HZ * 2, ok ? ID2P(LANG_WHEEL_TYPO_IMPORTED) : ID2P(LANG_WHEEL_TYPO_FAILED));
}

static const char *compat_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data; (void)buf; (void)len;
    static const int ids[] = { LANG_WHEEL_TYPO_COMPAT_1, LANG_WHEEL_TYPO_COMPAT_2,
                               LANG_WHEEL_TYPO_VARIABLE, LANG_WHEEL_TYPO_COMPAT_4,
                               LANG_WHEEL_TYPO_COMPAT_5, LANG_WHEEL_TYPO_COMPAT_6,
                               LANG_WHEEL_TYPO_COMPAT_7, LANG_WHEEL_TYPO_IMPORT_HINT };
    return S(ids[item]);
}

enum { FM_INSTALLED, FM_IMPORT, FM_RESCAN, FM_MEMORY, FM_CACHE, FM_FALLBACK, FM_CJK, FM_COMPAT, FM_COUNT };

static const char *fm_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    switch (item)
    {
        case FM_INSTALLED:
        {
            const char *tmp[64];
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d", S(LANG_WHEEL_TYPO_INSTALLED),
                     typo_family_list(tmp, 64));
            return buf;
        }
        case FM_IMPORT: return S(LANG_WHEEL_TYPO_IMPORT);
        case FM_RESCAN: return S(LANG_WHEEL_TYPO_RESCAN);
        case FM_MEMORY:
        {
            int n;
            long b = typo_loaded_bytes(&n);
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d · %ld KB", S(LANG_WHEEL_TYPO_MEMORY),
                     n, (b + 1023) / 1024);
            return buf;
        }
        case FM_CACHE:
        {
            int n;
            long b = typo_cache_bytes(&n);
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%d · %ld KB", S(LANG_WHEEL_TYPO_CACHE),
                     n, (b + 1023) / 1024);
            return buf;
        }
        case FM_FALLBACK:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", S(LANG_WHEEL_TYPO_FALLBACK_FONT),
                     typo_family_exists(TYPO_FALLBACK) ? TYPO_FALLBACK : "-");
            return buf;
        case FM_CJK:
            /* 中文 · 日本語 · 한국어 come from the fallback fonts */
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", "中文 · 日本語 · 한국어",
                     typo_cjk_fonts() >= 6 ? (const char *)S(LANG_WHEEL_TYPO_CJK_READY)
                                            : typo_cjk_fonts() ? "…" : "-");
            return buf;
        case FM_COMPAT: return S(LANG_WHEEL_TYPO_COMPAT);
    }
    return "";
}

static bool fm_get_nav(int item, void *data)
{
    (void)data;
    return item == FM_INSTALLED || item == FM_COMPAT;
}

static int fm_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, fm_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    switch (gui_synclist_get_sel_pos(lists))
    {
        case FM_INSTALLED: installed_fonts(); break;
        case FM_IMPORT: import_fonts(); break;
        case FM_RESCAN:
            typo_registry_scan(true);
            splashf(HZ, "%d", typo_face_count());
            break;
        case FM_CACHE:
            if (yesno_pop(ID2P(LANG_WHEEL_TYPO_CLEAR_CACHE)))
                typo_cache_clear();
            break;
        case FM_COMPAT:
        {
            struct simplelist_info info;
            simplelist_info_init(&info, S(LANG_WHEEL_TYPO_COMPAT), 8, NULL);
            info.get_name = compat_get_name;
            info.action_callback = pick_action;
            simplelist_show_list(&info);
            break;
        }
    }
    gui_synclist_set_title(lists, S(LANG_WHEEL_TYPO_FONTS), Icon_NOICON);
    return ACTION_REDRAW;
}

static void font_management(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, S(LANG_WHEEL_TYPO_FONTS), FM_COUNT, NULL);
    info.get_name = fm_get_name;
    info.action_callback = fm_action;
    simplelist_show_list(&info);
}

/* ---------------------------- main screen ---------------------------- */

enum { TY_THEME, TY_CUSTOMIZE, TY_PREVIEW, TY_PROFILES, TY_FONTS, TY_RESTORE, TY_COUNT };

static const char *ty_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    switch (item)
    {
        case TY_THEME:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", S(LANG_WHEEL_TYPO_THEME),
                     typo_is_custom() ? typo_active()->name : "Wheel");
            return buf;
        case TY_CUSTOMIZE: return S(LANG_WHEEL_TYPO_CUSTOMIZE);
        case TY_PREVIEW: return S(LANG_WHEEL_TYPO_PREVIEW);
        case TY_PROFILES: return S(LANG_WHEEL_TYPO_PROFILES);
        case TY_FONTS: return S(LANG_WHEEL_TYPO_FONTS);
        case TY_RESTORE: return S(LANG_WHEEL_TYPO_RESTORE);
    }
    return "";
}

static bool ty_get_nav(int item, void *data)
{
    (void)data;
    return item != TY_RESTORE;
}

static int ty_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, ty_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    switch (gui_synclist_get_sel_pos(lists))
    {
        case TY_THEME: gallery(); break;
        case TY_CUSTOMIZE: customize(typo_active()); break;
        case TY_PREVIEW:
        {
            if (preview(typo_active(), PM_VIEW) == PVR_CUSTOMIZE)
                customize(typo_active());
            break;
        }
        case TY_PROFILES: profiles(); break;
        case TY_FONTS: font_management(); break;
        case TY_RESTORE:
            if (typo_is_custom() && yesno_pop(ID2P(LANG_WHEEL_TYPO_RESTORE_ASK)))
                typo_restore_default();
            break;
    }
    gui_synclist_set_title(lists, S(LANG_WHEEL_TYPO), Icon_NOICON);
    return ACTION_REDRAW;
}

int wheel_typography_screen(void)
{
    struct simplelist_info info;
    typo_registry_scan(false);
    simplelist_info_init(&info, S(LANG_WHEEL_TYPO), TY_COUNT, NULL);
    info.get_name = ty_get_name;
    info.action_callback = ty_action;
    simplelist_show_list(&info);
    return 0;
}
