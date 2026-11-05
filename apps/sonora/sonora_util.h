/***************************************************************************
 * SONORA - small helpers shared by the parsers, the store and the screens.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef SONORA_UTIL_H
#define SONORA_UTIL_H

#include "sonora_port.h"

/* case-insensitive "natural" order: "Part 2" < "Part 10" */
int  sonora_natcmp(const char *a, const char *b);
/* copy, turning tabs / newlines into spaces (safe for TSV fields) */
void sonora_clean(char *dst, const char *src, size_t len);
/* append one code point as UTF-8; returns bytes written (0 = no room) */
size_t sonora_put_utf8(char *out, size_t room, unsigned cp);
void sonora_latin1_to_utf8(const unsigned char *in, size_t n, char *out, size_t len);
void sonora_utf16_to_utf8(const unsigned char *in, size_t n, bool big_endian,
                          char *out, size_t len);
/* XML character data -> plain UTF-8 text: CDATA, entities, tags removed,
 * white space collapsed, trimmed */
void sonora_xml_text(const char *in, size_t n, char *out, size_t len);
uint32_t sonora_hash(const char *s);
/* split a line on tabs, in place; returns the number of fields */
int  sonora_split(char *line, char **fields, int max);
bool sonora_is_audio(const char *name);
bool sonora_ext_is(const char *name, const char *ext);
/* "1:02:03" or "2:03" */
void sonora_fmt_time(char *buf, size_t len, unsigned long ms);
/* "Episode 12.mp3" -> "Episode 12" */
void sonora_basename_noext(const char *path, char *out, size_t len);
/* safe file name from a title (no / \ : * ? " < > |, max len) */
void sonora_safe_name(const char *in, char *out, size_t len);

/* buffered line reader over a file descriptor */
struct sonora_lines {
    int fd;
    int pos, end;
    char buf[2048];
};
void sonora_lines_init(struct sonora_lines *r, int fd);
/* next line without the newline; false at the end. Overlong lines are cut. */
bool sonora_lines_next(struct sonora_lines *r, char *line, size_t len);

#endif
