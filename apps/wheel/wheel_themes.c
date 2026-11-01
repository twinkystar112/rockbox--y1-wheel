/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - themes.
 *
 *  - a theme picker that tries the theme live and goes back to the old one
 *    unless you keep it (10 s)
 *  - installing a normal Rockbox theme .zip from the player itself
 *  - themes drawn for 320x240 are scaled to the screen (the skin parser
 *    scales coordinates and images; here: fonts and the theme's UI font)
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
#include "splash.h"
#include "yesno.h"
#include "settings.h"
#include "file.h"
#include "dir.h"
#include "misc.h"
#include "rbpaths.h"
#include "pathfuncs.h"
#include "string-extra.h"
#include "zip.h"
#include "wheel_screens.h"
#include "wheel_listview.h"
#include "wheel_themes.h"
#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
#include "filesystem-app.h"
#endif

#define THEME_STATE_FILE ROCKBOX_DIR "/wheel-theme.txt"

/* ----------------------------- scaling ----------------------------- */

static bool can_open(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    close(fd);
    return true;
}

/* "12-Nimbus.fnt" on a 1.5x theme: "18-Nimbus.fnt" if there is one,
 * otherwise the Inter size closest to 18 (shipped with this edition) */
const char *wheel_scaled_font_name(const char *name, char *buf, size_t len)
{
    char path[MAX_PATH];
    int size = atoi(name);
    if (size <= 0)
        return name;
    const char *rest = strchr(name, '-');
    int want = skin_scale_value(size);
    if (rest)
    {
        snprintf(buf, len, "%d%s", want, rest);
        snprintf(path, sizeof(path), FONT_DIR "/%s", buf);
        if (can_open(path))
            return buf;
    }
    static const struct { int max; const char *font; } inter[] = {
        { 19, "18-Inter-Regular.fnt" }, { 21, "20-Inter-Medium.fnt" },
        { 23, "22-Inter-Medium.fnt" },  { 26, "24-Inter-SemiBold.fnt" },
        { 999, "28-Inter-Bold.fnt" },
    };
    for (unsigned i = 0; i < ARRAYLEN(inter); i++)
        if (want <= inter[i].max)
        {
            strlcpy(buf, inter[i].font, len);
            return buf;
        }
    return name;
}

/* every viewport fits in 320x240 and the skin clearly fills that screen */
bool wheel_skin_is_small(const char *text, int *max_r_out, int *max_b_out)
{
    int max_r = 0, max_b = 0, n_vp = 0;
    for (const char *p = text; (p = strstr(p, "%V")); p += 2)
    {
        const char *a = p + 2;
        bool label = false;
        if (*a == 'l' || *a == 'i') { label = true; a++; }
        if (*a != '(')
            continue;
        a++;
        if (label)
        {
            while (*a && *a != ',' && *a != ')' && *a != '\n') a++;
            if (*a != ',') continue;
            a++;
        }
        int v[4], i;
        bool num[4];
        for (i = 0; i < 4; i++)
        {
            while (*a == ' ') a++;
            num[i] = (*a >= '0' && *a <= '9');
            v[i] = num[i] ? atoi(a) : 0;
            while (*a && *a != ',' && *a != ')' && *a != '\n' && *a != '%') a++;
            if (*a == '%') { num[i] = false; a++; }
            if (*a != ',' && i < 3) break;
            if (*a == ',') a++;
        }
        if (i < 3)
            continue;
        n_vp++;
        if (num[0] && num[2]) max_r = MAX(max_r, v[0] + v[2]);
        else if (num[0]) max_r = MAX(max_r, v[0]);
        if (num[1] && num[3]) max_b = MAX(max_b, v[1] + v[3]);
        else if (num[1]) max_b = MAX(max_b, v[1]);
    }
    if (max_r_out) *max_r_out = max_r;
    if (max_b_out) *max_b_out = max_b;
    return n_vp > 0 && max_r <= 320 && max_b <= 240 && (max_r >= 300 || max_b >= 220);
}

static bool file_is_small_skin(const char *path)
{
    static char peek[8192];
    int fd = open_utf8(path, O_RDONLY);
    if (fd < 0)
        return false;
    ssize_t n = read(fd, peek, sizeof(peek) - 1);
    close(fd);
    peek[n > 0 ? n : 0] = '\0';
    return wheel_skin_is_small(peek, NULL, NULL);
}

static bool screen_can_scale(void)
{
    return LCD_WIDTH > 320 && LCD_WIDTH * 240 == LCD_HEIGHT * 320;
}

/* after loading a theme made for 320x240: a readable list font, and the
 * list viewport (if the .cfg sets one) scaled like the skin */
static void fix_small_theme(void)
{
    char path[MAX_PATH];
    if (!global_settings.wheel_theme_scale || !screen_can_scale())
        return;
    const char *wps = global_settings.wps_file;
    const char *sbs = global_settings.sbs_file;
    bool small = false;
    if (sbs[0] && sbs[0] != '-')
    {
        snprintf(path, sizeof(path), WPS_DIR "/%s.sbs", sbs);
        small = file_is_small_skin(path);
    }
    if (!small && wps[0] && wps[0] != '-')
    {
        snprintf(path, sizeof(path), WPS_DIR "/%s.wps", wps);
        small = file_is_small_skin(path);
    }
    if (!small)
        return;
    int old_num = skin_scale_num, old_den = skin_scale_den;
    skin_scale_num = LCD_WIDTH;
    skin_scale_den = 320;
    if (global_settings.font_file[0] && global_settings.font_file[0] != '-')
    {
        char name[MAX_PATH], buf[MAX_PATH];
        snprintf(name, sizeof(name), "%s.fnt", global_settings.font_file);
        const char *f = wheel_scaled_font_name(name, buf, sizeof(buf));
        if (f != name)
        {
            strlcpy(global_settings.font_file, f, sizeof(global_settings.font_file));
            char *dot = strrchr(global_settings.font_file, '.');
            if (dot) *dot = '\0';
        }
    }
    if (global_settings.ui_vp_config[0] && global_settings.ui_vp_config[0] != '-')
    {   /* "x,y,w,h,font,fg,bg": scale the first four numbers */
        char out[64] = "";
        char tmp[64];
        strlcpy(tmp, global_settings.ui_vp_config, sizeof(tmp));
        char *save, *tok = strtok_r(tmp, ",", &save);
        for (int i = 0; tok; i++, tok = strtok_r(NULL, ",", &save))
        {
            char part[16];
            if (i < 4 && tok[0] >= '0' && tok[0] <= '9')
                snprintf(part, sizeof(part), "%d", skin_scale_value(atoi(tok)));
            else
                strlcpy(part, tok, sizeof(part));
            if (i) strlcat(out, ",", sizeof(out));
            strlcat(out, part, sizeof(out));
        }
        strlcpy(global_settings.ui_vp_config, out, sizeof(global_settings.ui_vp_config));
    }
    skin_scale_num = old_num;
    skin_scale_den = old_den;
}

/* --------------------- listing a .rockbox sub-folder --------------------- */

/* hosted builds keep the built-in files in a read-only share dir and the
 * user's in another (/sdcard/.rockbox): list both, without duplicates */
static int add_names(const char *dir, const char *ext, char names[][64], int n, int max)
{
    DIR *d = opendir(dir);
    if (!d)
        return n;
    struct dirent *e;
    while ((e = readdir(d)) && n < max)
    {
        const char *nm = (const char *)e->d_name;
        const char *x = strrchr(nm, '.');
        if (nm[0] == '.' || !x || strcasecmp(x, ext))
            continue;
        char base[64];
        strlcpy(base, nm, sizeof(base));
        *strrchr(base, '.') = '\0';
        int k;
        for (k = 0; k < n && strcasecmp(names[k], base); k++);
        if (k < n)
            continue;
        strcpy(names[n++], base);
    }
    closedir(d);
    return n;
}

int wheel_dir_list(const char *rbdir, const char *ext, char names[][64], int max)
{
    int n = 0;
#if (CONFIG_PLATFORM & PLATFORM_HOSTED) && defined(HAVE_SPECIAL_DIRS)
    char buf[MAX_PATH];
    const char *user = handle_special_dirs(rbdir, NEED_WRITE, buf, sizeof(buf));
    char user_copy[MAX_PATH];
    strlcpy(user_copy, user, sizeof(user_copy));
    n = add_names(user_copy, ext, names, n, max);
    char share[MAX_PATH];
    snprintf(share, sizeof(share), ROCKBOX_SHARE_PATH "%s", rbdir + ROCKBOX_DIR_LEN);
    n = add_names(share, ext, names, n, max);
#else
    n = add_names(rbdir, ext, names, n, max);
#endif
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--)
        {
            char t[64];
            strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t);
        }
    return n;
}

/* ------------------------------- picker ------------------------------- */

#define MAX_THEMES 48
static struct {
    char names[MAX_THEMES][64];
    int n;
    char current[64];
} th;

static void read_current(void)
{
    th.current[0] = '\0';
    int fd = open(THEME_STATE_FILE, O_RDONLY);
    if (fd >= 0)
    {
        read_line(fd, th.current, sizeof(th.current));
        close(fd);
    }
    if (!th.current[0] && strstr(global_settings.sbs_file, "Wheel"))
        strcpy(th.current, "Wheel");
}

static void store_current(const char *name)
{
    strlcpy(th.current, name, sizeof(th.current));
    int fd = open(THEME_STATE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0)
    {
        fdprintf(fd, "%s\n", name);
        close(fd);
    }
}

static void scan_themes(void)
{
    static char all[MAX_THEMES][64];
    int n = wheel_dir_list(THEME_DIR, ".cfg", all, MAX_THEMES);
    th.n = 0;
    for (int i = 0; i < n; i++)
    {   /* the dark Wheel is chosen with Appearance, not here */
        if (!strcmp(all[i], "Wheel-Dark") || !strcmp(all[i], "MacClassic-Dark"))
            continue;
        strcpy(th.names[th.n++], all[i]);
    }
    read_current();
}

static bool apply_theme(const char *name)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), THEME_DIR "/%s.cfg", name);
    if (!settings_load_config(path, false))
        return false;
    fix_small_theme();
    settings_apply(true);
    if (!strcmp(name, "Wheel"))
        wheel_appearance_apply(true); /* light/dark + accent on top */
    else
        wheel_reload_skins();
    return true;
}

static const char *th_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    if (item == th.n)
        return str(LANG_WHEEL_THEME_INSTALL);
    if (item == th.n + 1)
    {
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_THEME_SCALE),
                 str(global_settings.wheel_theme_scale ? LANG_ON : LANG_OFF));
        return buf;
    }
    if (!strcmp(th.names[item], th.current))
        snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "✓", th.names[item]);
    else
        strlcpy(buf, th.names[item], len);
    return buf;
}

static bool th_get_nav(int item, void *data)
{
    (void)data;
    return item == th.n;
}

static int install_from_zip(void);

static int th_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_REDRAW)
    {
        gui_synclist_set_nav_callback(lists, th_get_nav);
        return action;
    }
    if (action == ACTION_STD_MENU)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;
    int item = gui_synclist_get_sel_pos(lists);
    if (item == th.n)
    {
        install_from_zip();
        scan_themes();
        gui_synclist_set_nb_items(lists, th.n + 2);
    }
    else if (item == th.n + 1)
    {
        global_settings.wheel_theme_scale = !global_settings.wheel_theme_scale;
        settings_save();
        wheel_reload_skins();
    }
    else if (item < th.n && strcmp(th.names[item], th.current))
    {
        static struct user_settings before;
        memcpy(&before, &global_settings, sizeof(before));
        splash(0, ID2P(LANG_WAIT));
        if (!apply_theme(th.names[item]))
            splash(HZ * 2, ID2P(LANG_WHEEL_THEME_BROKEN));
        else
        {
            static const char *lines[] = { (const char *)ID2P(LANG_WHEEL_THEME_KEEP) };
            static const struct text_message msg = { lines, 1 };
            /* nothing pressed for 10 s: probably unreadable, go back */
            if (gui_syncyesno_run_w_tmo(HZ * 10, YESNO_NO, NULL, &msg, NULL, NULL) == YESNO_YES)
            {
                store_current(th.names[item]);
                settings_save();
            }
            else
            {
                memcpy(&global_settings, &before, sizeof(before));
                settings_apply(true);
                wheel_reload_skins();
            }
        }
    }
    gui_synclist_set_title(lists, str(LANG_WHEEL_THEME), Icon_NOICON);
    return ACTION_REDRAW;
}

int wheel_theme_screen(void)
{
    scan_themes();
    struct simplelist_info info;
    simplelist_info_init(&info, str(LANG_WHEEL_THEME), th.n + 2, NULL);
    info.get_name = th_get_name;
    info.action_callback = th_action;
    for (int i = 0; i < th.n; i++)
        if (!strcmp(th.names[i], th.current))
            info.selection = i;
    simplelist_show_list(&info);
    return 0;
}

const char *wheel_theme_current(void)
{
    read_current();
    return th.current[0] ? th.current : "—";
}

/* ---------------------------- zip install ---------------------------- */

static const char * const theme_dirs[] = {
    "themes/", "wps/", "fonts/", "backdrops/", "icons/",
};

/* "path/inside/zip" -> "wps/X.wps" (relative to .rockbox) or NULL */
static const char *map_entry(const char *name)
{
    while (name[0] == '.' && name[1] == '/') name += 2;
    while (name[0] == '/') name++;
    if (!strncmp(name, ".rockbox/", 9)) name += 9;
    else if (!strncmp(name, "rockbox/", 8)) name += 8;
    if (strstr(name, ".."))
        return NULL;
    for (unsigned i = 0; i < ARRAYLEN(theme_dirs); i++)
        if (!strncmp(name, theme_dirs[i], strlen(theme_dirs[i])))
            return name;
    return NULL;
}

static struct {
    int fd;
    int files, themes;
    char first_theme[64];
    char path[MAX_PATH];
} zx;

static void mkdirs_for(const char *rel)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), ROCKBOX_DIR "/%s", rel);
    for (char *p = path + strlen(ROCKBOX_DIR) + 1; *p; p++)
        if (*p == '/')
        {   /* always try: on hosted builds dir_exists() also sees the
             * read-only built-in folder, but files go to the user's one */
            *p = '\0';
            mkdir(path);
            *p = '/';
        }
}

static int zip_scan_cb(const struct zip_args *args, int pass, void *ctx)
{
    (void)ctx;
    if (pass != ZIP_PASS_SHALLOW)
        return 0;
    const char *rel = map_entry(args->name);
    if (rel && !strncmp(rel, "themes/", 7))
    {
        const char *x = strrchr(rel, '.');
        if (x && !strcasecmp(x, ".cfg"))
        {
            if (!zx.themes)
            {
                strlcpy(zx.first_theme, rel + 7, sizeof(zx.first_theme));
                char *dot = strrchr(zx.first_theme, '.');
                if (dot) *dot = '\0';
            }
            zx.themes++;
        }
    }
    return 0;
}

static int zip_write_cb(const struct zip_args *args, int pass, void *ctx)
{
    (void)ctx;
    switch (pass)
    {
        case ZIP_PASS_START:
        {
            zx.fd = -1;
            const char *rel = map_entry(args->name);
            if (!rel || rel[strlen(rel) - 1] == '/')
                return 0; /* skipped, or just a folder */
            mkdirs_for(rel);
            snprintf(zx.path, sizeof(zx.path), ROCKBOX_DIR "/%s", rel);
            zx.fd = open(zx.path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (zx.fd < 0)
                return 1;
            zx.files++;
            if ((args->entry & 7) == 0)
                splashf(0, "%s %d/%d", str(LANG_WHEEL_THEME_INSTALLING),
                        args->entry + 1, args->entries);
            return 0;
        }
        case ZIP_PASS_DATA:
            if (zx.fd >= 0 &&
                write(zx.fd, args->block, args->block_size) != (ssize_t)args->block_size)
                return 2;
            return 0;
        case ZIP_PASS_END:
            if (zx.fd >= 0)
                close(zx.fd);
            zx.fd = -1;
            return 0;
    }
    return 0;
}

bool wheel_install_theme_zip(const char *zip_path, char *theme_out, size_t len)
{
    memset(&zx, 0, sizeof(zx));
    zx.fd = -1;
    struct zip *z = zip_open(zip_path, false);
    if (!z)
        return false;
    bool ok = zip_read_shallow(z, zip_scan_cb, NULL) == 0 && zx.themes > 0;
    if (ok)
        ok = zip_read_deep(z, zip_write_cb, NULL) == 0;
    if (zx.fd >= 0)
        close(zx.fd);
    zip_close(z);
    if (ok && theme_out)
        strlcpy(theme_out, zx.first_theme, len);
    return ok && zx.files > 0;
}

/* .zip files where people usually drop them */
static int install_from_zip(void)
{
    static const char * const places[] = {
        HOME_DIR, HOME_DIR "/Download", HOME_DIR "/Downloads", HOME_DIR "/Themes",
        HOME_DIR "/themes",
    };
    static char paths[24][MAX_PATH];
    static char names[24][64];
    const char *items[24];
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
            if (nm[0] == '.' || !x || strcasecmp(x, ".zip"))
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
        splash(HZ * 4, ID2P(LANG_WHEEL_THEME_NO_ZIPS));
        return 0;
    }
    int pick = wheel_pick(str(LANG_WHEEL_THEME_INSTALL), items, n, 0);
    if (pick < 0)
        return 0;
    char theme[64];
    splash(0, ID2P(LANG_WHEEL_THEME_INSTALLING));
    if (!wheel_install_theme_zip(paths[pick], theme, sizeof(theme)))
    {
        splash(HZ * 3, ID2P(LANG_WHEEL_THEME_NOT_A_THEME));
        return 0;
    }
    splashf(HZ * 2, str(LANG_WHEEL_THEME_INSTALLED), theme);
    return 1;
}
