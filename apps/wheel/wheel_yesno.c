/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - confirmations as a sheet that slides over the screen.
 *
 * Replaces gui/yesno.c (and the Android dialog of hosted/android/yesno.c):
 * the question at the top of a card, two big rows below it, the wheel
 * picks, the centre confirms, MENU/back means "No". Same API.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lang.h"
#include "action.h"
#include "screen_access.h"
#include "viewport.h"
#include "splash.h"
#include "font.h"
#include "misc.h"
#include "talk.h"
#include "settings.h"
#include "string-extra.h"
#include "yesno.h"
#include "button.h"
#include "statusbar-skinned.h"
#include "wheel_gel.h"

#define PAD 18
#define ROW 44
#define MAX_WRAP 6

/* word-wrap the message into at most MAX_WRAP lines */
static int wrap(struct screen *d, const struct text_message *m, int width,
                char out[MAX_WRAP][96])
{
    int n = 0;
    for (int i = 0; m && i < m->nb_lines && n < MAX_WRAP; i++)
    {
        const char *s = P2STR((unsigned char *)m->message_lines[i]);
        char line[96] = "";
        while (*s && n < MAX_WRAP)
        {
            const char *sp = strchr(s, ' ');
            size_t wl = sp ? (size_t)(sp - s) : strlen(s);
            char cand[96];
            snprintf(cand, sizeof(cand), "%s%s%.*s", line, line[0] ? " " : "", (int)wl, s);
            int w, h;
            d->getstringsize(cand, &w, &h);
            if (w > width && line[0])
            {
                strlcpy(out[n++], line, 96);
                snprintf(line, sizeof(line), "%.*s", (int)wl, s);
            }
            else
                strlcpy(line, cand, sizeof(line));
            s += wl;
            while (*s == ' ') s++;
        }
        if (line[0] && n < MAX_WRAP)
            strlcpy(out[n++], line, 96);
    }
    return n;
}

static void draw_sheet(const struct text_message *m, int sel)
{
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = 0; vp.width = LCD_WIDTH;
    struct viewport *last = d->set_viewport(&vp);
    int fh = font_get(vp.font)->height;

    char lines[MAX_WRAP][96];
    int n = wrap(d, m, LCD_WIDTH - 2 * PAD, lines);
    int h = PAD + n * (fh + 4) + 10 + 2 * ROW + PAD / 2;
    if (h > LCD_HEIGHT)
        h = LCD_HEIGHT;
    vp.y = LCD_HEIGHT - h;
    vp.height = h;
    d->set_viewport(&vp);

#if LCD_DEPTH > 1
    unsigned fg = d->get_foreground(), bg = d->get_background();
    d->set_background(global_settings.bg_color);
    d->set_foreground(global_settings.fg_color);
#endif
    d->set_drawmode(DRMODE_SOLID);
    d->clear_viewport();
    /* top edge: a hairline and a short grab handle, like a sheet */
#if LCD_DEPTH > 1
    d->set_foreground(LCD_RGBPACK(0xb8, 0xb8, 0xbf));
#endif
    d->hline(0, LCD_WIDTH - 1, 0);
    d->fillrect(LCD_WIDTH / 2 - 18, 6, 36, 4);
#if LCD_DEPTH > 1
    d->set_foreground(global_settings.fg_color);
#endif
    int y = PAD;
    for (int i = 0; i < n; i++)
    {
        int w, hh;
        d->getstringsize(lines[i], &w, &hh);
        d->putsxy((LCD_WIDTH - w) / 2, y, lines[i]);
        y += fh + 4;
    }
    y += 10;
    const char *opt[2] = { str(LANG_SET_BOOL_YES), str(LANG_SET_BOOL_NO) };
    for (int i = 0; i < 2; i++)
    {
        int rx = PAD, rw = LCD_WIDTH - 2 * PAD;
        int w, hh;
        d->getstringsize(opt[i], &w, &hh);
        if (i == sel)
        {
#ifdef HAVE_LCD_COLOR
            wheel_selection(d, rx, y, rw, ROW - 6);
            d->set_foreground(global_settings.lst_color);
#else
            d->fillrect(rx, y, rw, ROW - 6);
            d->set_drawmode(DRMODE_SOLID|DRMODE_INVERSEVID);
#endif
        }
#if LCD_DEPTH > 1
        d->set_drawmode(DRMODE_FG);
#endif
        d->putsxy(rx + (rw - w) / 2, y + (ROW - 6 - hh) / 2, opt[i]);
        d->set_drawmode(DRMODE_SOLID);
#if LCD_DEPTH > 1
        d->set_foreground(global_settings.fg_color);
#endif
        y += ROW;
    }
#if LCD_DEPTH > 1
    d->set_foreground(fg);
    d->set_background(bg);
#endif
    d->update_viewport();
    d->set_viewport(last);
}

enum yesno_res gui_syncyesno_run_w_tmo(int ticks, enum yesno_res tmo_default_res,
                                       const char * title,
                                       const struct text_message * main_message,
                                       const struct text_message * yes_message,
                                       const struct text_message * no_message)
{
    int sel = 0; /* "Yes" first: most questions are "do it?" */
    enum yesno_res result = YESNO_NO;
    long end = current_tick + ticks;
    bool done = false;

    if (ticks < HZ) /* same rule as Rockbox: short timeouts mean "wait" */
        ticks = 0;
    if (title)
        FOR_NB_SCREENS(i)
            sb_set_persistent_title(title, Icon_NOICON, i);

    if (main_message && main_message->nb_lines > 0 && global_settings.talk_menu)
        talk_id(P2ID((unsigned char *)main_message->message_lines[0]), false);

    while (!done)
    {
        draw_sheet(main_message, sel);
        int tmo = ticks > 0 ? MAX(1, end - current_tick) : TIMEOUT_BLOCK;
        int action = get_action(CONTEXT_STD, tmo);
        switch (action)
        {
            case ACTION_STD_PREV:
            case ACTION_STD_PREVREPEAT:
                sel = 0;
                break;
            case ACTION_STD_NEXT:
            case ACTION_STD_NEXTREPEAT:
                sel = 1;
                break;
            case ACTION_STD_OK:
                result = sel == 0 ? YESNO_YES : YESNO_NO;
                done = true;
                break;
            case ACTION_STD_CANCEL:
            case ACTION_STD_MENU:
                result = YESNO_NO;
                done = true;
                break;
            case ACTION_NONE:
                if (ticks > 0 && TIME_AFTER(current_tick, end))
                {
                    result = tmo_default_res == YESNO_TMO ? YESNO_TMO : tmo_default_res;
                    done = true;
                }
                break;
            default:
                if (default_event_handler(action) == SYS_USB_CONNECTED)
                {
                    result = YESNO_USB;
                    done = true;
                }
                break;
        }
    }

    const struct text_message *after = result == YESNO_YES ? yes_message : no_message;
    if (after && after->nb_lines > 0)
        splash(HZ, P2STR((unsigned char *)after->message_lines[0]));

    /* make whatever was underneath redraw itself */
    FOR_NB_SCREENS(i)
        screens[i].clear_display();
    button_queue_post(BUTTON_REDRAW, 0);
    return result;
}

enum yesno_res gui_syncyesno_run(const struct text_message * main_message,
                                 const struct text_message * yes_message,
                                 const struct text_message * no_message)
{
    return gui_syncyesno_run_w_tmo(TIMEOUT_BLOCK, YESNO_NO, NULL,
                                   main_message, yes_message, no_message);
}

enum yesno_res gui_syncyesno_run_w_title(const char * title,
                                         const struct text_message * main_message,
                                         const struct text_message * yes_message,
                                         const struct text_message * no_message)
{
    return gui_syncyesno_run_w_tmo(TIMEOUT_BLOCK, YESNO_NO, title,
                                   main_message, yes_message, no_message);
}

bool yesno_pop(const char* text)
{
    const char *lines[] = { text };
    const struct text_message message = { lines, 1 };
    return gui_syncyesno_run(&message, NULL, NULL) == YESNO_YES;
}

bool yesno_pop_confirm(const char* text)
{
    const char *lines[] = { ID2P(LANG_ARE_YOU_SURE), text };
    const struct text_message message = { lines, 2 };
    bool ok = gui_syncyesno_run(&message, NULL, NULL) == YESNO_YES;
    if (!ok)
        splash(HZ, ID2P(LANG_CANCEL));
    return ok;
}
