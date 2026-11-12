# piccolo: basic libretro frontend built on host-libretro.
# So far: SDL3 window + Vulkan swapchain + Dear ImGui debug UI (from frontend_common),
# and a test of the retro-style UI (themed ImGui widgets with piccolo's pixel font).

# piccolo's own pixel font (5x5, drawn for piccolo; see src/pixel_font.h).
# A small library so the preview tool and, later, piccolo's UI share it.
add_library(piccolo_font STATIC
    src/pixel_font.cpp
)
target_include_directories(piccolo_font PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
multitap_set_project_flags(piccolo_font)

add_executable(piccolo
    src/main.cpp
    src/menus.cpp
    src/retro_ui.cpp
    src/pixel_font_imgui.cpp
)

target_link_libraries(piccolo PRIVATE frontend_common piccolo_font)

multitap_set_project_flags(piccolo)

# Development tool: writes a PNG of every glyph + sample text, to judge the font by eye.
add_executable(pixel_font_preview tools/pixel_font_preview.cpp)
target_link_libraries(pixel_font_preview PRIVATE piccolo_font)
multitap_set_project_flags(pixel_font_preview)

if(MULTITAP_BUILD_TESTS)
    # Table well formed, no character defined twice, no two glyphs identical.
    add_test(NAME piccolo_pixel_font COMMAND pixel_font_preview --check)
    set_tests_properties(piccolo_pixel_font PROPERTIES PASS_REGULAR_EXPRESSION "table OK")
endif()
