#!/bin/sh
# Desktop (Linux/Wayland) build for ImagesLogosCreator.
#   scripts/linux/build.sh            Release -> build/linux
#   scripts/linux/build.sh --debug    Debug   -> build/linux_debug (adds
#                                     ilc_ui_capture + textedit_test)
# The exe lands in build/<tree>/gui/ with fonts, shaders and bundles.json
# beside it; saved logos go to build/<tree>/gui/pictures/.
set -e
cd "$(dirname "$0")/../.."

if [ "$1" = "--debug" ]; then
    BUILD=build/linux_debug
    TYPE=Debug
else
    BUILD=build/linux
    TYPE=Release
fi

cmake -S . -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD"

echo
echo "GUI:       $BUILD/gui/imageslogoscreator"
echo "CLI twin:  $BUILD/core/glyphpng"
if [ "$TYPE" = "Debug" ]; then
    echo "Capture:   $BUILD/gui/ilc_ui_capture --out ui-shots"
    echo "Tests:     $BUILD/textedit_test"
fi
