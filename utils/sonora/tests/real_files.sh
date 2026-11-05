#!/bin/bash
# Optional: check the chapter reader against files made by a real tool.
# Needs ffmpeg/ffprobe. Every file is generated here (tones, original names).
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SRC="$HERE/../../../apps/sonora"
T=$(mktemp -d); trap "rm -rf $T" EXIT
gcc -std=gnu11 -O1 -DSONORA_HOST_TEST -I"$SRC" -o "$T/chdump" "$HERE/chdump.c" "$SRC/sonora_util.c" "$SRC/sonora_chapters.c"
cat > "$T/ch.txt" <<'M'
;FFMETADATA1
title=Test Book
[CHAPTER]
TIMEBASE=1/1000
START=0
END=20000
title=One
[CHAPTER]
TIMEBASE=1/1000
START=20000
END=45000
title=Two
[CHAPTER]
TIMEBASE=1/1000
START=45000
END=60000
title=Three
M
q() { ffmpeg -hide_banner -loglevel error -y "$@"; }
q -f lavfi -i sine=duration=60 -i "$T/ch.txt" -map 0 -map_chapters 1 -c:a aac "$T/book.m4b"
q -i "$T/book.m4b" -c copy -movflags +faststart "$T/fast.m4b"
q -f lavfi -i sine=duration=60 -i "$T/ch.txt" -map 0 -map_chapters 1 -c:a libmp3lame "$T/book.mp3"
fail=0
for f in book.m4b fast.m4b book.mp3; do
  out=$("$T/chdump" "$T/$f")
  echo "$out"
  echo "$out" | grep -q "3 chapters" && echo "$out" | grep -q "45000 ms  Three" || { echo "FAIL $f"; fail=1; }
done
exit $fail
