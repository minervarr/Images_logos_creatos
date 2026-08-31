# il_core - the logo engine, extracted verbatim from the glyphpng.cpp twin.
# The GUI links exactly the code that makes the CLI's PNGs (see AGENTS.md).
add_library(il_core STATIC glyphpng_core.cpp)
target_include_directories(il_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ZLIB for the PNG IDAT stream. The NDK ships zlib without a CMake config,
# so Android links the plain library name.
if(ANDROID)
  target_link_libraries(il_core PUBLIC z)
else()
  find_package(ZLIB REQUIRED)
  target_link_libraries(il_core PUBLIC ZLIB::ZLIB)
endif()

# -ffp-contract=off is part of the byte-parity contract with glyphpng.py
# (AGENTS.md): a fused a*b+c would change rasterized coverage by an ulp.
if(NOT MSVC)
  target_compile_options(il_core PRIVATE -ffp-contract=off)
endif()

# The CLI twin, also built here so one desktop configure covers everything.
# Android skips it (its interactive editor/menus assume a desktop shell).
if(NOT ILC_SKIP_CLI)
  add_executable(glyphpng_cli ${CMAKE_SOURCE_DIR}/glyphpng.cpp)
  target_link_libraries(glyphpng_cli PRIVATE il_core)
  set_target_properties(glyphpng_cli PROPERTIES OUTPUT_NAME glyphpng)
endif()
