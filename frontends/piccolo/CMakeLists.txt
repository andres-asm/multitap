# piccolo: small test frontend for host-libretro.
# For now: SDL3 window + Vulkan swapchain + Dear ImGui debug UI.

add_executable(piccolo
    src/main.cpp
    src/renderer.cpp
    src/vk_context.cpp
    src/vk_swapchain.cpp
    src/debug_ui.cpp
)

target_link_libraries(piccolo PRIVATE imgui SDL3::SDL3 Vulkan::Vulkan)

# Vulkan validation layers + debug messenger are enabled in Debug builds only.
target_compile_definitions(piccolo PRIVATE $<$<CONFIG:Debug>:PICCOLO_VULKAN_VALIDATION=1>)

multitap_set_project_flags(piccolo)
