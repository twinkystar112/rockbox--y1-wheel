"""Build an anti-aliased Rockbox .fnt (RB12, 4-bit) whose glyphs are UI icons.

Icons are drawn here with simple geometry (all original), supersampled 4x and
downsampled, so they come out smooth and can be tinted by the skin's
viewport foreground colour like normal text.

Mapping (ASCII letters so skins can type them directly):
  A chevron   B play      C pause     D stop      E shuffle   F repeat
  G repeat1   H headphones I batt0    J batt1     K batt2     L batt3
  M batt4     N charging  O note      P ffwd      Q rewind    R speaker
  S sync(up)  T check     U dot
"""
import struct, sys, math
from PIL import Image, ImageDraw

SS = 4  # supersampling


def canvas(w, h):
    im = Image.new("L", (w * SS, h * SS), 0)
    return im, ImageDraw.Draw(im)


def finish(im, w, h):
    return im.resize((w, h), Image.LANCZOS)


def g_chevron(s):
    w = max(6, int(s * 0.42)); h = s
    im, d = canvas(w, h)
    S = SS; t = max(2, s // 10) * S
    x0, x1 = 1 * S, (w - 1.5) * S
    yc = h * S / 2; dy = s * 0.24 * S
    d.line([(x0, yc - dy), (x1, yc), (x0, yc + dy)], fill=255, width=t, joint="curve")
    return finish(im, w, h)


def g_play(s):
    w = int(s * 0.8); im, d = canvas(w, s); S = SS
    m = s * 0.18 * S
    d.polygon([(m, m), (w * S - m * 0.6, s * S / 2), (m, s * S - m)], fill=255)
    return finish(im, w, s)


def g_pause(s):
    w = int(s * 0.8); im, d = canvas(w, s); S = SS
    m = s * 0.2 * S; bw = s * 0.18 * S
    d.rounded_rectangle([m, m, m + bw, s * S - m], radius=bw / 4, fill=255)
    d.rounded_rectangle([w * S - m - bw, m, w * S - m, s * S - m], radius=bw / 4, fill=255)
    return finish(im, w, s)


def g_stop(s):
    w = int(s * 0.8); im, d = canvas(w, s); S = SS
    m = s * 0.24 * S
    d.rounded_rectangle([m * 0.8, m, w * S - m * 0.8, s * S - m], radius=s * 0.06 * S, fill=255)
    return finish(im, w, s)


def arrowhead(d, tip, direction, size):
    x, y = tip
    dx = direction
    d.polygon([(x, y), (x - dx * size, y - size * 0.75), (x - dx * size, y + size * 0.75)], fill=255)


def g_shuffle(s):
    w = int(s * 1.1); im, d = canvas(w, s); S = SS
    t = max(2, s // 11) * S
    l, r = s * 0.12 * S, w * S - s * 0.22 * S
    top, bot = s * 0.3 * S, s * 0.7 * S
    d.line([(l, bot), (l + (r - l) * 0.3, bot), (l + (r - l) * 0.7, top), (r, top)], fill=255, width=t, joint="curve")
    d.line([(l, top), (l + (r - l) * 0.3, top), (l + (r - l) * 0.7, bot), (r, bot)], fill=255, width=t, joint="curve")
    arrowhead(d, (w * S - s * 0.08 * S, top), 1, s * 0.16 * S)
    arrowhead(d, (w * S - s * 0.08 * S, bot), 1, s * 0.16 * S)
    return finish(im, w, s)


def g_repeat(s, one=False):
    w = int(s * 1.1); im, d = canvas(w, s); S = SS
    t = max(2, s // 11) * S
    l, r = s * 0.14 * S, w * S - s * 0.14 * S
    top, bot = s * 0.32 * S, s * 0.72 * S
    rad = (bot - top) / 2
    d.rounded_rectangle([l, top, r, bot], radius=rad, outline=255, width=t)
    # gap + arrow on the top run
    gx = (l + r) / 2
    d.rectangle([gx - s * 0.05 * S, top - t, gx + s * 0.12 * S, top + t], fill=0)
    arrowhead(d, (gx + s * 0.16 * S, top + t / 2 - S * 0.2), 1, s * 0.16 * S)
    if one:
        cx, cy = r - s * 0.02 * S, top - s * 0.02 * S
        rr = s * 0.17 * S
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=255)
        d.line([(cx, cy - rr * 0.55), (cx, cy + rr * 0.6)], fill=0, width=max(1, t // 2))
    return finish(im, w, s)


def g_headphones(s):
    w = s; im, d = canvas(w, s); S = SS
    t = max(2, s // 10) * S
    m = s * 0.14 * S
    d.arc([m, m, w * S - m, s * S - m * 0.2], 180, 360, fill=255, width=t)
    cw, ch = s * 0.2 * S, s * 0.34 * S
    y0 = s * 0.5 * S
    d.rounded_rectangle([m - t / 2, y0, m - t / 2 + cw, y0 + ch], radius=cw / 3, fill=255)
    d.rounded_rectangle([w * S - m + t / 2 - cw, y0, w * S - m + t / 2, y0 + ch], radius=cw / 3, fill=255)
    return finish(im, w, s)


def g_battery(s, level, charging=False):
    w = int(s * 1.25); im, d = canvas(w, s); S = SS
    t = max(1.5, s / 14) * S
    top, bot = s * 0.3 * S, s * 0.72 * S
    l, r = s * 0.06 * S, w * S - s * 0.16 * S
    d.rounded_rectangle([l, top, r, bot], radius=s * 0.08 * S, outline=255, width=int(t))
    d.rounded_rectangle([r + t * 0.6, top + (bot - top) * 0.3, w * S - s * 0.05 * S, bot - (bot - top) * 0.3],
                        radius=S, fill=255)
    if charging:
        cx = (l + r) / 2; cy = (top + bot) / 2; k = (bot - top) * 0.42
        d.polygon([(cx + k * 0.15, cy - k), (cx - k * 0.55, cy + k * 0.12), (cx - k * 0.02, cy + k * 0.12),
                   (cx - k * 0.2, cy + k), (cx + k * 0.6, cy - k * 0.15), (cx + k * 0.05, cy - k * 0.15)], fill=255)
    elif level > 0:
        p = t * 1.7
        fw = (r - l - 2 * p) * level / 4
        d.rounded_rectangle([l + p, top + p, l + p + fw, bot - p], radius=S, fill=255)
    return finish(im, w, s)


def g_note(s):
    w = s; im, d = canvas(w, s); S = SS
    t = max(2, s // 12) * S
    hx1, hy = s * 0.30 * S, s * 0.74 * S
    hx2 = s * 0.70 * S; hy2 = s * 0.64 * S
    rw, rh = s * 0.15 * S, s * 0.11 * S
    for (hx, yy) in ((hx1, hy), (hx2, hy2)):
        d.ellipse([hx - rw, yy - rh, hx + rw, yy + rh], fill=255)
    sx1 = hx1 + rw - t / 2; sx2 = hx2 + rw - t / 2
    d.line([(sx1, hy), (sx1, s * 0.24 * S)], fill=255, width=t)
    d.line([(sx2, hy2), (sx2, s * 0.14 * S)], fill=255, width=t)
    d.polygon([(sx1 - t / 2, s * 0.24 * S), (sx2 + t / 2, s * 0.14 * S),
               (sx2 + t / 2, s * 0.26 * S), (sx1 - t / 2, s * 0.36 * S)], fill=255)
    return finish(im, w, s)


def g_ff(s, rev=False):
    w = int(s * 1.0); im, d = canvas(w, s); S = SS
    m = s * 0.24 * S; half = w * S / 2
    for i in range(2):
        x0 = s * 0.08 * S + i * (half - s * 0.06 * S)
        pts = [(x0, m), (x0 + half - s * 0.04 * S, s * S / 2), (x0, s * S - m)]
        if rev:
            pts = [(w * S - p[0], p[1]) for p in pts]
        d.polygon(pts, fill=255)
    return finish(im, w, s)


def g_speaker(s):
    w = int(s * 1.0); im, d = canvas(w, s); S = SS
    t = max(2, s // 12) * S
    d.polygon([(s * 0.1 * S, s * 0.38 * S), (s * 0.28 * S, s * 0.38 * S), (s * 0.5 * S, s * 0.2 * S),
               (s * 0.5 * S, s * 0.8 * S), (s * 0.28 * S, s * 0.62 * S), (s * 0.1 * S, s * 0.62 * S)], fill=255)
    for r in (0.18, 0.32):
        c = (s * 0.5 * S, s * 0.5 * S); R = s * r * S
        d.arc([c[0] - R, c[1] - R, c[0] + R, c[1] + R], -50, 50, fill=255, width=t)
    return finish(im, w, s)


def g_sync(s):
    w = s; im, d = canvas(w, s); S = SS
    t = max(2, s // 11) * S
    cx = w * S / 2
    d.line([(cx, s * 0.78 * S), (cx, s * 0.3 * S)], fill=255, width=t)
    arrowhead(d, (cx, s * 0.16 * S), 0, 0)  # no-op keeps helper symmetric
    d.polygon([(cx, s * 0.16 * S), (cx - s * 0.2 * S, s * 0.38 * S), (cx + s * 0.2 * S, s * 0.38 * S)], fill=255)
    d.line([(s * 0.18 * S, s * 0.84 * S), (w * S - s * 0.18 * S, s * 0.84 * S)], fill=255, width=t)
    return finish(im, w, s)


def g_check(s):
    w = s; im, d = canvas(w, s); S = SS
    t = max(2, s // 9) * S
    d.line([(s * 0.18 * S, s * 0.52 * S), (s * 0.42 * S, s * 0.74 * S), (s * 0.84 * S, s * 0.28 * S)],
           fill=255, width=t, joint="curve")
    return finish(im, w, s)


def g_dot(s):
    w = int(s * 0.5); im, d = canvas(w, s); S = SS
    r = s * 0.12 * S; c = (w * S / 2, s * S / 2)
    d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=255)
    return finish(im, w, s)


GLYPHS = {
    'A': g_chevron, 'B': g_play, 'C': g_pause, 'D': g_stop, 'E': g_shuffle,
    'F': g_repeat, 'G': lambda s: g_repeat(s, True), 'H': g_headphones,
    'I': lambda s: g_battery(s, 0), 'J': lambda s: g_battery(s, 1),
    'K': lambda s: g_battery(s, 2), 'L': lambda s: g_battery(s, 3),
    'M': lambda s: g_battery(s, 4), 'N': lambda s: g_battery(s, 0, True),
    'O': g_note, 'P': g_ff, 'Q': lambda s: g_ff(s, True), 'R': g_speaker,
    'S': g_sync, 'T': g_check, 'U': g_dot,
}


def build(size, out):
    first, last = ord(' '), ord('U')
    height = size
    ascent = int(size * 0.8)
    bits = bytearray()
    offsets, widths = [], []
    space_w = max(3, size // 4)
    for code in range(first, last + 1):
        ch = chr(code)
        if ch in GLYPHS:
            g = GLYPHS[ch](size)
        else:
            g = Image.new("L", (space_w, size), 0)
        w, h = g.size
        assert h == height
        offsets.append(len(bits))
        widths.append(w)
        px = g.load()
        field, n = 0, 0
        for y in range(h):
            for x in range(w):
                v = 15 - ((px[x, y] + 8) // 17)  # Rockbox: 15 = no ink
                field |= (v & 0xF) << (4 * n)
                n += 1
                if n == 2:
                    bits.append(field); field, n = 0, 0
        if n:
            bits.append(field)
    long_off = len(bits) >= 0xFFDB
    hdr = b"RB12" + struct.pack("<hhhhiiiiii", max(widths), height, ascent, 1,
                                first, first, last - first + 1, len(bits),
                                last - first + 1, last - first + 1)
    with open(out, "wb") as f:
        f.write(hdr); f.write(bits)
        if long_off:
            f.write(b"\0" * (((len(bits) + 3) & ~3) - len(bits)))
            for o in offsets: f.write(struct.pack("<i", o))
        else:
            f.write(b"\0" * (((len(bits) + 1) & ~1) - len(bits)))
            for o in offsets: f.write(struct.pack("<H", o))
        f.write(bytes(widths))
    # preview sheet for design review
    sheet = Image.new("L", (sum(widths[ord(c) - first] + 6 for c in GLYPHS), height + 4), 255)
    x = 2
    for c in GLYPHS:
        g = GLYPHS[c](size)
        sheet.paste(Image.eval(g, lambda v: 255 - v), (x, 2)); x += g.size[0] + 6
    sheet.save(out.replace(".fnt", "_preview.png"))


if __name__ == "__main__":
    outdir = sys.argv[1]
    for s in (18, 22, 28, 34, 110):
        build(s, f"{outdir}/{s}-Wheel-Icons.fnt")
    print("ok")
