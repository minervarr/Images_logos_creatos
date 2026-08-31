#!/bin/sh
# The clangd trick: configure ONCE with the Android NDK legacy toolchain purely
# to emit compile_commands.json, then copy it to the repo root. clangd reads
# that and lights up with cross-compilation IntelliSense; whether the rest of
# the configure (or any later build) succeeds is irrelevant to the editor.
#
#   scripts/clangd.sh                      # NDK arm64-v8a (default)
#   scripts/clangd.sh --desktop            # plain native configure instead
#
# Restart clangd in your editor after regenerating.
set -e
cd "$(dirname "$0")/.."

NDK="${ANDROID_NDK_HOME:-/opt/android-sdk/ndk/29.0.14206865}"

if [ "$1" = "--desktop" ] || [ ! -d "$NDK" ]; then
    echo "clangd: desktop configure -> build/linux"
    cmake -S . -B build/linux -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cp build/linux/compile_commands.json compile_commands.json
    exit 0
fi

echo "clangd: NDK configure (compile db only) -> build/clangd"
# Configure-only: the goal is the JSON, not a build. android-26 matches the
# app's minSdk; arm64-v8a is the ABI phones actually run.
cmake -S android -B build/clangd -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android-legacy.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    >/dev/null 2>&1 || \
    echo "note: configure ended with errors - the JSON is still written; that is all clangd needs"

if [ -f build/clangd/compile_commands.json ]; then
    cp build/clangd/compile_commands.json compile_commands.json
    echo "clangd: compile_commands.json -> repo root; restart clangd now"
else
    echo "clangd: no JSON emitted - check the NDK path ($NDK)"
    exit 1
fi
