#!/bin/bash
# The fonts of the stock Wheel look (= the "Apple Music" preset with Inter),
# made with the same forge the player uses. Output: fonts/prebuilt/.
#   needs: gcc; fonts/typo/ (build_fonts.py)
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
T=$ROOT/fonts/typo
OUT=${OUT:-$ROOT/fonts/prebuilt}
F=${FORGE:-/tmp/wheel-forge}
gcc -O2 -I "$ROOT/apps/wheel" -o "$F" "$ROOT/utils/wheel/typography/forge_cli.c" "$ROOT/apps/wheel/wheel_fontforge.c"
k="-k 1"
$F $k -p 18 -b 32              "$T/Inter-700.ttf" "$OUT/Wheel-Title.fnt"
$F $k -p 23 -b 32              "$T/Inter-700.ttf" "$OUT/Wheel-Display.fnt"
$F $k -p 20 -b 40 -r           "$T/Inter-500.ttf" "$OUT/40-Wheel-Row.fnt"
$F $k -p 20 -b 40 -r           "$T/Inter-600.ttf" "$OUT/40-Wheel-RowSel.fnt"
$F $k -p 18 -b 26              "$T/Inter-500.ttf" "$OUT/Wheel-UI.fnt"
$F $k -p 18 -b 26              "$T/Inter-500.ttf" "$OUT/Wheel-Secondary.fnt"
$F $k -p 15 -b 22 -T           "$T/Inter-400.ttf" "$OUT/Wheel-Meta.fnt"
$F $k -p 15 -b 22 -T           "$T/Inter-500.ttf" "$OUT/Wheel-Numeric.fnt"
$F $k -p 26 -b 36              "$T/Inter-700.ttf" "$OUT/Wheel-Lyrics.fnt"
$F $k -p 52 -b 65 -T -l 0x7e   "$T/Inter-700.ttf" "$OUT/64-Wheel-Letters.fnt"
