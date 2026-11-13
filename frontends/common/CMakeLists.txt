# frontend_common: code shared by every frontend (piccolo, the host-libretro
# test frontend, later Multitap): Vulkan instance/device, swapchain, frame
# rendering, Dear ImGui glue, settings. Plain static library, no frontend-specific
# code. Links host_common for its file helpers (host_fs: atomic writes); the
# other direction (hosts using frontend or SDL code) is what's not allowed.

# libsamplerate (deps/, BSD-2-Clause): audio_output's resampler, chosen by
# measurement (tools/resampler_measure.cpp). Built from its sources without
# our warning flags; it wants a generated config.h: ours, in the build folder.
set(LIBSAMPLERATE_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/libsamplerate_config")
file(WRITE "${LIBSAMPLERATE_CONFIG}/config.h"
     "#define PACKAGE \"libsamplerate\"\n#define VERSION \"0.2.2\"\n"
     "#define CPU_CLIPS_NEGATIVE 0\n#define CPU_CLIPS_POSITIVE 0\n"
     "#define HAVE_STDBOOL_H 1\n#define HAVE_STDINT_H 1\n"
     "#define ENABLE_SINC_FAST_CONVERTER 1\n#define ENABLE_SINC_MEDIUM_CONVERTER 1\n"
     "#define ENABLE_SINC_BEST_CONVERTER 1\n")
add_library(multitap_dep_libsamplerate STATIC
    "${CMAKE_SOURCE_DIR}/deps/libsamplerate/src/samplerate.c"
    "${CMAKE_SOURCE_DIR}/deps/libsamplerate/src/src_linear.c"
    "${CMAKE_SOURCE_DIR}/deps/libsamplerate/src/src_sinc.c"
    "${CMAKE_SOURCE_DIR}/deps/libsamplerate/src/src_zoh.c")
target_include_directories(multitap_dep_libsamplerate SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/deps/libsamplerate/include")
target_include_directories(multitap_dep_libsamplerate PRIVATE "${LIBSAMPLERATE_CONFIG}")
target_compile_definitions(multitap_dep_libsamplerate PRIVATE HAVE_CONFIG_H)
# Optimized even in Debug builds: it runs for every audio frame and we don't debug it.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(multitap_dep_libsamplerate PRIVATE -O2)
endif()
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(multitap_dep_libsamplerate PUBLIC ${MATH_LIBRARY})
endif()

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
    viewport.cpp
    picture.cpp
    window_size.cpp
    localization.cpp
    rate_control.cpp
)

target_include_directories(frontend_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frontend_common PUBLIC imgui SDL3::SDL3 Vulkan::Vulkan host_common multitap_dep_libsamplerate)

# Vulkan validation layers + debug messenger are enabled in Debug builds only.
target_compile_definitions(frontend_common PRIVATE $<$<CONFIG:Debug>:MULTITAP_VULKAN_VALIDATION=1>)

multitap_set_project_flags(frontend_common)

# Build-time tool: a frontend's strings.txt -> its string IDs, English table
# and .pot (see tools/strings_generator.cpp).
add_executable(multitap_strings_generator tools/strings_generator.cpp)
target_link_libraries(multitap_strings_generator PRIVATE host_common)
multitap_set_project_flags(multitap_strings_generator)

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

    add_executable(frontend_viewport_test tests/viewport_test.cpp)
    target_link_libraries(frontend_viewport_test PRIVATE frontend_common)
    multitap_set_project_flags(frontend_viewport_test)
    add_test(NAME frontend_viewport COMMAND frontend_viewport_test)
    set_tests_properties(frontend_viewport PROPERTIES
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")

    add_executable(frontend_window_size_test tests/window_size_test.cpp)
    target_link_libraries(frontend_window_size_test PRIVATE frontend_common)
    multitap_set_project_flags(frontend_window_size_test)
    add_test(NAME frontend_window_size COMMAND frontend_window_size_test)
    set_tests_properties(frontend_window_size PROPERTIES
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")

    add_executable(frontend_localization_test tests/localization_test.cpp)
    target_link_libraries(frontend_localization_test PRIVATE frontend_common)
    multitap_set_project_flags(frontend_localization_test)
    add_test(NAME frontend_localization
             COMMAND frontend_localization_test "${CMAKE_CURRENT_BINARY_DIR}/localization_test")
    set_tests_properties(frontend_localization PROPERTIES
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")

    add_executable(frontend_rate_control_test tests/rate_control_test.cpp)
    target_link_libraries(frontend_rate_control_test PRIVATE frontend_common)
    multitap_set_project_flags(frontend_rate_control_test)
    add_test(NAME frontend_rate_control COMMAND frontend_rate_control_test)
    set_tests_properties(frontend_rate_control PROPERTIES
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")

    # Resampler comparison (plan-milestones, milestone 2): every candidate is
    # built here from deps/ (read-only, without our warning flags) only for
    # this tool. The one chosen moves into the real audio path later.
    set(MULTITAP_DEPS "${CMAKE_SOURCE_DIR}/deps")

    # speexdsp: only its resampler, built standalone with our own symbol prefix.
    add_library(multitap_dep_speex_resampler STATIC "${MULTITAP_DEPS}/speexdsp/libspeexdsp/resample.c")
    target_include_directories(multitap_dep_speex_resampler SYSTEM PUBLIC "${MULTITAP_DEPS}/speexdsp/include/speex")
    target_include_directories(multitap_dep_speex_resampler PRIVATE "${MULTITAP_DEPS}/speexdsp/libspeexdsp")
    target_compile_definitions(multitap_dep_speex_resampler PUBLIC OUTSIDE_SPEEX RANDOM_PREFIX=multitap_speex
                               PRIVATE FLOATING_POINT EXPORT=)

    # libretro-common's sinc driver (MIT-style license in the file itself):
    # its C path only (no SIMD mask), plus the aligned allocator it needs.
    add_library(multitap_dep_retro_sinc STATIC
        "${MULTITAP_DEPS}/libretro-common/audio/resampler/drivers/sinc_resampler.c"
        "${MULTITAP_DEPS}/libretro-common/memmap/memalign.c")
    target_include_directories(multitap_dep_retro_sinc SYSTEM PUBLIC "${MULTITAP_DEPS}/libretro-common/include")

    foreach(dep multitap_dep_speex_resampler multitap_dep_retro_sinc)
        if(MATH_LIBRARY)
            target_link_libraries(${dep} PUBLIC ${MATH_LIBRARY})
        endif()
    endforeach()

    add_executable(resampler_measure tools/resampler_measure.cpp)
    target_link_libraries(resampler_measure PRIVATE SDL3::SDL3 multitap_dep_libsamplerate
                          multitap_dep_speex_resampler multitap_dep_retro_sinc)
    multitap_set_project_flags(resampler_measure)
    add_test(NAME frontend_resampler_measure COMMAND resampler_measure --quick)
    set_tests_properties(frontend_resampler_measure PROPERTIES
                         PASS_REGULAR_EXPRESSION "all candidates ran"
                         FAIL_REGULAR_EXPRESSION "FAILED|ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")
endif()
