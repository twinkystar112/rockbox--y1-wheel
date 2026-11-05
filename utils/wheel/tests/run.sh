#!/bin/bash
# Runs the Java helper tests (Last.fm, lyrics, Bluetooth send) on a PC.
# Needs: JDK 8+, Python 3, and org.json sources (JSON_JAVA=path to
# https://github.com/stleary/JSON-java/src/main/java).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
JSON_JAVA=${JSON_JAVA:-$HERE/JSON-java/src/main/java}
if [ ! -d "$JSON_JAVA/org/json" ]; then
  git clone --depth 1 https://github.com/stleary/JSON-java.git "$HERE/JSON-java" >/dev/null 2>&1
fi
OUT=$(mktemp -d)
SD=$(mktemp -d)
H="$ROOT/android/y1/src/org/rockbox/Helper"
javac -nowarn -encoding UTF-8 -d "$OUT/classes" \
  $(find "$HERE/fakes" -name '*.java') \
  $(find "$JSON_JAVA/org/json" -name '*.java') \
  "$H/WheelScrobbler.java" "$H/WheelLyrics.java" "$H/WheelSend.java" "$H/WheelNet.java" "$H/SonoraNet.java" "$H/Logger.java" \
  "$HERE/org/rockbox/Helper/WheelTests.java" 2>&1 | grep -v "^Note:" || true
HTTP_PORT=${HTTP_PORT:-18431}; OBEX_PORT=${OBEX_PORT:-18432}
python3 "$HERE/mock_services.py" $HTTP_PORT $OBEX_PORT "$OUT/received" &
MOCK=$!
trap "kill $MOCK 2>/dev/null; rm -rf '$OUT' '$SD'" EXIT
sleep 1
java -Dstdout.encoding=UTF-8 -Dwheel.sdcard="$SD" -Dwheel.timescale=0.02 -cp "$OUT/classes" org.rockbox.Helper.WheelTests $HTTP_PORT $OBEX_PORT
