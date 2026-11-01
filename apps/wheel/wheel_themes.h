/* Y1 Wheel edition - theme picker, .zip install, 320x240 theme scaling. GPLv2+ */
#ifndef WHEEL_THEMES_H
#define WHEEL_THEMES_H
#include <stdbool.h>
#include <stddef.h>
#include "skin_parser.h" /* skin_scale_num/den, skin_scale_value() */

int  wheel_theme_screen(void);
const char *wheel_theme_current(void);
bool wheel_install_theme_zip(const char *zip_path, char *theme_out, size_t len);
/* list NAME of every NAME.ext in a .rockbox sub-folder (user + built-in) */
int  wheel_dir_list(const char *rbdir, const char *ext, char names[][64], int max);

/* used by the skin engine while a scaled skin is parsed */
bool wheel_skin_is_small(const char *text, int *max_r, int *max_b);
const char *wheel_scaled_font_name(const char *name, char *buf, size_t len);
#endif
