#!/bin/sh
# Headless UI capture on a device (debug APK only):
#   scripts/android/capture.sh [SERIAL]
# Launches the app with the il_capture intent extra; the app renders its UI
# states off-screen (present suppressed), writes PNGs to its external files
# dir and exits. The PNGs are pulled into ./ui-shots/.
#
# Windowed screenshots instead:
#   scripts/android/screenshot.sh [SERIAL]
set -e
cd "$(dirname "$0")/../.."

SERIAL="$1"
ADB="adb"
[ -n "$SERIAL" ] && ADB="adb -s $SERIAL"

PKG=io.nava.imageslogoscreator
ACTIVITY=$PKG/.MainActivity

$ADB logcat -c || true
$ADB shell am start -n "$ACTIVITY" --es il_capture capture

# The capture runs, writes summary.txt and finishes the activity itself.
sleep 6
$ADB logcat -d -s ILCMain:V imageslogoscreator:V | tail -20

mkdir -p ui-shots
$ADB pull "/storage/emulated/0/Android/data/$PKG/files/ui-shots/." ui-shots/ 2>/dev/null || \
    $ADB pull "/sdcard/Android/data/$PKG/files/ui-shots/." ui-shots/

echo
echo "done - see ui-shots/"
