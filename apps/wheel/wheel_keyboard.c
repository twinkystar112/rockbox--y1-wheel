/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - text entry with only a wheel.
 *
 * Replaces the on-screen keyboard (which needs a touchscreen or a d-pad
 * on the Y1): a list of characters you spin through and click, with the
 * text being typed shown as the title. Used by Search, playlist names,
 * renaming files and any plugin that asks for text.
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
#include "list.h"
#include "misc.h"
#include "rbunicode.h"
#include "string-extra.h"
#include "keyboard.h"
#include "wheel_letterjump.h"

enum { K_DONE = 0, K_DELETE, K_SPACE, K_CASE, K_FIRST_CHAR };

/* UTF-8 characters offered after the commands */
static const char * const chars[] = {
    "a","b","c","d","e","f","g","h","i","j","k","l","m",
    "n","o","p","q","r","s","t","u","v","w","x","y","z",
    "0","1","2","3","4","5","6","7","8","9",
    "\xc3\xa1","\xc3\xa9","\xc3\xad","\xc3\xb3","\xc3\xba", /* á é í ó ú */
    "\xc3\xb1","\xc3\xbc","\xc3\xa7",                       /* ñ ü ç */
    ".",",","-","'","&","(",")","!","?","/",":","_","+","#","@",
};
#define N_ROWS (K_FIRST_CHAR + (int)ARRAYLEN(chars))

static char *kb_text;
static int kb_len;
static bool kb_upper;
static bool kb_done;
static char kb_title[MAX_PATH];

/* "A" for "a" etc. (ASCII + the Latin-1 letters above) */
static void case_of(const char *in, char *out, size_t len)
{
    strlcpy(out, in, len);
    if (!kb_upper)
        return;
    if (out[0] >= 'a' && out[0] <= 'z')
        out[0] -= 'a' - 'A';
    else if ((unsigned char)out[0] == 0xc3 && (unsigned char)out[1] >= 0xa0
             && (unsigned char)out[1] <= 0xbe)
        out[1] -= 0x20; /* á -> Á in UTF-8 */
}

static void update_title(void)
{
    if (kb_text[0])
        snprintf(kb_title, sizeof(kb_title), "%s_", kb_text);
    else
        strlcpy(kb_title, str(LANG_WHEEL_KBD_HINT), sizeof(kb_title));
}

static const char *kb_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    switch (item)
    {
        case K_DONE:   return str(LANG_WHEEL_KBD_DONE);
        case K_DELETE: return str(LANG_WHEEL_KBD_DELETE);
        case K_SPACE:  return str(LANG_WHEEL_KBD_SPACE);
        case K_CASE:
            snprintf(buf, len, "%s" LIST_VALUE_SEP_STR "%s", str(LANG_WHEEL_KBD_CASE),
                     kb_upper ? "ABC" : "abc");
            return buf;
    }
    case_of(chars[item - K_FIRST_CHAR], buf, len);
    return buf;
}

static void delete_last(void)
{
    int n = strlen(kb_text);
    if (n == 0)
        return;
    /* step back over UTF-8 continuation bytes */
    n--;
    while (n > 0 && ((unsigned char)kb_text[n] & 0xc0) == 0x80)
        n--;
    kb_text[n] = '\0';
}

static int kb_action(int action, struct gui_synclist *lists)
{
    if (action == ACTION_STD_MENU || action == ACTION_STD_CANCEL)
        return ACTION_STD_CANCEL;
    if (action != ACTION_STD_OK)
        return action;

    int sel = gui_synclist_get_sel_pos(lists);
    char ch[8];
    switch (sel)
    {
        case K_DONE:
            kb_done = true;
            return ACTION_STD_CANCEL;
        case K_DELETE:
            delete_last();
            break;
        case K_SPACE:
            if ((int)strlen(kb_text) + 1 < kb_len)
                strcat(kb_text, " ");
            break;
        case K_CASE:
            kb_upper = !kb_upper;
            break;
        default:
            case_of(chars[sel - K_FIRST_CHAR], ch, sizeof(ch));
            if ((int)(strlen(kb_text) + strlen(ch)) < kb_len)
                strcat(kb_text, ch);
            /* after the first letter, go on in lower case like a name */
            if (kb_upper && strlen(kb_text) == strlen(ch))
                kb_upper = false;
            break;
    }
    update_title();
    gui_synclist_set_title(lists, kb_title, Icon_NOICON);
    return ACTION_REDRAW;
}

int kbd_input(char* text, int buflen, ucschar_t *kbd)
{
    (void)kbd;
    struct simplelist_info info;
    char backup[MAX_PATH];
    strlcpy(backup, text, sizeof(backup));

    kb_text = text;
    kb_len = buflen;
    kb_done = false;
    kb_upper = text[0] == '\0'; /* first letter in capitals */
    update_title();

    simplelist_info_init(&info, kb_title, N_ROWS, NULL);
    info.get_name = kb_get_name;
    info.action_callback = kb_action;
    info.selection = text[0] ? K_DONE : K_FIRST_CHAR;
    wheel_letterjump_suppress = true;
    simplelist_show_list(&info);
    wheel_letterjump_suppress = false;

    if (!kb_done)
    {   /* cancelled: leave the caller's text untouched */
        strlcpy(text, backup, buflen);
        return -1;
    }
    return 0;
}

/* custom layouts are for the touch keyboard; nothing to load here */
int load_kbd(unsigned char* filename)
{
    (void)filename;
    return 0;
}
