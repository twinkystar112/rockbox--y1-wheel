#!/bin/bash
# Rebuild the Wheel theme fonts into fonts/prebuilt/.
# Needs: convttf  (gcc -O2 -o tools/convttf tools/convttf.c $(pkg-config --cflags --libs freetype2) -lm),
#        Python 3 + Pillow, Inter (SIL OFL) .otf files.
set -e
INTER=${INTER:-/usr/share/fonts/opentype/inter}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-$ROOT/fonts/prebuilt}
C=${CONVTTF:-$ROOT/tools/convttf}
mkdir -p "$OUT"
for spec in "18 Regular" "18 Medium" "20 Medium" "22 Medium" "22 SemiBold" "24 SemiBold" "28 Bold"; do
  set -- $spec
  $C -p $1 -l 8482 -o "$OUT/$1-Inter-$2.fnt" "$INTER/Inter-$2.otf" >/dev/null
done
# 40px list-row fonts (text centred in the row) + drawn chevron at U+203A
$C -p 24 -Ta -8 -Td -7 -l 10003 -o /tmp/row-med.fnt  "$INTER/Inter-Medium.otf"   >/dev/null
$C -p 24 -Ta -8 -Td -7 -l 10003 -o /tmp/row-semi.fnt "$INTER/Inter-SemiBold.otf" >/dev/null
cd "$(dirname "$0")"
python3 -c "
import sys
from fnt_tool import Font, chevron, check
for src, dst in (('/tmp/row-med.fnt','$OUT/40-Wheel-Row.fnt'),('/tmp/row-semi.fnt','$OUT/40-Wheel-RowSel.fnt')):
    f = Font(src); f.set_glyph(0x203A, chevron(f.height, center_y=f.ascent-8.5, size=13, stroke=2.6))
    f.set_glyph(0x2713, check(f.height, center_y=f.ascent-8.5, size=13, stroke=2.6)); f.save(dst)
"
$C -p 64 -s 35 -l 90 -o "$OUT/64-Wheel-Letters.fnt" "$INTER/Inter-SemiBold.otf" >/dev/null
python3 make_iconfont.py "$OUT"
rm -f "$OUT"/*_preview.png
echo "fonts written to $OUT"
