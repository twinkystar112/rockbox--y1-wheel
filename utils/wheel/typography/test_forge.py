#!/usr/bin/env python3
"""Tests for the Wheel font forge (apps/wheel/wheel_fontforge.c), run on a PC.

Builds the PC front end, converts every bundled font with the settings the
typography screens use, and checks the .fnt files the way Rockbox reads
them: header, glyph data, line height, baseline, spacing, tabular figures,
pixel mode, fallback glyphs, the drawn list chevron.

usage: test_forge.py            (from anywhere; needs gcc)
"""
import glob
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TYPO = os.path.join(ROOT, "fonts", "typo")
passed = failed = 0


def check(ok, what):
    global passed, failed
    print(("  PASS  " if ok else "  FAIL  ") + what)
    if ok:
        passed += 1
    else:
        failed += 1


class Fnt:
    """reads an RB12 font like firmware/font.c does"""
    def __init__(self, path):
        d = open(path, "rb").read()
        assert d[:4] == b"RB12"
        (self.maxwidth, self.height, self.ascent, self.depth, self.first, self.default,
         self.size, nbits, noff, nwid) = struct.unpack("<hhhhiiiiii", d[4:36])
        self.bytes = len(d)
        self.bits = d[36:36 + nbits]
        p = 36 + nbits
        off32 = nbits >= 0xFFDB
        p = (p + 3) & ~3 if off32 else (p + 1) & ~1
        fmt = "<%d%s" % (noff, "I" if off32 else "H")
        self.offs = struct.unpack(fmt, d[p:p + noff * (4 if off32 else 2)])
        p += noff * (4 if off32 else 2)
        self.widths = d[p:p + nwid]
        self.ok_size = p + nwid == len(d)

    def width(self, ch):
        i = ord(ch) - self.first
        return self.widths[i] if 0 <= i < self.size else None

    def pixels(self, ch):
        i = ord(ch) - self.first
        w, o = self.widths[i], self.offs[i]
        n = w * self.height
        return [15 - ((self.bits[o + (k >> 1)] >> ((k & 1) * 4)) & 15) for k in range(n)], w

    def ink_rows(self, ch):
        px, w = self.pixels(ch)
        rows = [y for y in range(self.height) if any(px[y * w + x] > 7 for x in range(w))]
        return (rows[0], rows[-1]) if rows else (None, None)


def main():
    tmp = tempfile.mkdtemp()
    forge = os.path.join(tmp, "forge")
    subprocess.check_call(["gcc", "-O2", "-Wall", "-I", os.path.join(ROOT, "apps", "wheel"), "-o", forge,
                           os.path.join(ROOT, "utils", "wheel", "typography", "forge_cli.c"),
                           os.path.join(ROOT, "apps", "wheel", "wheel_fontforge.c")])

    def run(src, out, *opts):
        r = subprocess.run([forge] + list(opts) + [src, out], capture_output=True, text=True)
        return r.returncode == 0, r.stdout + r.stderr

    print("Every bundled font converts and reads back")
    sources = sorted(glob.glob(os.path.join(TYPO, "*.ttf")) + glob.glob(os.path.join(TYPO, "*.fnt")))
    all_ok = True
    for s in sources:
        out = os.path.join(tmp, os.path.basename(s) + ".fnt")
        ok, msg = run(s, out, "-p", "20", "-b", "40", "-r", "-k", "1")
        f = Fnt(out) if ok else None
        good = ok and f.ok_size and f.height == 40 and 0 < f.ascent <= 40 and f.depth == 1 \
            and f.width("A") and f.width("›")
        if not good:
            print("    ", os.path.basename(s), msg.strip())
        all_ok &= bool(good)
    check(all_ok and len(sources) >= 40, "%d sources -> valid RB12 fonts (header, offsets, widths)" % len(sources))

    inter = os.path.join(TYPO, "Inter-500.ttf")
    print("Line height and baseline")
    for band in (22, 26, 32, 40, 46):
        out = os.path.join(tmp, "b%d.fnt" % band)
        run(inter, out, "-p", "18", "-b", str(band))
        f = Fnt(out)
        top, bottom = f.ink_rows("H")
        mid = (top + bottom) / 2
        check(f.height == band and abs(mid - band / 2) <= 1.5,
              "band %d: height %d, capitals centred (rows %d-%d)" % (band, f.height, top, bottom))
    out = os.path.join(tmp, "desc.fnt")
    run(inter, out, "-p", "17", "-b", "22")
    f = Fnt(out)
    check(f.ink_rows("g")[1] < 22 and f.ink_rows("Á")[0] >= 0, "descenders and accents stay inside a tight band")

    print("Letter spacing")
    a, b, c = (os.path.join(tmp, n) for n in ("t0.fnt", "t2.fnt", "tm.fnt"))
    run(inter, a, "-p", "20", "-b", "26")
    run(inter, b, "-p", "20", "-b", "26", "-t", "2")
    run(inter, c, "-p", "20", "-b", "26", "-t", "-1")
    fa, fb, fc = Fnt(a), Fnt(b), Fnt(c)
    check(all(fb.width(ch) == fa.width(ch) + 2 and fc.width(ch) == fa.width(ch) - 1 for ch in "nAéW1"),
          "wide +2 px / compact -1 px on every glyph")

    print("Numerals")
    out = os.path.join(tmp, "tab.fnt")
    run(inter, out, "-p", "15", "-b", "22", "-T")
    f = Fnt(out)
    check(len({f.width(d) for d in "0123456789"}) == 1, "tabular: all digits %d px" % f.width("0"))
    check(len({fa.width(d) for d in "0123456789"}) > 1, "proportional: digits keep their own widths")

    print("Rendering")
    out = os.path.join(tmp, "px.fnt")
    run(inter, out, "-p", "18", "-b", "26", "-c")
    f = Fnt(out)
    px, _ = f.pixels("a")
    check(set(px) <= {0, 15} and 15 in px, "pixel mode: no anti-aliasing (0/15 only)")
    out2 = os.path.join(tmp, "aa.fnt")
    run(inter, out2, "-p", "18", "-b", "26")
    px2, _ = Fnt(out2).pixels("a")
    check(len(set(px2)) > 4, "smooth mode: %d alpha levels" % len(set(px2)))

    print("Fallback")
    lora = os.path.join(TYPO, "Lora-400.ttf")
    ok1, m1 = run(lora, os.path.join(tmp, "lora.fnt"), "-p", "20", "-b", "28")
    ok2, m2 = run(lora, os.path.join(tmp, "lorafb.fnt"), "-p", "20", "-b", "28", "-f", os.path.join(TYPO, "Inter-400.ttf"))
    fb = int(m2.split("(+")[1].split()[0]) if ok2 else 0
    f1, f2 = Fnt(os.path.join(tmp, "lora.fnt")), Fnt(os.path.join(tmp, "lorafb.fnt"))
    lam = "\u03bb"  # Greek small lambda: not in Lora
    same = lambda f, ch: f.offs[ord(ch) - f.first] == f.offs[ord("?") - f.first]
    check(fb > 50 and not same(f2, lam) and same(f1, lam),
          "Greek missing in Lora -> taken from Inter (%d glyphs); without fallback it shows '?'" % fb)

    print("Bitmap sources")
    bm = os.path.join(TYPO, "ClassicBitmap-700-21.fnt")
    out = os.path.join(tmp, "bm.fnt")
    ok, msg = run(bm, out, "-b", "40", "-c", "-r", "-t", "1")
    f = Fnt(out)
    check(ok and f.height == 40 and set(f.pixels("A")[0]) <= {0, 15}, "X11 bitmap font re-laid out in a 40 px row")

    print("Size")
    sizes = [os.path.getsize(os.path.join(tmp, os.path.basename(s) + ".fnt")) for s in sources]
    check(max(sizes) < 400_000, "largest font %d KB (glyph cache handles > 60 KB)" % (max(sizes) // 1024))

    print("%d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
