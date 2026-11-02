#!/usr/bin/env python3
"""Chinese, Japanese and Korean fallback fonts for the Wheel edition.

Rockbox fonts hold one script well; the firmware now falls back, glyph by
glyph, to these fonts for characters a UI font lacks (font_set_fallbacks).
They come from Noto Sans SC / JP / KR (SIL OFL 1.1), cut to the character
sets people actually use - GB 2312, JIS X 0208, KS X 1001 Hangul - and
made into .fnt files in two sizes with the same forge the player uses.

usage: build_cjk.py GOOGLE_FONTS_CHECKOUT OUTDIR
"""
import os
import shutil
import subprocess
import sys
import tempfile

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

GF, OUT = sys.argv[1], sys.argv[2]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def charset(codec, lead=range(0xA1, 0xFF), trail=range(0xA1, 0xFF)):
    s = set()
    for a in lead:
        for b in trail:
            try:
                t = bytes([a, b]).decode(codec)
            except UnicodeDecodeError:
                continue
            if len(t) == 1:
                s.add(ord(t))
    return s


COMMON = set(range(0x3000, 0x3040)) | set(range(0xFF01, 0xFF5F)) | set(range(0x2190, 0x2200)) \
    | set(range(0x2460, 0x24FF)) | set(range(0x25A0, 0x2600)) | set(range(0x2600, 0x2700))
SETS = {
    # language: (google/fonts dir, file, code points)
    "SC": ("ofl/notosanssc", "NotoSansSC[wght].ttf", charset("gb2312") | COMMON),
    "JP": ("ofl/notosansjp", "NotoSansJP[wght].ttf", charset("euc_jp") | set(range(0x3040, 0x3100)) | COMMON),
    "KR": ("ofl/notosanskr", "NotoSansKR[wght].ttf",
           {c for c in charset("euc_kr") if not 0x4E00 <= c <= 0x9FFF} | set(range(0x3130, 0x3190)) | COMMON),
}
SIZES = [(17, 21), (22, 27)]   # em px, line height: details / everything else
FIRST, LAST = 0x2190, 0xFFEF


def main():
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp()
    forge = os.path.join(tmp, "forge")
    subprocess.check_call(["gcc", "-O2", "-I", os.path.join(ROOT, "apps", "wheel"), "-o", forge,
                           os.path.join(ROOT, "utils", "wheel", "typography", "forge_cli.c"),
                           os.path.join(ROOT, "apps", "wheel", "wheel_fontforge.c")])
    for lang, (d, fname, cps) in SETS.items():
        font = TTFont(os.path.join(GF, d, fname))
        loc = {a.axisTag: a.defaultValue for a in font["fvar"].axes}
        loc["wght"] = 500
        font = instancer.instantiateVariableFont(font, loc)
        cps = {c for c in cps if FIRST <= c <= LAST}
        opts = subset.Options()
        opts.layout_features = []
        opts.hinting = False
        opts.notdef_outline = True
        opts.drop_tables += ["DSIG", "STAT", "GDEF", "GPOS", "GSUB", "VORG", "vhea", "vmtx"]
        sub = subset.Subsetter(opts)
        # 'H' and 'x' only for the forge's baseline maths
        sub.populate(unicodes=sorted(cps | {ord("H"), ord("x")}))
        sub.subset(font)
        ttf = os.path.join(tmp, "cjk-%s.ttf" % lang)
        font.save(ttf)
        for em, band in SIZES:
            out = os.path.join(OUT, "CJK-%s-%d.fnt" % (lang, em))
            r = subprocess.run([forge, "-A", "-s", hex(FIRST), "-l", hex(LAST), "-p", str(em),
                                "-b", str(band), "-k", "1", ttf, out], capture_output=True, text=True)
            print(r.stdout.strip() or r.stderr.strip())
        shutil.copy(os.path.join(GF, d, "OFL.txt"), os.path.join(OUT, "OFL-NotoSans%s.txt" % lang))


if __name__ == "__main__":
    main()
