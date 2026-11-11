# piccolo: basic libretro frontend built on host-libretro.
# So far: SDL3 window + Vulkan swapchain + Dear ImGui debug UI (from frontend_common).

add_executable(piccolo
    src/main.cpp
)

target_link_libraries(piccolo PRIVATE frontend_common)

multitap_set_project_flags(piccolo)
