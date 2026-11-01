/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - "Sign in with your phone" for Last.fm.
 *
 * Shows Last.fm's authorisation link as a QR code; the user scans it with
 * a phone, taps "Yes, allow access", and the player picks up the session
 * by itself. No typing with the wheel, no config file with a password.
 * QR encoding: Project Nayuki's QR Code generator (MIT, qrcodegen.c).
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
#include "settings.h"
#include "misc.h"
#include "string-extra.h"
#include "qrcodegen.h"
#include "wheel_connect.h"
#include "wheel_screens.h"

#define QR_MAX_VERSION 10

static void draw_qr(struct screen *d, const uint8_t *qr, int x, int y, int box)
{
    int n = qrcodegen_getSize(qr);
    int quiet = 3;
    int mod = box / (n + 2 * quiet);
    if (mod < 1) mod = 1;
    int w = mod * (n + 2 * quiet);
    x += (box - w) / 2;
    y += (box - w) / 2;
#if LCD_DEPTH > 1
    d->set_foreground(LCD_RGBPACK(0xff, 0xff, 0xff));
#endif
    d->set_drawmode(DRMODE_SOLID);
    d->fillrect(x, y, w, w);
#if LCD_DEPTH > 1
    d->set_foreground(LCD_RGBPACK(0x10, 0x10, 0x12));
#endif
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (qrcodegen_getModule(qr, c, r))
                d->fillrect(x + (c + quiet) * mod, y + (r + quiet) * mod, mod, mod);
}

static void draw_screen(const char *url, int state)
{
    static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
    static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
    struct screen *d = &screens[SCREEN_MAIN];
    struct viewport vp;
    viewport_set_defaults(&vp, SCREEN_MAIN);
    vp.x = 0; vp.y = 0; vp.width = LCD_WIDTH; vp.height = LCD_HEIGHT;
    struct viewport *last = d->set_viewport(&vp);
#if LCD_DEPTH > 1
    unsigned fg = d->get_foreground(), bg = d->get_background();
    d->set_background(global_settings.bg_color);
    d->set_foreground(global_settings.fg_color);
#endif
    d->clear_viewport();

    int fh = font_get(vp.font)->height;
    int box = MIN(LCD_HEIGHT - 40, LCD_WIDTH / 2);
    int text_x = box + 36;
    int y = 24;
    d->putsxy(text_x, y, "Last.fm");
    y += fh + 12;
    if (url[0])
    {
        if (qrcodegen_encodeText(url, tmp, qr, qrcodegen_Ecc_LOW, 1, QR_MAX_VERSION,
                                 qrcodegen_Mask_AUTO, true))
            draw_qr(d, qr, 16, (LCD_HEIGHT - box) / 2, box);
#if LCD_DEPTH > 1
        d->set_foreground(global_settings.fg_color);
#endif
        static const int steps[] = { LANG_WHEEL_QR_STEP1, LANG_WHEEL_QR_STEP2, LANG_WHEEL_QR_STEP3 };
        for (unsigned i = 0; i < ARRAYLEN(steps); i++)
        {
            char line[96];
            snprintf(line, sizeof(line), "%u. %s", i + 1, str(steps[i]));
            d->putsxy(text_x, y, line);
            y += fh + 8;
        }
    }
    else
    {
        d->putsxy(text_x, y, str(state < 0 ? LANG_WHEEL_LFM_OFFLINE : LANG_WAIT));
    }
#if LCD_DEPTH > 1
    d->set_foreground(fg);
    d->set_background(bg);
#endif
    d->update_viewport();
    d->set_viewport(last);
}

int wheel_lastfm_qr_signin(void)
{
    if (!wheel_scrobble_web_auth_start())
    {
        splash(HZ*3, ID2P(LANG_WHEEL_QR_NEEDS));
        return 0;
    }
    FOR_NB_SCREENS(i)
        viewportmanager_theme_enable(i, false, NULL);

    char shown[256] = "";
    int state = 0;
    bool redraw = true;
    long started = current_tick;
    while (1)
    {
        const char *url = wheel_scrobble_web_auth_url();
        state = wheel_scrobble_web_auth_state();
        if (strcmp(url, shown))
        {
            strlcpy(shown, url, sizeof(shown));
            redraw = true;
        }
        if (redraw)
        {
            draw_screen(shown, state);
            redraw = false;
        }
        if (state != 0)
            break;
        int action = get_action(CONTEXT_STD, HZ/2);
        if (action == ACTION_STD_CANCEL || action == ACTION_STD_MENU)
            break;
        if (default_event_handler(action) == SYS_USB_CONNECTED)
            break;
        if (TIME_AFTER(current_tick, started + HZ*600))
            break;
    }
    wheel_scrobble_web_auth_cancel();
    FOR_NB_SCREENS(i)
        viewportmanager_theme_undo(i, true);

    if (state == 1)
    {
        struct wheel_scrobble_status st;
        wheel_scrobble_get_status(&st);
        splashf(HZ*2, "%s: %s", str(LANG_WHEEL_LFM_ACCOUNT), st.user);
    }
    else if (state < 0)
        splash(HZ*2, ID2P(LANG_WHEEL_BT_FAILED));
    return 0;
}
