add_library(textedit STATIC
    text_buffer.cc
    text_field.cc)
target_include_directories(textedit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(textedit PUBLIC vk_canvas_core)
