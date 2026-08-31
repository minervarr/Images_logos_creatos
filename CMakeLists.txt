cmake_minimum_required(VERSION 3.22.1)
project(imageslogoscreator)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Whole-program optimization for Release (see Matrix_Player's root file for
# the reasoning; the logo rasterizer is pure CPU math and benefits the same).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Byte-parity contract with glyphpng.py: a fused a*b+c changes rasterized
# coverage by an ulp. il_core sets this per-target too; stated globally so the
# GUI's own math (layout, preview scaling) agrees with the CLI's.
if(NOT MSVC)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-ffp-contract=off" IL_HAVE_FP_CONTRACT_OFF)
    if(IL_HAVE_FP_CONTRACT_OFF)
        string(APPEND CMAKE_CXX_FLAGS " -ffp-contract=off")
        string(APPEND CMAKE_C_FLAGS " -ffp-contract=off")
    endif()
endif()

# ---------- ZLIB (PNG IDAT for il_core) ----------
find_package(ZLIB REQUIRED)

# ---------- vk_canvas (Vulkan 2D canvas engine, framework/ submodule) ----------
add_subdirectory(framework/vk_canvas/core vk_canvas_build)
if(NOT WIN32 AND NOT ANDROID)
    add_subdirectory(framework/vk_canvas/platform/linux vk_canvas_linux_build)
endif()

# ---------- app_shell (Host seam; framework/ submodule) ----------
set(APP_SHELL_APP_NAME  "imageslogoscreator" CACHE STRING "" FORCE)
set(APP_SHELL_WIN_CLASS "ImagesLogosCreatorMain" CACHE STRING "" FORCE)
set(APP_SHELL_WINDOW_TITLE "Images Logos Creator" CACHE STRING "" FORCE)
set(APP_SHELL_APP_ID "imageslogoscreator" CACHE STRING "" FORCE)
set(APP_SHELL_STATE_HOME "" CACHE STRING "" FORCE)
# app_shell's ui_metrics includes ui_min_text_size.gen.h from
# ${CMAKE_BINARY_DIR}/generated; our floor is static (bitmap font: the
# smallest strike), so a plain copy replaces the outline-font generator.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
configure_file(gui/src/ui_min_text_size.gen.h
               ${CMAKE_BINARY_DIR}/generated/ui_min_text_size.gen.h COPYONLY)
# The target name app_shell's Debug tests expect from the outline-font
# generator; ours is a static copy, so the "generation" is the copy above.
add_custom_target(generate_ui_min_text_size
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/gui/src/ui_min_text_size.gen.h
        ${CMAKE_BINARY_DIR}/generated/ui_min_text_size.gen.h)
add_subdirectory(framework/app_shell app_shell_build)

# ---------- il_core (the logo engine, shared by the CLI twin and the GUI) ----
add_subdirectory(core)

# ---------- textedit (single-line text field library) ----------
add_subdirectory(textedit)

# ---------- shaders (SPIR-V next to the exe; slangc when present) ----------
# slangc comes from the Vulkan SDK. Without it we fall back to the prebuilt
# .spv checked in under assets/shaders_prebuilt/ — compiled from these exact
# shader sources when the framework was at this same commit, so the binaries
# are what the engine expects. Re-run with slangc installed to regenerate.
include(framework/vk_canvas/cmake/VceShaders.cmake)
set(IL_SHADER_DIR ${CMAKE_BINARY_DIR}/assets/shaders)
find_program(VCE_SLANGC slangc)
# A Windows slangc.exe (an SDK mounted from elsewhere, or under Wine) is not
# a working host compiler here; fall through to the prebuilt .spv instead.
if(VCE_SLANGC AND NOT (NOT WIN32 AND VCE_SLANGC MATCHES "\\.exe$"))
    message(STATUS "slangc found: ${VCE_SLANGC} - compiling shaders")
    vce_compile_slang(compile_shared_shaders
        ${IL_SHADER_DIR}
        ${CMAKE_SOURCE_DIR}/framework/vk_canvas/first_party/vulkan_font_engine/shaders_src
        composite_vert composite_frag tiling coverage msdf_vert msdf_frag
        glyph_area glyph_resolve)
    vce_compile_slang(compile_vk_canvas_shaders
        ${IL_SHADER_DIR}
        ${CMAKE_SOURCE_DIR}/framework/vk_canvas/shaders_src
        overlay_vert overlay_frag image_vert image_frag shape_vert shape_frag)
    set(IL_SHADER_TARGETS compile_shared_shaders compile_vk_canvas_shaders)
else()
    if(VCE_SLANGC)
        message(STATUS "slangc at ${VCE_SLANGC} is a Windows binary - using prebuilt .spv")
    else()
        message(STATUS "slangc NOT found - using prebuilt .spv from assets/shaders_prebuilt/")
    endif()
    add_custom_target(copy_prebuilt_shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets/shaders_prebuilt ${IL_SHADER_DIR})
    set(IL_SHADER_TARGETS copy_prebuilt_shaders)
endif()

# ---------- the GUI app ----------
add_subdirectory(gui)

# ---------- Debug-only tooling ----------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(textedit_test textedit/tests/textedit_test.cc)
    target_include_directories(textedit_test PRIVATE ${CMAKE_SOURCE_DIR}/textedit)
    target_link_libraries(textedit_test PRIVATE textedit)
endif()
