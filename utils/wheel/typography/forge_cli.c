/* PC front end of the Wheel font forge (apps/wheel/wheel_fontforge.c):
 * makes the .fnt files shipped with the default theme, and lets you try
 * the on-device conversion on a PC.
 *
 *   gcc -O2 -I apps/wheel -o forge utils/wheel/typography/forge_cli.c apps/wheel/wheel_fontforge.c
 *   forge [-p px] [-b band] [-t tracking] [-T] [-c] [-e px] [-k contrast]
 *         [-l last] [-r] [-f fallback.ttf] [-i] in.ttf|in.fnt out.fnt
 *
 * GPLv2+ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wheel_fontforge.h"

static unsigned char *slurp(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(*size);
    if (fread(d, 1, *size, f) != (size_t)*size) { perror(path); exit(1); }
    fclose(f);
    return d;
}

static bool wr(void *ctx, const void *buf, size_t len)
{
    return fwrite(buf, 1, len, (FILE *)ctx) == len;
}

int main(int argc, char **argv)
{
    struct forge_params p = { .px = 22, .band = 0, .last = 0x2122 };
    const char *fb = NULL;
    bool info = false;
    int i;
    for (i = 1; i < argc && argv[i][0] == '-'; i++)
    {
        char o = argv[i][1];
        if (o == 'T') p.tabular = true;
        else if (o == 'c') p.crisp = true;
        else if (o == 'r') p.extras = FORGE_EXTRA_ROW;
        else if (o == 'i') info = true;
        else if (i + 1 < argc)
        {
            const char *v = argv[++i];
            switch (o)
            {
                case 'p': p.px = atoi(v); break;
                case 'b': p.band = atoi(v); break;
                case 't': p.tracking = atoi(v); break;
                case 'e': p.embolden = atoi(v); break;
                case 'k': p.contrast = atoi(v); break;
                case 'l': p.last = strtoul(v, NULL, 0); break;
                case 'f': fb = v; break;
                default: fprintf(stderr, "unknown option -%c\n", o); return 1;
            }
        }
    }
    if (info && i < argc)
    {
        struct forge_src s;
        s.data = slurp(argv[i], &s.size);
        struct forge_info fi;
        if (!forge_info(&s, &fi)) { fprintf(stderr, "not a font\n"); return 1; }
        printf("%s | %s | %s | weight %d%s | h %d asc %d\n", fi.outline ? "outline" : "bitmap",
               fi.family, fi.style, fi.weight, fi.variable ? " | variable" : "", fi.height, fi.ascent);
        static const char *bn[] = { "Latin", "Latin-1", "Latin Ext-A", "Greek", "Cyrillic", "Punctuation" };
        for (int b = 0; b < FB_COUNT; b++)
            printf("  %-12s %3d/%d\n", bn[b], fi.cover[b], fi.cover_of[b]);
        return 0;
    }
    if (i + 2 != argc)
    {
        fprintf(stderr, "usage: forge [options] in.ttf|in.fnt out.fnt\n");
        return 1;
    }
    struct forge_src s, f;
    s.data = slurp(argv[i], &s.size);
    if (fb)
        f.data = slurp(fb, &f.size);
    FILE *out = fopen(argv[i + 1], "wb");
    if (!out) { perror(argv[i + 1]); return 1; }
    struct forge_stats st;
    int r = forge_build(&s, fb ? &f : NULL, &p, wr, out, &st);
    fclose(out);
    if (r < 0) { fprintf(stderr, "forge failed (%d)\n", r); remove(argv[i + 1]); return 1; }
    printf("%s: %d glyphs (+%d fallback, %d missing), h %d asc %d maxw %d, %ld bytes\n",
           argv[i + 1], st.glyphs, st.fallback, st.missing, st.height, st.ascent, st.maxwidth, st.bytes);
    return 0;
}
