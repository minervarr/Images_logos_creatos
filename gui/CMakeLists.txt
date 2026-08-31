# The Images Logos Creator GUI. Desktop reaches the Wayland host through
# app_shell_wayland; Android compiles the same logo_view.cc against
# app_shell_android instead (see android/CMakeLists.txt).
add_executable(imageslogoscreator
    src/main.cc
    src/logo_view.cc)

target_include_directories(imageslogoscreator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(imageslogoscreator PRIVATE
    il_core
    textedit
    app_shell
    app_shell_wayland
    vk_canvas_core
    vk_canvas_wayland)

add_dependencies(imageslogoscreator ${IL_SHADER_TARGETS})

# A build tree is one self-contained folder (app_paths' rule): the fonts, the
# shader SPIR-V and the bundle presets all sit beside the exe. The logo PNGs
# the app writes go to <exe dir>/pictures, exactly like the CLI's own rule.
# Dependency-driven (not POST_BUILD): a shader recompile must re-stage even
# when the executable itself doesn't relink.
add_custom_target(stage_app_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/fonts
        $<TARGET_FILE_DIR:imageslogoscreator>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IL_SHADER_DIR}
        $<TARGET_FILE_DIR:imageslogoscreator>/assets/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/bundles.json
        $<TARGET_FILE_DIR:imageslogoscreator>/bundles.json)
add_dependencies(stage_app_assets ${IL_SHADER_TARGETS})

# Headless screenshots of the real UI, Debug only (see tools/ui_capture).
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(ilc_ui_capture
        ${CMAKE_SOURCE_DIR}/tools/ui_capture/main.cc
        src/logo_view.cc)
    target_include_directories(ilc_ui_capture PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tools/ui_capture)
    target_link_libraries(ilc_ui_capture PRIVATE
        il_core textedit app_shell vk_canvas_core vk_canvas_wayland)
    # The headless run borrows the Wayland backend's exe-relative
    # FileAssetReader without its window machinery.
    target_include_directories(ilc_ui_capture PRIVATE
        ${CMAKE_SOURCE_DIR}/framework/vk_canvas/platform/linux
        ${CMAKE_SOURCE_DIR}/framework/vk_canvas/core/capture)
    add_dependencies(ilc_ui_capture ${IL_SHADER_TARGETS})
    add_custom_target(stage_capture_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets/fonts
            $<TARGET_FILE_DIR:ilc_ui_capture>/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${IL_SHADER_DIR}
            $<TARGET_FILE_DIR:ilc_ui_capture>/assets/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/bundles.json
            $<TARGET_FILE_DIR:ilc_ui_capture>/bundles.json)
    add_dependencies(stage_capture_assets ${IL_SHADER_TARGETS})

    # Renderer isolation probe: if the trivial clear+rect is wrong here but
    # right on a known-good machine, the capture environment is the suspect.
    add_executable(ilc_capture_probe ${CMAKE_SOURCE_DIR}/tools/ui_capture/probe.cc)
    target_include_directories(ilc_capture_probe PRIVATE
        ${CMAKE_SOURCE_DIR}/framework/vk_canvas/core/capture)
    # capture_main's runner, compiled in rather than linked: vk_canvas does
    # not add_subdirectory its own capture/ target for consumers.
    target_sources(ilc_capture_probe PRIVATE
        ${CMAKE_SOURCE_DIR}/framework/vk_canvas/core/capture/capture.cc)
    target_link_libraries(ilc_capture_probe PRIVATE vk_canvas_wayland)
endif()
