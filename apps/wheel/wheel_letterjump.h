/* Y1 Wheel edition - letter jump (fast wheel spin jumps by initial). GPLv2+ */
#ifndef WHEEL_LETTERJUMP_H
#define WHEEL_LETTERJUMP_H
#include <stdbool.h>
struct gui_synclist;
struct screen;
bool wheel_letterjump_step(struct gui_synclist *lists, int dir);
void wheel_letterjump_draw(struct gui_synclist *lists, struct screen *display);
bool wheel_letterjump_active(void);
extern bool wheel_letterjump_suppress;
#endif
