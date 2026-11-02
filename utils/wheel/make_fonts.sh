#!/bin/bash
# Rebuild the Wheel theme fonts into fonts/prebuilt/ (icons, Inter sizes
# used when scaling other themes, and the stock typography fonts).
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
# list rows, titles, details, numbers, lyrics, letter jump: the stock look of
# the typography system, made by the same forge the player uses
"$(dirname "$0")/typography/make_stock_fonts.sh"
cd "$(dirname "$0")"
python3 make_iconfont.py "$OUT"
rm -f "$OUT"/*_preview.png
echo "fonts written to $OUT"
