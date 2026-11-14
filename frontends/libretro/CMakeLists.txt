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
    core_options_store.cpp
    core_options_cache.cpp
)

target_include_directories(frontend_libretro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frontend_libretro PUBLIC host_libretro frontend_common)

multitap_set_project_flags(frontend_libretro)

if(MULTITAP_BUILD_TESTS)
    add_executable(frontend_core_options_store_test tests/core_options_store_test.cpp)
    target_link_libraries(frontend_core_options_store_test PRIVATE frontend_libretro)
    multitap_set_project_flags(frontend_core_options_store_test)
    add_test(NAME frontend_core_options_store COMMAND frontend_core_options_store_test)
    set_tests_properties(frontend_core_options_store PROPERTIES PASS_REGULAR_EXPRESSION "core options store: all checks passed")

    add_executable(frontend_core_options_cache_test tests/core_options_cache_test.cpp)
    target_link_libraries(frontend_core_options_cache_test PRIVATE frontend_libretro)
    multitap_set_project_flags(frontend_core_options_cache_test)
    add_test(NAME frontend_core_options_cache COMMAND frontend_core_options_cache_test)
    set_tests_properties(frontend_core_options_cache PROPERTIES PASS_REGULAR_EXPRESSION "core options cache: all checks passed")
endif()
