# frontend_libretro: what every libretro frontend shares on top of
# host-libretro and frontend_common: the core session (run a core in a window:
# video texture, audio, timing, input, states), keyboard and fixed controls,
# and the catalog of installed cores. Used by piccolo and the host-libretro
# test frontend; each adds only its own UI.

add_library(frontend_libretro STATIC
    core_session.cpp
    core_catalog.cpp
    keyboard_input.cpp
    fixed_input.cpp
)

target_include_directories(frontend_libretro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frontend_libretro PUBLIC host_libretro frontend_common)

multitap_set_project_flags(frontend_libretro)
