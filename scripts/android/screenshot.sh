#!/bin/sh
# Windowed screenshot of the app on a device (any build):
#   scripts/android/screenshot.sh [SERIAL] [OUT.png]
# Screenshots whatever is on screen while the app is running (adb's screencap
# talks to the compositor, not to our renderer - works for the normal UI).
set -e
cd "$(dirname "$0")/../.."

SERIAL="$1"
OUT="${2:-screenshot.png}"
ADB="adb"
[ -n "$SERIAL" ] && ADB="adb -s $SERIAL"

$ADB exec-out screencap -p > "$OUT"
echo "saved $OUT"
