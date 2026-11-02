/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - letter jump.
 *
 * Spin the wheel fast in a long list and it stops moving row by row and
 * jumps from initial to initial (A, B, C ...), with the letter shown big in
 * the middle of the screen. Slow down and it scrolls normally again.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
#include <string.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "button.h"
#include "font.h"
#include "screen_access.h"
#include "settings.h"
#include "wheel_typo.h"
#include "list.h"
#include "lang.h"
#include "rbunicode.h"
#include "rbpaths.h"
#include "wheel_letterjump.h"

#define MIN_ITEMS        40          /* only worth it in long lists */
#define FAST_STEPS       5           /* this many steps ...           */
#define FAST_WINDOW      (HZ*40/100) /* ... within this time = fast   */
#define SLOW_GAP         (HZ*30/100) /* a pause this long ends jumping */
#define OVERLAY_TIME     (HZ*70/100)

static long steps[FAST_STEPS];
static int  step_pos;
static int  last_dir;
static long last_step;
static bool jumping;
static long overlay_until;
static char overlay_letter[8];
static int  jump_font = -1;
static bool jump_font_tried;
static struct timeout overlay_tmo;
bool wheel_letterjump_suppress; /* set by screens where it makes no sense */

/* "The Beatles" sorts under B on most players; accents fold to the base
 * letter; digits and symbols share '#' */
static void initial_of(const char *name, char *out, size_t len)
{
    const unsigned char *p = (const unsigned char *)name;
    while (*p == ' ' || *p == '\t' || *p == '[' || *p == '(' || *p == '"' || *p == '\'')
        p++;
    if (!strncasecmp((const char *)p, "the ", 4))
        p += 4;
    ucschar_t c;
    utf8decode(p, &c);
    if (c >= 0xC0 && c <= 0xFF)
    {   /* Latin-1 letters with accents */
        static const char fold[] = "AAAAAAACEEEEIIII" "DNOOOOO*OUUUUYTS"
                                   "AAAAAAACEEEEIIII" "DNOOOOO/OUUUUYTY";
        c = fold[c - 0xC0];
    }
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    if (!(c >= 'A' && c <= 'Z'))
        c = '#';
    out[0] = (char)c;
    out[1] = '\0';
    (void)len;
}

static void item_initial(struct gui_synclist *l, int item, char *out, size_t len)
{
    char buf[MAX_PATH];
    const char *s = l->callback_get_item_name(item, l->data, buf, sizeof(buf));
    initial_of(P2STR((unsigned char *)s), out, len);
}

static int overlay_expire(struct timeout *tmo)
{
    (void)tmo;
    button_queue_post(BUTTON_REDRAW, 0);
    return 0;
}

/* returns true when it moved the selection itself */
bool wheel_letterjump_step(struct gui_synclist *lists, int dir)
{
    long now = current_tick;
    if (!global_settings.wheel_letter_jump || wheel_letterjump_suppress ||
        lists->nb_items < MIN_ITEMS ||
        lists->selected_size != 1 || !lists->callback_get_item_name)
    {
        jumping = false;
        return false;
    }

    if (dir != last_dir || TIME_AFTER(now, last_step + SLOW_GAP))
    {   /* direction change or a pause: back to row by row */
        jumping = false;
        memset(steps, 0, sizeof(steps));
    }
    last_dir = dir;
    last_step = now;
    steps[step_pos] = now;
    step_pos = (step_pos + 1) % FAST_STEPS;
    long oldest = steps[step_pos];
    if (!jumping && oldest && TIME_BEFORE(now, oldest + FAST_WINDOW))
        jumping = true;
    if (!jumping)
        return false;

    char cur[8], next[8];
    int sel = lists->selected_item;
    item_initial(lists, sel, cur, sizeof(cur));
    int i = sel;
    for (;;)
    {
        i += dir;
        if (i < 0 || i >= lists->nb_items)
        {   /* stop at the ends, never wrap while flying */
            i = dir > 0 ? lists->nb_items - 1 : 0;
            break;
        }
        item_initial(lists, i, next, sizeof(next));
        if (strcmp(next, cur))
        {
            if (dir < 0)
            {   /* land on the first item of that letter */
                char prev[8];
                while (i > 0)
                {
                    item_initial(lists, i - 1, prev, sizeof(prev));
                    if (strcmp(prev, next))
                        break;
                    i--;
                }
            }
            break;
        }
    }
    gui_synclist_select_item(lists, i);
    item_initial(lists, i, overlay_letter, sizeof(overlay_letter));
    overlay_until = now + OVERLAY_TIME;
    timeout_register(&overlay_tmo, overlay_expire, OVERLAY_TIME + 1, 0);
    return true;
}

/* drawn on top of the list after it has been rendered */
void wheel_letterjump_draw(struct gui_synclist *lists, struct screen *display)
{
    if (!overlay_letter[0] || !TIME_BEFORE(current_tick, overlay_until))
        return;
    jump_font_tried = true;
    jump_font = typo_font(TU_JUMBO);   /* big letters in the titles font */
    struct viewport *parent = lists->parent[display->screen_type];
    struct viewport vp = *parent;
    struct viewport *last = display->set_viewport(&vp);
    int size = MIN(vp.width, vp.height) * 4 / 10;
    if (size < 60) size = 60;
    int x = (vp.width - size) / 2, y = (vp.height - size) / 2;

#if LCD_DEPTH > 1
    unsigned fg = display->get_foreground(), bg = display->get_background();
    display->set_foreground(LCD_RGBPACK(0x2a, 0x2a, 0x30));
#endif
    display->set_drawmode(DRMODE_SOLID);
    /* rounded card (radius ~8 px, built from stacked rectangles) */
    static const unsigned char inset[8] = { 8, 5, 4, 3, 2, 1, 1, 0 };
    for (int r = 0; r < 8; r++)
    {
        display->hline(x + inset[r], x + size - 1 - inset[r], y + r);
        display->hline(x + inset[r], x + size - 1 - inset[r], y + size - 1 - r);
    }
    display->fillrect(x, y + 8, size, size - 16);

    int old_font = vp.font;
    if (jump_font >= 0)
        vp.font = jump_font;
#if LCD_DEPTH > 1
    display->set_foreground(LCD_RGBPACK(0xff, 0xff, 0xff));
#endif
    int w, h;
    display->getstringsize(overlay_letter, &w, &h);
    display->set_drawmode(DRMODE_FG);
    display->putsxy(x + (size - w) / 2, y + (size - h) / 2, overlay_letter);
    display->set_drawmode(DRMODE_SOLID);
    vp.font = old_font;
#if LCD_DEPTH > 1
    display->set_foreground(fg);
    display->set_background(bg);
#endif
    display->update_viewport_rect(x, y, size, size);
    display->set_viewport(last);
}

bool wheel_letterjump_active(void)
{
    return overlay_letter[0] && TIME_BEFORE(current_tick, overlay_until);
}
