cmake_minimum_required(VERSION 3.22.1)
project(imageslogoscreator_android)

# Reached via app/build.gradle's externalNativeBuild.cmake.path. REPO_ROOT is
# two levels up from this file (android/ is a sibling of core/, gui/,
# framework/ at the repo root).
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(NATIVE_APP_GLUE ${ANDROID_NDK}/sources/android/native_app_glue)

# vk_canvas core: Vulkan canvas + text engine. Its own CMakeLists handles the
# Android branch (libvulkan, log).
add_subdirectory(${REPO_ROOT}/framework/vk_canvas/core ${CMAKE_BINARY_DIR}/vk_canvas_core)

# The logo engine (il_core) and the CLI twin's source live in core/. The CLI
# itself is desktop-only (unistd/proc/self/exe), so only the library builds.
set(ILC_SKIP_CLI ON)
add_subdirectory(${REPO_ROOT}/core ${CMAKE_BINARY_DIR}/il_core)
add_subdirectory(${REPO_ROOT}/textedit ${CMAKE_BINARY_DIR}/textedit)

# app_shell: the Host seam and the Android JNI plumbing.
set(APP_SHELL_APP_NAME "imageslogoscreator" CACHE STRING "" FORCE)
set(APP_SHELL_NATIVE_APP_GLUE_DIR ${NATIVE_APP_GLUE} CACHE PATH "" FORCE)
add_subdirectory(${REPO_ROOT}/framework/app_shell ${CMAKE_BINARY_DIR}/app_shell)

# The generated text-size floor ui_metrics.hh includes. For an outline-font
# app this is copied out of a desktop build tree (the generator cannot run
# under a cross-compiler); for this bitmap-font app the floor is a constant -
# the smallest Terminus strike - checked in at gui/src/, so there is nothing
# to generate.
set(UI_FLOOR_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${UI_FLOOR_DIR})
configure_file(${REPO_ROOT}/gui/src/ui_min_text_size.gen.h
               ${UI_FLOOR_DIR}/ui_min_text_size.gen.h COPYONLY)

# Shaders: the prebuilt .spv set compiled from these exact sources (same
# framework commit) ships at assets/shaders_prebuilt/. Cross-compiled slangc
# is not available to Gradle, so the APK gets the prebuilt binaries.
# Output goes to app/src/main/assets/shaders/ so AGP packages it into the APK.
set(ILC_ANDROID_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/assets)
file(MAKE_DIRECTORY ${ILC_ANDROID_ASSETS}/shaders)
file(COPY ${REPO_ROOT}/assets/shaders_prebuilt/ DESTINATION ${ILC_ANDROID_ASSETS}/shaders)

# Fonts: the same tree the desktop stages beside the exe, but inside the APK;
# AndroidAssetReader serves them through the Host::dataReader seam.
file(COPY ${REPO_ROOT}/assets/fonts/ DESTINATION ${ILC_ANDROID_ASSETS}/fonts)

# The app. logo_view.cc is the whole UI - no OS header in it - so the Android
# library compiles the SAME file the desktop exe does, plus this folder's
# android_main() and vk_canvas's Android platform seam implementations.
set(ILC_GUI_SRC ${REPO_ROOT}/gui/src)

add_library(imageslogoscreator SHARED
    src/main.cc
    ${ILC_GUI_SRC}/logo_view.cc
    ${REPO_ROOT}/framework/vk_canvas/platform/android/android_platform.cc
    ${NATIVE_APP_GLUE}/android_native_app_glue.c)

target_include_directories(imageslogoscreator PRIVATE
    ${NATIVE_APP_GLUE}
    ${REPO_ROOT}/framework/vk_canvas/platform/android
    ${ILC_GUI_SRC}
    ${REPO_ROOT}/core
    ${REPO_ROOT}/textedit
    ${UI_FLOOR_DIR})

target_link_libraries(imageslogoscreator
    app_shell
    app_shell_android
    android
    log
    il_core
    textedit
    vk_canvas_core
    z)

# Keep the glue's NativeActivity entry point alive against linker GC (same
# rule framework/vk_canvas/platform/android/CMakeLists.txt documents).
target_link_options(imageslogoscreator PRIVATE -u ANativeActivity_onCreate)
