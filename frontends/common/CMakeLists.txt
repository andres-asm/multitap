# frontend_common: code shared by every frontend (piccolo, the host-libretro
# test frontend, later Multitap): Vulkan instance/device, swapchain, frame
# rendering, Dear ImGui glue. Plain static library, no frontend-specific code.

add_library(frontend_common STATIC
    vk_context.cpp
    vk_swapchain.cpp
    renderer.cpp
    debug_ui.cpp
    vk_texture.cpp
    app_shell.cpp
    audio_output.cpp
)

target_include_directories(frontend_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frontend_common PUBLIC imgui SDL3::SDL3 Vulkan::Vulkan)

# Vulkan validation layers + debug messenger are enabled in Debug builds only.
target_compile_definitions(frontend_common PRIVATE $<$<CONFIG:Debug>:MULTITAP_VULKAN_VALIDATION=1>)

multitap_set_project_flags(frontend_common)
