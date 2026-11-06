/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - personalisation: the finish, a background image, Now
 * Playing layouts, the main menu, saved looks (and day/night looks), the
 * wheel and its click, the idle screen and the startup image.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
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
#include "settings.h"
#include "settings_list.h"
#include "splash.h"
#include "misc.h"
#include "file.h"
#include "dir.h"
#include "font.h"
#include "rbpaths.h"
#include "pathfuncs.h"
#include "string-extra.h"
#include "screens.h"
#include "viewport.h"
#include "audio.h"
#include "metadata.h"
#include "timefuncs.h"
#include "keyboard.h"
#include "root_menu.h"
#include "menu.h"
#include "bmp.h"
#include "jpeg_load.h"
#include "option_select.h"
#include "skin_engine/skin_engine.h"
#include "wheel_custom.h"
#include "wheel_screens.h"
#include "wheel_palette.h"
#include "wheel_typo.h"
#include "wheel_gel.h"
#include "wheel_art.h"
#include "wheel_listview.h"

#define WHEEL_DIR   ROCKBOX_DIR "/wheel"
#define LOOKS_DIR   WHEEL_DIR "/looks"
#define BOOT_BMP    WHEEL_DIR "/boot.bmp"
#define BOOT_TXT    WHEEL_DIR "/boot.txt"
#define BOOT_PIC    WHEEL_DIR "/boot-picture.txt"

/* ------------------------------ option lists ------------------------------ */
/* a row: a setting cycled with OK, or a screen to open */
struct opt {
    int label;                 /* LANG id */
    int *ivar;                 /* an int setting with count choices... */
    bool *bvar;                /* ...or a bool one */
    int count;
    const int *choices;        /* LANG ids of the choices */
    void (*changed)(void);     /* after a change */
    void (*open)(void);        /* opens something instead */
    const char *(*text)(char *buf, size_t len);   /* value shown */
};

static const struct opt *cur_opts;
static int cur_n, cur_title;

static const char *opt_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    const struct opt *o = &cur_opts[item];
    char val[64] = "";
    if (o->text)
        o->text(val, sizeof(val));
    else if (o->ivar && o->choices)
        strlcpy(val, str(o->choices[MAX(0, MIN(o->count - 1, *o->ivar))]), sizeof(val));
    else if (o->ivar)
        snprintf(val, sizeof(val), "%d", *o->ivar);
    else if (o->bvar)
        strlcpy(val, str(*o->bvar ? LANG_ON : LANG_OFF), sizeof(val));
    if (val[0])
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(o->label), val);
    else
        strlcpy(buf, str(o->label), len);
    return buf;
}

static bool opt_nav(int item, void *data)
{
    (void)data;
    return cur_opts[item].open != NULL;
}

static int opt_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, opt_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    const struct opt *saved = cur_opts;
    int n = cur_n, title = cur_title;
    const struct opt *o = &cur_opts[gui_synclist_get_sel_pos(lists)];
    if (o->open)
        o->open();
    else if (o->ivar)
        *o->ivar = (*o->ivar + 1) % o->count;
    else if (o->bvar)
        *o->bvar = !*o->bvar;
    if (!o->open)
    {
        settings_save();
        if (o->changed)
            o->changed();
    }
    cur_opts = saved;   /* (a screen opened from here may have used them) */
    cur_n = n;
    cur_title = title;
    gui_synclist_set_title(lists, str(cur_title), Icon_NOICON);
    return ACTION_REDRAW;
}

static void options_screen(int title, const struct opt *opts, int n)
{
    const struct opt *saved = cur_opts;
    int sn = cur_n, st = cur_title;
    cur_opts = opts;
    cur_n = n;
    cur_title = title;
    struct simplelist_info info;
    simplelist_info_init(&info, str(title), n, NULL);
    info.get_name = opt_name;
    info.action_callback = opt_action;
    simplelist_show_list(&info);
    cur_opts = saved;
    cur_n = sn;
    cur_title = st;
}

/* the skins are generated from these settings: make them again */
static void remake(void)
{
    splash(0, ID2P(LANG_WAIT));
    wheel_appearance_apply(true);
}

/* only a %St() or a drawing changed: redraw */
static void redraw(void)
{
    skin_request_full_update(WPS);
    skin_request_full_update(CUSTOM_STATUSBAR);
    viewportmanager_theme_changed(THEME_STATUSBAR);
}

/* --------------------------------- finish --------------------------------- */
static const int finish_names[] = { LANG_WHEEL_FINISH_MATTE, LANG_WHEEL_FINISH_AERO };
static const int gloss_names[] = { LANG_WHEEL_GLOSS_0, LANG_WHEEL_GLOSS_1, LANG_WHEEL_GLOSS_2 };
static const int corner_names[] = { LANG_WHEEL_CORNERS_0, LANG_WHEEL_CORNERS_1,
                                    LANG_WHEEL_CORNERS_2 };

static const char *finish_text(char *buf, size_t len)
{
    strlcpy(buf, str(finish_names[global_settings.wheel_aero ? 1 : 0]), len);
    return buf;
}

void wheel_finish_screen(void)
{
    static const struct opt opts[] = {
        { LANG_WHEEL_FINISH, NULL, &global_settings.wheel_aero, 0, NULL, remake, NULL, finish_text },
        { LANG_WHEEL_GLOSS, &global_settings.wheel_gloss, NULL, 3, gloss_names, remake, NULL, NULL },
        { LANG_WHEEL_REFLECT, NULL, &global_settings.wheel_reflect, 0, NULL, remake, NULL, NULL },
        { LANG_WHEEL_CORNERS, &global_settings.wheel_corners, NULL, 3, corner_names, redraw, NULL, NULL },
    };
    options_screen(LANG_WHEEL_FINISH, opts, ARRAYLEN(opts));
}

/* ------------------------------ picture picker ------------------------------ */
/* .jpg / .bmp files in the folders people keep pictures in */
static int pick_picture(int title, char *out, size_t len)
{
    static const char * const places[] = {
        HOME_DIR, HOME_DIR "/Pictures", HOME_DIR "/DCIM", HOME_DIR "/DCIM/Camera",
        HOME_DIR "/Download", HOME_DIR "/Downloads", HOME_DIR "/Wallpapers",
        HOME_DIR "/Images",
    };
    enum { MAXP = 48 };
    static char paths[MAXP][MAX_PATH];
    static char names[MAXP][64];
    const char *items[MAXP];
    int n = 0;
    for (unsigned p = 0; p < ARRAYLEN(places) && n < MAXP; p++)
    {
        DIR *d = opendir(places[p]);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d)) && n < MAXP)
        {
            const char *nm = (const char *)e->d_name;
            const char *x = strrchr(nm, '.');
            if (nm[0] == '.' || !x || (strcasecmp(x, ".jpg") && strcasecmp(x, ".jpeg") &&
                                       strcasecmp(x, ".bmp")))
                continue;
            snprintf(paths[n], MAX_PATH, "%s/%s", places[p], nm);
            strlcpy(names[n], nm, 64);
            items[n] = names[n];
            n++;
        }
        closedir(d);
    }
    if (!n)
    {
        splash(HZ * 3, ID2P(LANG_WHEEL_WALL_BAD));
        return -1;
    }
    int pick = wheel_pick(str(title), items, n, 0);
    if (pick >= 0)
        strlcpy(out, paths[pick], len);
    return pick;
}

/* ------------------------------- background ------------------------------- */
static const int wall_names[] = { LANG_WHEEL_WALL_S0, LANG_WHEEL_WALL_S1, LANG_WHEEL_WALL_S2 };

static const char *wall_text(char *buf, size_t len)
{
    const char *p = global_settings.wheel_wallpaper;
    const char *slash = strrchr(p, '/');
    strlcpy(buf, p[0] ? (slash ? slash + 1 : p) : (const char *)str(LANG_WHEEL_WALL_NONE), len);
    return buf;
}

static void wall_pick(void)
{
    char path[MAX_PATH];
    if (pick_picture(LANG_WHEEL_WALLPAPER, path, sizeof(path)) < 0)
        return;
    splash(0, ID2P(LANG_WAIT));
    if (!wheel_wallpaper_check(path))
    {
        splash(HZ * 3, ID2P(LANG_WHEEL_WALL_BAD));
        return;
    }
    settings_save();
    remake();
}

static void wall_remove(void)
{
    if (!global_settings.wheel_wallpaper[0])
        return;
    global_settings.wheel_wallpaper[0] = '\0';
    settings_save();
    remake();
}

static void remake_if_wall(void)
{
    if (global_settings.wheel_wallpaper[0])
        remake();
}

void wheel_background_screen(void)
{
    static const struct opt opts[] = {
        { LANG_WHEEL_WALLPAPER, NULL, NULL, 0, NULL, NULL, wall_pick, wall_text },
        { LANG_WHEEL_WALL_STRENGTH, &global_settings.wheel_wall_strength, NULL, 3, wall_names,
          remake_if_wall, NULL, NULL },
        { LANG_WHEEL_WALL_BLUR, NULL, &global_settings.wheel_wall_blur, 0, NULL, remake_if_wall,
          NULL, NULL },
        { LANG_WHEEL_WALL_REMOVE, NULL, NULL, 0, NULL, NULL, wall_remove, NULL },
    };
    options_screen(LANG_WHEEL_WALLPAPER, opts, ARRAYLEN(opts));
}

/* ------------------------------- Now Playing ------------------------------- */
static const int layout_names[] = { LANG_WHEEL_NP_L0, LANG_WHEEL_NP_L1, LANG_WHEEL_NP_L2,
                                    LANG_WHEEL_NP_L3 };
static const int bar_names[] = { LANG_WHEEL_NP_BAR_0, LANG_WHEEL_NP_BAR_1 };

void wheel_np_screen(void)
{
    static const struct opt opts[] = {
        { LANG_WHEEL_NP_LAYOUT, &global_settings.wheel_np_layout, NULL, 4, layout_names, remake,
          NULL, NULL },
        { LANG_WHEEL_NP_ALBUM, NULL, &global_settings.wheel_np_album, 0, NULL, redraw, NULL, NULL },
        { LANG_WHEEL_NP_DETAILS, NULL, &global_settings.wheel_np_details, 0, NULL, redraw, NULL, NULL },
        { LANG_WHEEL_NP_NEXT, NULL, &global_settings.wheel_np_next, 0, NULL, redraw, NULL, NULL },
        { LANG_WHEEL_NP_BAR, &global_settings.wheel_np_bar, NULL, 2, bar_names, redraw, NULL, NULL },
    };
    options_screen(LANG_WHEEL_NOW_PLAYING, opts, ARRAYLEN(opts));
}

/* -------------------------------- main menu -------------------------------- */
static const int pane_names[] = { LANG_WHEEL_PANE_0, LANG_WHEEL_PANE_1, LANG_WHEEL_UP_NEXT,
                                  LANG_WHEEL_PANE_3 };

struct mm_item { int index; bool shown; };
static struct mm_item mm[32];
static int mm_n;

static const char *mm_name(int i)
{
    int n;
    struct menu_table *t = root_menu_get_options(&n);
    const struct menu_item_ex *item = t[mm[i].index].item;
    if (item->flags & MENU_HAS_DESC)
        return P2STR(item->callback_and_desc->desc);
    if (!strcmp(t[mm[i].index].string, "wps"))
        return str(LANG_NOW_PLAYING);
    return t[mm[i].index].string;
}

static void mm_load(void)
{
    int n;
    struct menu_table *t = root_menu_get_options(&n);
    char cfg[256] = "";
    root_menu_write_to_cfg(NULL, cfg, sizeof(cfg));
    mm_n = 0;
    /* the ones in the menu, in order... */
    char *save = NULL;
    for (char *tok = strtok_r(cfg, ", ", &save); tok && mm_n < (int)ARRAYLEN(mm);
         tok = strtok_r(NULL, ", ", &save))
        for (int i = 0; i < n; i++)
            if (!strcmp(tok, t[i].string))
            {
                mm[mm_n].index = i;
                mm[mm_n++].shown = true;
                break;
            }
    /* ...then the rest */
    for (int i = 0; i < n && mm_n < (int)ARRAYLEN(mm); i++)
    {
        bool in = false;
        for (int k = 0; k < mm_n; k++)
            if (mm[k].index == i)
                in = true;
        if (!in)
        {
            mm[mm_n].index = i;
            mm[mm_n++].shown = false;
        }
    }
}

static void mm_save(void)
{
    int n;
    struct menu_table *t = root_menu_get_options(&n);
    char cfg[256] = "";
    size_t o = 0;
    for (int i = 0; i < mm_n; i++)
        if (mm[i].shown)
            o += snprintf(cfg + o, sizeof(cfg) - o, "%s%s", o ? ", " : "", t[mm[i].index].string);
    root_menu_load_from_cfg(&global_settings.root_menu_customized, cfg);
    settings_save();
}

static void mm_draw(int sel, int top, bool moving)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    unsigned fg = global_settings.fg_color, bg = global_settings.bg_color, barfg;
    d->set_drawmode(DRMODE_SOLID);
    d->set_background(bg);
    d->clear_viewport();
    wheel_top_bar(d, LCD_WIDTH, 38, &barfg);
    d->set_drawmode(DRMODE_FG);
    vp.font = typo_font(TU_DISPLAY_MENU);
    d->set_foreground(barfg);
    d->putsxy(16, 2, (unsigned char *)str(LANG_WHEEL_MENU_ITEMS));
    vp.font = typo_font(TU_META);
    d->set_foreground(wheel_blend(fg, bg, 45));
    d->putsxy(16, 44, (unsigned char *)str(moving ? LANG_WHEEL_MENU_MOVING : LANG_WHEEL_MENU_HINT));
    const int RH = 40, Y0 = 70;
    for (int i = top, y = Y0; i < mm_n && y + RH <= LCD_HEIGHT; i++, y += RH)
    {
        unsigned col = mm[i].shown ? fg : wheel_blend(fg, bg, 55);
        if (i == sel)
        {
            d->set_drawmode(DRMODE_SOLID);
            if (moving)
            {   /* lifted: an outline */
                d->set_foreground(global_settings.lse_color);
                d->drawrect(4, y, LCD_WIDTH - 8, RH);
                d->drawrect(5, y + 1, LCD_WIDTH - 10, RH - 2);
            }
            else
            {
                wheel_selection(d, 0, y, LCD_WIDTH, RH);
                col = global_settings.lst_color;
            }
        }
        d->set_drawmode(DRMODE_FG);
        vp.font = typo_font(TU_ROW);
        d->set_foreground(col);
        int fh = font_get(vp.font)->height;
        d->putsxy(52, y + (RH - fh) / 2, (unsigned char *)mm_name(i));
        /* a tick for the ones shown */
        d->set_drawmode(DRMODE_SOLID);
        int cx = 26, cy = y + RH / 2;
        d->set_foreground(col);
        d->drawrect(cx - 9, cy - 9, 18, 18);
        if (mm[i].shown)
            d->fillrect(cx - 5, cy - 5, 10, 10);
    }
    d->update_viewport();
    d->set_viewport(last);
}

static void mm_items(void)
{
    mm_load();
    int sel = 0, top = 0;
    bool moving = false, changed = false;
    const int visible = (LCD_HEIGHT - 70) / 40;
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    while (1)
    {
        if (sel < top) top = sel;
        if (sel >= top + visible) top = sel - visible + 1;
        mm_draw(sel, top, moving);
        int a = get_action(CONTEXT_STD, TIMEOUT_BLOCK);
        int step = (a == ACTION_STD_NEXT || a == ACTION_STD_NEXTREPEAT) ? 1 :
                   (a == ACTION_STD_PREV || a == ACTION_STD_PREVREPEAT) ? -1 : 0;
        if (step)
        {
            int to = sel + step;
            if (to < 0 || to >= mm_n)
                continue;
            if (moving)
            {
                struct mm_item t = mm[sel];
                mm[sel] = mm[to];
                mm[to] = t;
                changed = true;
            }
            sel = to;
        }
        else if (a == ACTION_STD_CONTEXT)
            moving = !moving;
        else if (a == ACTION_STD_OK)
        {
            if (moving)
                moving = false;
            else
            {   /* (Settings always stays: it's the way back here) */
                int n;
                struct menu_table *t = root_menu_get_options(&n);
                if (strcmp(t[mm[sel].index].string, "settings"))
                {
                    mm[sel].shown = !mm[sel].shown;
                    changed = true;
                }
            }
        }
        else if (a == ACTION_STD_CANCEL || a == ACTION_STD_MENU)
        {
            if (moving)
                moving = false;
            else
                break;
        }
        else if (default_event_handler(a) == SYS_USB_CONNECTED)
            break;
    }
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, false);
    if (changed)
        mm_save();
}

static void mm_reset(void)
{
    root_menu_set_default(&global_settings.root_menu_customized, NULL);
    settings_save();
    splash(HZ, ID2P(LANG_WHEEL_MENU_RESET));
}

void wheel_mainmenu_screen(void)
{
    static const struct opt opts[] = {
        { LANG_WHEEL_PANE, &global_settings.wheel_pane, NULL, 4, pane_names, redraw, NULL, NULL },
        { LANG_WHEEL_MENU_ITEMS, NULL, NULL, 0, NULL, NULL, mm_items, NULL },
        { LANG_WHEEL_MENU_RESET, NULL, NULL, 0, NULL, NULL, mm_reset, NULL },
    };
    options_screen(LANG_WHEEL_MAIN_MENU, opts, ARRAYLEN(opts));
}

/* ---------------------------------- looks ---------------------------------- */
/* everything a look sets (the typography profile is saved apart) */
static void *const look_settings[] = {
    &global_settings.wheel_palette, &global_settings.wheel_aero, &global_settings.wheel_gloss,
    &global_settings.wheel_corners, &global_settings.wheel_reflect,
    &global_settings.wheel_custom_bar, &global_settings.wheel_custom_bg,
    &global_settings.wheel_custom_accent, global_settings.wheel_wallpaper,
    &global_settings.wheel_wall_strength, &global_settings.wheel_wall_blur,
    &global_settings.wheel_np_layout, &global_settings.wheel_np_album,
    &global_settings.wheel_np_details, &global_settings.wheel_np_next,
    &global_settings.wheel_np_bar, &global_settings.wheel_pane,
    &global_settings.wheel_appearance, &global_settings.wheel_accent,
};

static int look_list(char names[][64], int max)
{
    int n = 0;
    DIR *d = opendir(LOOKS_DIR);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max)
    {
        const char *nm = (const char *)e->d_name;
        const char *x = strrchr(nm, '.');
        if (nm[0] == '.' || !x || strcasecmp(x, ".cfg"))
            continue;
        strlcpy(names[n], nm, 64);
        names[n][x - nm] = '\0';
        n++;
    }
    closedir(d);
    return n;
}

static bool look_save(const char *name)
{
    char path[MAX_PATH], line[MAX_PATH + 64], val[MAX_PATH];
    mkdir(WHEEL_DIR);
    mkdir(LOOKS_DIR);
    snprintf(path, sizeof(path), LOOKS_DIR "/%s.cfg", name);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    fdprintf(fd, "# Wheel look \"%s\" (Settings > Appearance > Looks)\n", name);
    /* the typography: a copy of the profile in use */
    if (typo_is_custom())
    {
        struct typo_profile p = *typo_active();
        snprintf(p.name, sizeof(p.name), "look %s", name);
        typo_slug(p.name, p.id, sizeof(p.id));
        if (typo_profile_save(&p))
            fdprintf(fd, "#typo: %s\n", p.id);
    }
    else
        fdprintf(fd, "#typo: -\n");
    for (unsigned i = 0; i < ARRAYLEN(look_settings); i++)
    {
        const struct settings_list *s = find_setting(look_settings[i]);
        if (!s || !s->cfg_name)
            continue;
        cfg_to_string(s, val, sizeof(val));
        snprintf(line, sizeof(line), "%s: %s\n", s->cfg_name, val);
        fdprintf(fd, "%s", line);
    }
    close(fd);
    return true;
}

static bool look_apply(const char *name)
{
    char path[MAX_PATH], line[128], typo[64] = "";
    snprintf(path, sizeof(path), LOOKS_DIR "/%s.cfg", name);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    while (read_line(fd, line, sizeof(line)) > 0)
        if (!strncmp(line, "#typo: ", 7))
            strlcpy(typo, line + 7, sizeof(typo));
    close(fd);
    splash(0, ID2P(LANG_WAIT));
    if (!settings_load_config(path, false))
        return false;
    struct typo_profile p;
    if (!strcmp(typo, "-"))
    {
        if (typo_is_custom())
            typo_restore_default();
    }
    else if (typo[0] && typo_profile_load(typo, &p) &&
             (!typo_is_custom() || strcmp(typo_active()->id, p.id)))
        typo_apply(&p);
    settings_save();
    wheel_appearance_apply(true);
    return true;
}

static void look_save_new(void)
{
    char name[48];
    char names[16][64];
    snprintf(name, sizeof(name), "Look %d", look_list(names, 16) + 1);
    if (kbd_input(name, sizeof(name), NULL) < 0 || !name[0])
        return;
    for (char *c = name; *c; c++)   /* (a file name) */
        if (strchr("/\\:*?\"<>|", *c))
            *c = '-';
    splash(HZ, look_save(name) ? ID2P(LANG_WHEEL_LOOK_SAVED) : ID2P(LANG_FAILED));
}

static char looks_names[24][64];
static int looks_n;

static void look_open(void)
{
    /* the look picked in the list: apply or delete */
    char title[64];
    strlcpy(title, looks_names[looks_n], sizeof(title));
    const char *items[2] = { str(LANG_WHEEL_LOOK_APPLY), str(LANG_WHEEL_LOOK_DELETE) };
    int what = wheel_pick(title, items, 2, 0);
    if (what == 0)
        look_apply(title);
    else if (what == 1)
    {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), LOOKS_DIR "/%s.cfg", title);
        remove(path);
    }
}

static const char *look_pick(const char *title, const char *current)
{
    static char choice[64];
    looks_n = look_list(looks_names, 24);
    const char *items[25];
    items[0] = str(LANG_WHEEL_LOOK_NONE);
    int sel = 0;
    for (int i = 0; i < looks_n; i++)
    {
        items[i + 1] = looks_names[i];
        if (!strcmp(current, looks_names[i]))
            sel = i + 1;
    }
    int pick = wheel_pick(title, items, looks_n + 1, sel);
    if (pick < 0)
        return NULL;
    strlcpy(choice, pick ? looks_names[pick - 1] : "", sizeof(choice));
    return choice;
}

static const char *day_text(char *buf, size_t len)
{
    strlcpy(buf, global_settings.wheel_look_day[0] ? global_settings.wheel_look_day
                                                   : (const char *)str(LANG_WHEEL_LOOK_NONE), len);
    return buf;
}

static const char *night_text(char *buf, size_t len)
{
    strlcpy(buf, global_settings.wheel_look_night[0] ? global_settings.wheel_look_night
                                                     : (const char *)str(LANG_WHEEL_LOOK_NONE), len);
    return buf;
}

static void day_pick(void)
{
    const char *c = look_pick(str(LANG_WHEEL_LOOK_DAY), global_settings.wheel_look_day);
    if (c)
    {
        strlcpy(global_settings.wheel_look_day, c, sizeof(global_settings.wheel_look_day));
        settings_save();
    }
}

static void night_pick(void)
{
    const char *c = look_pick(str(LANG_WHEEL_LOOK_NIGHT), global_settings.wheel_look_night);
    if (c)
    {
        strlcpy(global_settings.wheel_look_night, c, sizeof(global_settings.wheel_look_night));
        settings_save();
    }
}

static const char *hour_text_v(int h, char *buf, size_t len)
{
    snprintf(buf, len, "%02d:00", h);
    return buf;
}
static const char *dayh_text(char *buf, size_t len)
{ return hour_text_v(global_settings.wheel_day_hour, buf, len); }
static const char *nighth_text(char *buf, size_t len)
{ return hour_text_v(global_settings.wheel_night_hour, buf, len); }

/* the looks screen: saved looks first, then saving and the schedule */
static int lk_count(void) { return looks_n + 6; }
enum { LK_SAVE, LK_AUTO, LK_DAY, LK_NIGHT, LK_DAYH, LK_NIGHTH };

static const char *lk_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (item < looks_n)
    {
        strlcpy(buf, looks_names[item], len);
        return buf;
    }
    char v[64] = "";
    int label = 0;
    switch (item - looks_n)
    {
        case LK_SAVE:   strlcpy(buf, str(LANG_WHEEL_LOOK_SAVE), len); return buf;
        case LK_AUTO:   label = LANG_WHEEL_LOOK_AUTO;
                        strlcpy(v, str(global_settings.wheel_look_auto ? LANG_ON : LANG_OFF), sizeof(v));
                        break;
        case LK_DAY:    label = LANG_WHEEL_LOOK_DAY; day_text(v, sizeof(v)); break;
        case LK_NIGHT:  label = LANG_WHEEL_LOOK_NIGHT; night_text(v, sizeof(v)); break;
        case LK_DAYH:   label = LANG_WHEEL_DAY_FROM; dayh_text(v, sizeof(v)); break;
        case LK_NIGHTH: label = LANG_WHEEL_NIGHT_FROM; nighth_text(v, sizeof(v)); break;
    }
    snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(label), v);
    return buf;
}

static int lk_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    int item = gui_synclist_get_sel_pos(lists);
    if (item < looks_n)
    {
        int keep = looks_n;
        looks_n = item;      /* (look_open reads the name from there) */
        look_open();
        looks_n = keep;
    }
    else switch (item - looks_n)
    {
        case LK_SAVE:  look_save_new(); break;
        case LK_AUTO:  global_settings.wheel_look_auto = !global_settings.wheel_look_auto;
                       settings_save(); break;
        case LK_DAY:   day_pick(); break;
        case LK_NIGHT: night_pick(); break;
        case LK_DAYH:  global_settings.wheel_day_hour = (global_settings.wheel_day_hour + 1) % 24;
                       settings_save(); break;
        case LK_NIGHTH: global_settings.wheel_night_hour = (global_settings.wheel_night_hour + 1) % 24;
                        settings_save(); break;
    }
    looks_n = look_list(looks_names, 24);
    gui_synclist_set_nb_items(lists, lk_count());
    gui_synclist_set_title(lists, str(LANG_WHEEL_LOOKS), Icon_NOICON);
    return ACTION_REDRAW;
}

void wheel_looks_screen(void)
{
    looks_n = look_list(looks_names, 24);
    struct simplelist_info info;
    simplelist_info_init(&info, str(LANG_WHEEL_LOOKS), lk_count(), NULL);
    info.get_name = lk_name;
    info.action_callback = lk_action;
    simplelist_show_list(&info);
}

bool wheel_looks_check(void)
{
    /* on a change from day to night (or back) the look of the new period
     * is applied; at start-up the look in use stays */
    static int period = -1;
    if (!global_settings.wheel_look_auto)
    {
        period = -1;
        return false;
    }
    int now = wheel_is_night() ? 1 : 0;
    if (period < 0 || now == period)
    {
        period = now;
        return false;
    }
    /* (not in the middle of a song: reloading the skins pauses it a moment) */
    if ((audio_status() & (AUDIO_STATUS_PLAY|AUDIO_STATUS_PAUSE)) == AUDIO_STATUS_PLAY)
        return false;
    period = now;
    const char *want = now ? global_settings.wheel_look_night : global_settings.wheel_look_day;
    return want[0] && look_apply(want);
}

/* ------------------------------ wheel and click ------------------------------ */
static const int click_names[] = { LANG_WHEEL_CLICK_0, LANG_WHEEL_CLICK_1, LANG_WHEEL_CLICK_2,
                                   LANG_WHEEL_CLICK_3, LANG_WHEEL_CLICK_4 };
static const int sens_names[] = { LANG_WHEEL_SENS_0, LANG_WHEEL_SENS_1, LANG_WHEEL_SENS_2 };

static const char *click_volume_text(char *buf, size_t len)
{
    const struct settings_list *s = find_setting(&global_settings.keyclick);
    if (s)
        option_get_valuestring(s, buf, len, global_settings.keyclick);
    return buf;
}

static void click_volume(void)
{
    global_settings.keyclick = (global_settings.keyclick + 1) % 4;
    settings_save();
    system_sound_play(SOUND_KEYCLICK);
}

static void click_preview(void)
{
    if (!global_settings.keyclick)
        global_settings.keyclick = 2;   /* (choosing a sound: you want to hear it) */
    system_sound_play(SOUND_KEYCLICK);
}

void wheel_wheelclick_screen(void)
{
    static const struct opt opts[] = {
        { LANG_WHEEL_CLICK_VOLUME, NULL, NULL, 0, NULL, NULL, click_volume, click_volume_text },
        { LANG_WHEEL_CLICK_SOUND, &global_settings.wheel_click_sound, NULL, 5, click_names,
          click_preview, NULL, NULL },
        { LANG_WHEEL_SENSITIVITY, &global_settings.wheel_sensitivity, NULL, 3, sens_names,
          NULL, NULL, NULL },
        { LANG_WHEEL_ACCEL, NULL, &global_settings.wheel_accel, 0, NULL, NULL, NULL, NULL },
    };
    options_screen(LANG_WHEEL_WHEEL_CLICK, opts, ARRAYLEN(opts));
}

/* ------------------------------ the idle screen ------------------------------ */
static const int idle_secs[] = { 0, 15, 30, 60, 120 };

static const char *idle_text(char *buf, size_t len)
{
    int s = idle_secs[MAX(0, MIN(4, global_settings.wheel_idle))];
    if (!s)
        strlcpy(buf, str(LANG_OFF), len);
    else if (s < 60)
        snprintf(buf, len, "%d s", s);
    else
        snprintf(buf, len, "%d min", s / 60);
    return buf;
}

WHEEL_ART_INIT(idle_art, 64);

/* the screen's background: the cover, blurred and dimmed, or the palette */
static void idle_background(fb_data *buf)
{
    struct mp3entry *id3 = (audio_status() & AUDIO_STATUS_PLAY) ? audio_current_track() : NULL;
    bool art = id3 && wheel_art_load(&idle_art, id3->path);
    int aw = art ? idle_art.bm.width : 1, ah = art ? idle_art.bm.height : 1;
    const fb_data *px = art ? (const fb_data *)idle_art.bm.data : NULL;
    unsigned base = global_settings.lse_color;
    for (int y = 0; y < LCD_HEIGHT; y++)
        for (int x = 0; x < LCD_WIDTH; x++)
        {
            unsigned c;
            if (px)
            {   /* the middle of the cover, scaled up: soft by itself */
                int sx = aw / 6 + x * (aw * 2 / 3) / LCD_WIDTH;
                int sy = ah / 6 + y * (ah * 2 / 3) / LCD_HEIGHT;
                int fx = (x * (aw * 2 / 3) * 256 / LCD_WIDTH) & 255;
                int fy = (y * (ah * 2 / 3) * 256 / LCD_HEIGHT) & 255;
                int x1 = MIN(sx + 1, aw - 1), y1 = MIN(sy + 1, ah - 1);
                unsigned a = FB_UNPACK_SCALAR_LCD(px[sy * aw + sx]);
                unsigned b = FB_UNPACK_SCALAR_LCD(px[sy * aw + x1]);
                unsigned cc = FB_UNPACK_SCALAR_LCD(px[y1 * aw + sx]);
                unsigned dd = FB_UNPACK_SCALAR_LCD(px[y1 * aw + x1]);
                c = wheel_blend(wheel_blend(a, b, fx * 100 / 256), wheel_blend(cc, dd, fx * 100 / 256),
                                fy * 100 / 256);
                c = wheel_blend(c, LCD_RGBPACK(0, 0, 0), 55);
            }
            else
                c = wheel_blend(wheel_blend(base, LCD_RGBPACK(0, 0, 0), 70),
                                wheel_blend(base, LCD_RGBPACK(0, 0, 0), 88), y * 100 / LCD_HEIGHT);
            buf[y * LCD_WIDTH + x] = FB_SCALARPACK(c);
        }
}

static void idle_draw(const fb_data *bgbuf)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    d->set_drawmode(DRMODE_SOLID);
    d->bitmap(bgbuf, 0, 0, LCD_WIDTH, LCD_HEIGHT);
    d->set_drawmode(DRMODE_FG);
    unsigned white = LCD_RGBPACK(255, 255, 255);
    char t[32], date[64];
    struct tm *tm = get_time();
    int w, h;
    if (global_settings.timeformat)
        snprintf(t, sizeof(t), "%d:%02d", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, tm->tm_min);
    else
        snprintf(t, sizeof(t), "%02d:%02d", tm->tm_hour, tm->tm_min);
    vp.font = typo_font(TU_JUMBO);
    font_getstringsize((unsigned char *)t, &w, &h, vp.font);
    d->set_foreground(white);
    d->putsxy((LCD_WIDTH - w) / 2, 70, (unsigned char *)t);
    snprintf(date, sizeof(date), "%s %d %s", str(LANG_WEEKDAY_SUNDAY + tm->tm_wday),
             tm->tm_mday, str(LANG_MONTH_JANUARY + tm->tm_mon));
    vp.font = typo_font(TU_SECONDARY);
    font_getstringsize((unsigned char *)date, &w, NULL, vp.font);
    d->set_foreground(wheel_blend(white, LCD_RGBPACK(0, 0, 0), 20));
    d->putsxy((LCD_WIDTH - w) / 2, 70 + h + 8, (unsigned char *)date);
    struct mp3entry *id3 = (audio_status() & AUDIO_STATUS_PLAY) ? audio_current_track() : NULL;
    if (id3)
    {
        char line[160];
        snprintf(line, sizeof(line), "%s%s%s", id3->title ? id3->title : "",
                 id3->artist ? " · " : "", id3->artist ? id3->artist : "");
        vp.font = typo_font(TU_META);
        font_getstringsize((unsigned char *)line, &w, NULL, vp.font);
        d->set_foreground(wheel_blend(white, LCD_RGBPACK(0, 0, 0), 15));
        d->putsxy(MAX(16, (LCD_WIDTH - w) / 2), LCD_HEIGHT - 70, (unsigned char *)line);
        /* a thin progress line */
        int bw = LCD_WIDTH - 120, bx = 60, by = LCD_HEIGHT - 38;
        int done = id3->length ? (int)((long long)bw * id3->elapsed / id3->length) : 0;
        d->set_drawmode(DRMODE_SOLID);
        d->set_foreground(wheel_blend(white, LCD_RGBPACK(0, 0, 0), 70));
        d->fillrect(bx, by, bw, 4);
        d->set_foreground(white);
        d->fillrect(bx, by, MAX(0, MIN(bw, done)), 4);
    }
    d->update_viewport();
    d->set_viewport(last);
}

static void idle_run(void)
{
    fb_data *bgbuf = malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(fb_data));
    if (!bgbuf)
        return;
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    idle_background(bgbuf);
    while (1)
    {
        idle_draw(bgbuf);
        int a = get_action(CONTEXT_STD, HZ / 2);
        if (a == ACTION_NONE)
            continue;
        if (default_event_handler(a) == SYS_USB_CONNECTED)
            break;
        if (!IS_SYSEVENT(a))
            break;
    }
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, true);
    free(bgbuf);
}

bool wheel_idle_maybe(void)
{
    int s = idle_secs[MAX(0, MIN(4, global_settings.wheel_idle))];
    static bool running;
    if (!s || running || !wheel_skin_is_wheel())
        return false;
    if (TIME_BEFORE(current_tick, action_last_input_tick() + s * HZ))
        return false;
    running = true;
    idle_run();
    running = false;
    return true;
}

/* ------------------------------ startup image ------------------------------ */
static void boot_compose(const char *picture, const char *text)
{
    /* drawn on the screen (that's the preview too) and saved from there */
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
    d->set_drawmode(DRMODE_SOLID);
    d->set_background(LCD_RGBPACK(0, 0, 0));
    d->clear_viewport();
    bool pic = false;
    if (picture[0])
    {
        struct bitmap bm;
        memset(&bm, 0, sizeof(bm));
        bm.width = LCD_WIDTH;
        bm.height = LCD_HEIGHT;
        const char *ext = strrchr(picture, '.');
        bool jpeg = ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg"));
        /* (the JPEG decoder keeps its state in the buffer even to measure) */
        void *probe = malloc(96 * 1024);
        int need = -1;
        if (probe)
        {
            bm.data = probe;
            int f = FORMAT_NATIVE | FORMAT_RESIZE | FORMAT_KEEP_ASPECT | FORMAT_RETURN_SIZE;
            need = jpeg ? read_jpeg_file(picture, &bm, 96 * 1024, f, NULL)
                        : read_bmp_file(picture, &bm, 96 * 1024, f, NULL);
            free(probe);
        }
        void *mem = need > 0 ? malloc(need + 64 * 1024) : NULL;
        if (mem)
        {
            bm.data = mem;
            bm.width = LCD_WIDTH;
            bm.height = LCD_HEIGHT;
            int fmt = FORMAT_NATIVE | FORMAT_RESIZE | FORMAT_KEEP_ASPECT | FORMAT_DITHER;
            int got = jpeg ? read_jpeg_file(picture, &bm, need + 64 * 1024, fmt, NULL)
                           : read_bmp_file(picture, &bm, need + 64 * 1024, fmt, NULL);
            if (got > 0)
            {
                d->bitmap((fb_data *)bm.data, (LCD_WIDTH - bm.width) / 2,
                          (LCD_HEIGHT - bm.height) / 2, bm.width, bm.height);
                pic = true;
            }
            free(mem);
        }
    }
    if (!pic)
    {   /* no picture: a soft gel backdrop in the accent colour */
        for (int y = 0; y < LCD_HEIGHT; y++)
        {
            d->set_foreground(wheel_blend(wheel_blend(global_settings.lss_color,
                              LCD_RGBPACK(0, 0, 0), 30),
                              wheel_blend(global_settings.lse_color, LCD_RGBPACK(0, 0, 0), 75),
                              y * 100 / LCD_HEIGHT));
            d->hline(0, LCD_WIDTH - 1, y);
        }
    }
    if (text[0])
    {
        vp.font = typo_font(TU_DISPLAY_NP);
        int w, h;
        font_getstringsize((unsigned char *)text, &w, &h, vp.font);
        int y = pic ? LCD_HEIGHT - h - 24 : (LCD_HEIGHT - h) / 2;
        if (pic)
        {   /* a dark glass band under the text */
            for (int k = y - 10; k < y + h + 10; k++)
            {
                for (int x = 0; x < LCD_WIDTH; x++)
                {
                    unsigned c = FB_UNPACK_SCALAR_LCD(*FBADDR(x, k));
                    d->set_foreground(wheel_blend(c, LCD_RGBPACK(0, 0, 0), 45));
                    d->drawpixel(x, k);
                }
            }
        }
        d->set_drawmode(DRMODE_FG);
        d->set_foreground(LCD_RGBPACK(255, 255, 255));
        d->putsxy((LCD_WIDTH - w) / 2, y, (unsigned char *)text);
    }
    d->update_viewport();
    d->set_viewport(last);
}

static bool boot_save_screen(void)
{
    mkdir(WHEEL_DIR);
    int fd = open(BOOT_BMP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    const int stride = (LCD_WIDTH * 3 + 3) & ~3;
    unsigned char hdr[54] = { 'B', 'M' };
    unsigned v[6] = { 54 + stride * LCD_HEIGHT, 54, 40, LCD_WIDTH, LCD_HEIGHT, 0 };
    int off[5] = { 2, 10, 14, 18, 22 };
    for (int i = 0; i < 5; i++)
        for (int b = 0; b < 4; b++)
            hdr[off[i] + b] = (v[i] >> (8 * b)) & 0xff;
    hdr[26] = 1;
    hdr[28] = 24;
    bool ok = write(fd, hdr, sizeof(hdr)) == sizeof(hdr);
    static unsigned char row[(LCD_WIDTH * 3 + 3) & ~3];
    for (int y = LCD_HEIGHT - 1; y >= 0 && ok; y--)
    {
        for (int x = 0; x < LCD_WIDTH; x++)
        {
            unsigned c = FB_UNPACK_SCALAR_LCD(*FBADDR(x, y));
            row[x * 3] = RGB_UNPACK_BLUE(c);
            row[x * 3 + 1] = RGB_UNPACK_GREEN(c);
            row[x * 3 + 2] = RGB_UNPACK_RED(c);
        }
        ok = write(fd, row, stride) == stride;
    }
    close(fd);
    return ok;
}

static bool read_small(const char *path, char *buf, size_t len)
{
    buf[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    int n = read_line(fd, buf, len);
    close(fd);
    return n > 0;
}

static void write_small(const char *path, const char *text)
{
    mkdir(WHEEL_DIR);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0)
    {
        fdprintf(fd, "%s", text);
        close(fd);
    }
}

/* shows the composition for a moment, then keeps it for the next start */
static void boot_update(void)
{
    char pic[MAX_PATH], text[64];
    read_small(BOOT_PIC, pic, sizeof(pic));
    read_small(BOOT_TXT, text, sizeof(text));
    if (!pic[0] && !text[0])
    {
        remove(BOOT_BMP);
        return;
    }
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    boot_compose(pic, text);
    boot_save_screen();
    sleep(HZ * 3 / 2);
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, true);
}

static const char *boot_pic_text(char *buf, size_t len)
{
    char pic[MAX_PATH];
    read_small(BOOT_PIC, pic, sizeof(pic));
    const char *slash = strrchr(pic, '/');
    strlcpy(buf, pic[0] ? (slash ? slash + 1 : pic) : (const char *)str(LANG_WHEEL_BOOT_DEFAULT), len);
    return buf;
}

static const char *boot_txt_text(char *buf, size_t len)
{
    read_small(BOOT_TXT, buf, len);
    return buf;
}

static void boot_pick(void)
{
    char path[MAX_PATH];
    const char *items[2] = { str(LANG_WHEEL_BOOT_DEFAULT), str(LANG_WHEEL_WALL_PICK) };
    int what = wheel_pick(str(LANG_WHEEL_BOOT_IMAGE), items, 2, 0);
    if (what == 0)
        write_small(BOOT_PIC, "");
    else if (what == 1 && pick_picture(LANG_WHEEL_BOOT_IMAGE, path, sizeof(path)) >= 0)
        write_small(BOOT_PIC, path);
    else
        return;
    boot_update();
}

static void boot_text_edit(void)
{
    char text[64];
    read_small(BOOT_TXT, text, sizeof(text));
    if (kbd_input(text, sizeof(text), NULL) < 0)
        return;
    write_small(BOOT_TXT, text);
    boot_update();
}

static void boot_preview(void)
{
    char pic[MAX_PATH], text[64];
    read_small(BOOT_PIC, pic, sizeof(pic));
    read_small(BOOT_TXT, text, sizeof(text));
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);
    if (pic[0] || text[0])
        boot_compose(pic, text);
    else
    {
        struct screen *d = &screens[SCREEN_MAIN];
        d->set_background(LCD_RGBPACK(0, 0, 0));
        d->clear_display();
        d->update();
    }
    get_action(CONTEXT_STD, HZ * 3);
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, true);
}

void wheel_idleboot_screen(void)
{
    static const struct opt opts[] = {
        { LANG_WHEEL_IDLE, &global_settings.wheel_idle, NULL, 5, NULL, NULL, NULL, idle_text },
        { LANG_WHEEL_BOOT_IMAGE, NULL, NULL, 0, NULL, NULL, boot_pick, boot_pic_text },
        { LANG_WHEEL_BOOT_TEXT, NULL, NULL, 0, NULL, NULL, boot_text_edit, boot_txt_text },
        { LANG_WHEEL_PREVIEW, NULL, NULL, 0, NULL, NULL, boot_preview, NULL },
    };
    options_screen(LANG_WHEEL_IDLE_BOOT, opts, ARRAYLEN(opts));
}

bool wheel_boot_logo(void)
{
    int fd = open(BOOT_BMP, O_RDONLY);
    if (fd < 0)
        return false;
    close(fd);
    void *mem = malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(fb_data) + 4096);
    if (!mem)
        return false;
    struct bitmap bm;
    memset(&bm, 0, sizeof(bm));
    bm.data = mem;
    int got = read_bmp_file(BOOT_BMP, &bm, LCD_WIDTH * LCD_HEIGHT * sizeof(fb_data) + 4096,
                            FORMAT_NATIVE, NULL);
    bool ok = got > 0 && bm.width <= LCD_WIDTH && bm.height <= LCD_HEIGHT;
    if (ok)
    {
        lcd_set_background(LCD_RGBPACK(0, 0, 0));
        lcd_clear_display();
        lcd_bitmap((fb_data *)bm.data, (LCD_WIDTH - bm.width) / 2, (LCD_HEIGHT - bm.height) / 2,
                   bm.width, bm.height);
        lcd_update();
    }
    free(mem);
    return ok;
}

/* ------------------------------ Appearance values ------------------------------ */
const char *wheel_custom_value(int which, char *buf, size_t len)
{
    buf[0] = '\0';
    switch (which)
    {
        case WCV_FINISH:     return finish_text(buf, len);
        case WCV_BACKGROUND: return wall_text(buf, len);
        case WCV_NP:
            strlcpy(buf, str(layout_names[MAX(0, MIN(3, global_settings.wheel_np_layout))]), len);
            break;
        case WCV_MENU:
            strlcpy(buf, str(pane_names[MAX(0, MIN(3, global_settings.wheel_pane))]), len);
            break;
        case WCV_LOOKS:
            break;
        case WCV_WHEEL:
            strlcpy(buf, str(click_names[MAX(0, MIN(4, global_settings.wheel_click_sound))]), len);
            break;
        case WCV_IDLE:
            return idle_text(buf, len);
    }
    return buf;
}
