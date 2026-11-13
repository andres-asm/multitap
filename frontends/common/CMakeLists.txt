# frontend_common: code shared by every frontend (piccolo, the host-libretro
# test frontend, later Multitap): Vulkan instance/device, swapchain, frame
# rendering, Dear ImGui glue, settings. Plain static library, no frontend-specific
# code. Links host_common for its file helpers (host_fs: atomic writes); the
# other direction (hosts using frontend or SDL code) is what's not allowed.

add_library(frontend_common STATIC
    vk_context.cpp
    vk_swapchain.cpp
    renderer.cpp
    debug_ui.cpp
    vk_texture.cpp
    app_shell.cpp
    audio_output.cpp
    frame_timing.cpp
    settings_file.cpp
    settings.cpp
    app_paths.cpp
)

target_include_directories(frontend_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frontend_common PUBLIC imgui SDL3::SDL3 Vulkan::Vulkan host_common)

# Vulkan validation layers + debug messenger are enabled in Debug builds only.
target_compile_definitions(frontend_common PRIVATE $<$<CONFIG:Debug>:MULTITAP_VULKAN_VALIDATION=1>)

multitap_set_project_flags(frontend_common)

if(MULTITAP_BUILD_TESTS)
    add_executable(frontend_settings_file_test tests/settings_file_test.cpp)
    target_link_libraries(frontend_settings_file_test PRIVATE frontend_common)
    multitap_set_project_flags(frontend_settings_file_test)

    add_test(NAME frontend_settings_file
             COMMAND frontend_settings_file_test "${CMAKE_CURRENT_BINARY_DIR}/settings_file_test")
    set_tests_properties(frontend_settings_file PROPERTIES
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")

    add_executable(frontend_settings_test tests/settings_test.cpp)
    target_link_libraries(frontend_settings_test PRIVATE frontend_common)
    multitap_set_project_flags(frontend_settings_test)

    add_test(NAME frontend_settings
             COMMAND frontend_settings_test "${CMAKE_CURRENT_BINARY_DIR}/settings_test")
    set_tests_properties(frontend_settings PROPERTIES
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")

    # The migration check needs a scratch folder that doesn't exist yet: a
    # fixture empties it first.
    add_executable(frontend_app_paths_test tests/app_paths_test.cpp)
    target_link_libraries(frontend_app_paths_test PRIVATE frontend_common)
    multitap_set_project_flags(frontend_app_paths_test)

    set(APP_PATHS_SCRATCH "${CMAKE_CURRENT_BINARY_DIR}/app_paths_test")
    add_test(NAME frontend_app_paths_clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${APP_PATHS_SCRATCH}")
    set_tests_properties(frontend_app_paths_clean PROPERTIES FIXTURES_SETUP app_paths_scratch)
    add_test(NAME frontend_app_paths COMMAND frontend_app_paths_test "${APP_PATHS_SCRATCH}")
    set_tests_properties(frontend_app_paths PROPERTIES
                         FIXTURES_REQUIRED app_paths_scratch
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")
endif()
