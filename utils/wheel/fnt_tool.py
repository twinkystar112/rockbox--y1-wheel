"""Read/patch/write Rockbox RB12 fonts (4-bit anti-aliased, as made by convttf).

Used to give the list fonts a proper chevron: the glyph for U+203A (›) is
replaced by a drawn chevron sized and centred for the row height.
"""
import struct
from PIL import Image, ImageDraw

HDR = "<hhhhiiiiii"


class Font:
    def __init__(self, path):
        d = open(path, "rb").read()
        assert d[:4] == b"RB12", path
        (self.maxwidth, self.height, self.ascent, self.depth, self.first,
         self.default, self.size, nbits, noffset, nwidth) = struct.unpack(HDR, d[4:36])
        assert self.depth == 1, "only 4-bit fonts supported"
        p = 36
        bits = d[p:p + nbits]; p += nbits
        long_off = nbits >= 0xFFDB
        if long_off:
            p = 36 + ((nbits + 3) & ~3)
            offs = list(struct.unpack("<%di" % noffset, d[p:p + 4 * noffset])); p += 4 * noffset
        else:
            p = 36 + ((nbits + 1) & ~1)
            offs = list(struct.unpack("<%dH" % noffset, d[p:p + 2 * noffset])); p += 2 * noffset
        widths = list(d[p:p + nwidth])
        self.glyphs = []  # per code: (width, bytes)
        for i in range(self.size):
            w = widths[i]
            n = (w * self.height + 1) // 2
            self.glyphs.append((w, bytes(bits[offs[i]:offs[i] + n])))

    def set_glyph(self, code, img):
        """img: PIL 'L' image, 255 = ink, height must equal font height"""
        w, h = img.size
        assert h == self.height
        px = img.load()
        out = bytearray(); field = 0; n = 0
        for y in range(h):
            for x in range(w):
                v = 15 - ((px[x, y] + 8) // 17)
                field |= (v & 15) << (4 * n); n += 1
                if n == 2:
                    out.append(field); field = 0; n = 0
        if n:
            out.append(field)
        self.glyphs[code - self.first] = (w, bytes(out))
        self.maxwidth = max(self.maxwidth, w)

    def save(self, path):
        bits = bytearray(); offs = []; widths = []
        cache = {}
        for (w, data) in self.glyphs:
            key = (w, data)
            if key not in cache:
                cache[key] = len(bits); bits += data
            offs.append(cache[key]); widths.append(w)
        nbits = len(bits)
        long_off = nbits >= 0xFFDB
        with open(path, "wb") as f:
            f.write(b"RB12" + struct.pack(HDR, self.maxwidth, self.height, self.ascent, 1,
                                          self.first, self.default, self.size, nbits,
                                          self.size, self.size))
            f.write(bits)
            if long_off:
                f.write(b"\0" * (((nbits + 3) & ~3) - nbits))
                f.write(struct.pack("<%di" % len(offs), *offs))
            else:
                f.write(b"\0" * (((nbits + 1) & ~1) - nbits))
                f.write(struct.pack("<%dH" % len(offs), *offs))
            f.write(bytes(widths))


def chevron(height, center_y, size, stroke, pad_left=4, pad_right=2):
    SS = 4
    w = int(size * 0.55) + pad_left + pad_right + 2
    im = Image.new("L", (w * SS, height * SS), 0)
    d = ImageDraw.Draw(im)
    x0 = (pad_left + 0.5) * SS
    x1 = x0 + size * 0.5 * SS
    yc = center_y * SS
    dy = size * 0.5 * SS
    d.line([(x0, yc - dy), (x1, yc), (x0, yc + dy)], fill=255, width=int(stroke * SS), joint="curve")
    # round caps
    r = stroke * SS / 2
    for (cx, cy) in ((x0, yc - dy), (x0, yc + dy)):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    return im.resize((w, height), Image.LANCZOS)


def check(height, center_y, size, stroke, pad_left=3, pad_right=3):
    """a drawn check mark (U+2713) for fonts whose source lacks one"""
    SS = 4
    w = int(size * 1.05) + pad_left + pad_right
    im = Image.new("L", (w * SS, height * SS), 0)
    d = ImageDraw.Draw(im)
    x0 = (pad_left + 0.5) * SS
    yc = center_y * SS
    pts = [(x0, yc), (x0 + size * 0.36 * SS, yc + size * 0.36 * SS),
           (x0 + size * 0.98 * SS, yc - size * 0.42 * SS)]
    d.line(pts, fill=255, width=int(stroke * SS), joint="curve")
    r = stroke * SS / 2
    for (cx, cy) in (pts[0], pts[2]):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    return im.resize((w, height), Image.LANCZOS)


if __name__ == "__main__":
    import sys
    src, dst = sys.argv[1], sys.argv[2]
    f = Font(src)
    # centre of lower-case x-height-ish band: ~ascent - 0.36 * glyph size
    cap = f.ascent - (f.height - f.ascent)  # rough
    cy = f.ascent - 0.36 * (f.ascent - 8)
    f.set_glyph(0x203A, chevron(f.height, cy, size=12, stroke=2.4))
    f.save(dst)
    print("patched", dst, f.height, f.ascent)
