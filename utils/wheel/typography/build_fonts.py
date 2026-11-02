#!/usr/bin/env python3
"""Build the source fonts of the Wheel typography system into fonts/typo/.

The firmware converts these to Rockbox .fnt on the device (apps/wheel/
wheel_fontforge.c), with the size, spacing, line height and numerals the
user picks. This script only prepares the sources:

  * open-source (SIL OFL 1.1) families from the Google Fonts repository,
    cut to static weights (the on-device rasteriser reads one instance of
    a variable font) and subset to the scripts the Y1 menus need
    (Latin, Latin-1, Latin Extended-A, Greek, Cyrillic, punctuation);
  * the X11 Adobe bitmap fonts that already ship with Rockbox, compiled to
    .fnt for the "Classic" (bitmap) look.

No proprietary font is bundled. People who own Chicago, Geneva, SF Pro or
Avenir Next can copy their files to /.rockbox/fonts/import/ on the player.

usage: build_fonts.py GOOGLE_FONTS_CHECKOUT OUTDIR [CONVBDF]
"""
import os
import shutil
import subprocess
import sys

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

GF, OUT = sys.argv[1], sys.argv[2]
CONVBDF = sys.argv[3] if len(sys.argv) > 3 else None
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

# family name, google/fonts dir, file, weights (None: static file per weight)
FAMILIES = [
    ("Inter",            "ofl/inter",            "Inter[opsz,wght].ttf",           (300, 400, 500, 600, 700, 800)),
    ("Manrope",          "ofl/manrope",          "Manrope[wght].ttf",              (300, 400, 500, 600, 700, 800)),
    ("Nunito Sans",      "ofl/nunitosans",       "NunitoSans[YTLC,opsz,wdth,wght].ttf", (300, 400, 600, 700)),
    ("IBM Plex Mono",    "ofl/ibmplexmono",      "IBMPlexMono-%s.ttf",             (400, 500, 600)),
    ("VT323",            "ofl/vt323",            "VT323-Regular.ttf",              (400,)),
    ("DM Sans",          "ofl/dmsans",           "DMSans[opsz,wght].ttf",          (400, 500, 700)),
    ("Playfair Display", "ofl/playfairdisplay",  "PlayfairDisplay[wght].ttf",      (400, 600, 700)),
    ("Lora",             "ofl/lora",             "Lora[wght].ttf",                 (400, 500, 600, 700)),
]
# optical size: the player's ~250 dpi screen shows UI text at 13-28 px;
# the "display" cuts keep spacing as tight as the old Inter 3 fonts
OPSZ = {"Inter": 32, "DM Sans": 24}
STATIC_NAMES = {300: "Light", 400: "Regular", 500: "Medium", 600: "SemiBold", 700: "Bold", 800: "ExtraBold"}

UNICODES = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x180)) + [0x192, 0x2C6, 0x2C7, 0x2D8, 0x2D9, 0x2DA, 0x2DB, 0x2DC, 0x2DD]
            + list(range(0x370, 0x400)) + list(range(0x400, 0x460))
            + list(range(0x2010, 0x2028)) + list(range(0x2030, 0x203B)) + [0x2044, 0x20AC, 0x2116, 0x2122]
            + list(range(0x2190, 0x2194)) + [0x2212, 0x2713])


def tag_name(family, weight):
    return "%s-%d.ttf" % (family.replace(" ", ""), weight)


def fix_names(font, family, weight):
    """family/weight as the firmware's registry reads them (name IDs 1/2/16/17, OS/2)"""
    style = STATIC_NAMES.get(weight, str(weight))
    name = font["name"]
    for rec in list(name.names):
        if rec.nameID in (1, 2, 4, 6, 16, 17, 25):
            name.removeNames(nameID=rec.nameID)
    name.setName(family, 16, 3, 1, 0x409)
    name.setName(style, 17, 3, 1, 0x409)
    name.setName(family if weight in (400, 700) else "%s %s" % (family, style), 1, 3, 1, 0x409)
    name.setName(style if weight in (400, 700) else "Regular", 2, 3, 1, 0x409)
    name.setName("%s %s" % (family, style), 4, 3, 1, 0x409)
    name.setName(("%s-%s" % (family, style)).replace(" ", ""), 6, 3, 1, 0x409)
    font["OS/2"].usWeightClass = weight


def build_outline():
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for family, d, fname, weights in FAMILIES:
        src_dir = os.path.join(GF, d)
        for w in weights:
            if "%s" in fname:
                font = TTFont(os.path.join(src_dir, fname % STATIC_NAMES[w]))
            else:
                font = TTFont(os.path.join(src_dir, fname))
                if "fvar" in font:
                    loc = {a.axisTag: a.defaultValue for a in font["fvar"].axes}
                    loc["wght"] = w
                    if "opsz" in loc and family in OPSZ:
                        loc["opsz"] = OPSZ[family]
                    font = instancer.instantiateVariableFont(font, loc)
            opts = subset.Options()
            opts.layout_features = []          # no shaping on the player
            opts.hinting = False               # the rasteriser ignores hints
            opts.name_IDs = ["*"]
            opts.notdef_outline = True
            opts.glyph_names = False
            opts.drop_tables += ["DSIG", "STAT", "MVAR", "HVAR", "GDEF", "GPOS", "GSUB", "kern", "meta"]
            sub = subset.Subsetter(opts)
            sub.populate(unicodes=UNICODES)
            sub.subset(font)
            fix_names(font, family, w)
            out = os.path.join(OUT, tag_name(family, w))
            font.save(out)
            total += os.path.getsize(out)
            print("  %-28s %6d bytes" % (os.path.basename(out), os.path.getsize(out)))
        lic = os.path.join(src_dir, "OFL.txt")
        shutil.copy(lic, os.path.join(OUT, "OFL-%s.txt" % family.replace(" ", "")))
    print("outline sources: %d KB" % (total // 1024))


# X11 Adobe Helvetica bitmaps (already part of Rockbox, X11 licence: see
# fonts/COPYING). File prefix = line height; the family is shown as
# "Classic Bitmap" (a look, not a brand).
BITMAPS = ["14-Adobe-Helvetica-Bold", "15-Adobe-Helvetica", "16-Adobe-Helvetica",
           "16-Adobe-Helvetica-Bold", "18-Adobe-Helvetica", "18-Adobe-Helvetica-Bold",
           "21-Adobe-Helvetica", "21-Adobe-Helvetica-Bold", "27-Adobe-Helvetica",
           "27-Adobe-Helvetica-Bold", "35-Adobe-Helvetica", "35-Adobe-Helvetica-Bold"]


def build_bitmap():
    if not CONVBDF:
        print("no convbdf given: bitmap fonts skipped")
        return
    for b in BITMAPS:
        bold = b.endswith("-Bold")
        h = b.split("-")[0]
        out = os.path.join(OUT, "ClassicBitmap-%d-%s.fnt" % (700 if bold else 400, h))
        subprocess.check_call([CONVBDF, "-f", "-l", "8482", "-o", out,
                               os.path.join(ROOT, "fonts", b + ".bdf")],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("  %-28s %6d bytes" % (os.path.basename(out), os.path.getsize(out)))
    with open(os.path.join(OUT, "LICENSE-ClassicBitmap.txt"), "w") as f:
        f.write("Classic Bitmap = the X11 Adobe Helvetica bitmap fonts shipped with\n"
                "Rockbox (fonts/*.bdf). Copyright 1984-1989, 1994 Adobe Systems\n"
                "Incorporated; 1988, 1994 Digital Equipment Corporation. Licence:\n"
                "see COPYING-fonts.txt / fonts/COPYING in the Rockbox sources.\n")


if __name__ == "__main__":
    build_outline()
    build_bitmap()
