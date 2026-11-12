# host-libretro: our libretro implementation, as a static library.
# Frontends (piccolo, the bundled test frontend) link it in-process; an IPC
# process wrapper comes later for Multitap.

add_library(host_libretro STATIC
    src/libretro_core.cpp
    src/libretro_host.cpp
    src/libretro_environment.cpp
    src/libretro_environment_names.cpp
    src/libretro_video.cpp
    src/libretro_audio.cpp
    src/libretro_options.cpp
)

target_include_directories(host_libretro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(host_libretro PUBLIC host_common libretro_api)

multitap_set_project_flags(host_libretro)

# The test frontend bundled with host-libretro: an ImGui window to load and run
# cores and check that every libretro function works. Not meant for playing.
add_executable(libretro_test_frontend
    frontend/main.cpp
    frontend/fixed_input.cpp
)
target_link_libraries(libretro_test_frontend PRIVATE host_libretro frontend_common)
multitap_set_project_flags(libretro_test_frontend)

# Development tool: load a core (+ content), log environment calls, print info, unload.
add_executable(libretro_core_info tools/core_info.cpp)
target_link_libraries(libretro_core_info PRIVATE host_libretro)
multitap_set_project_flags(libretro_core_info)

# ---------------------------------------------------------------------------
# Tests: our own minimal test core + libretro_core_info. Run with: ctest --test-dir build
# ---------------------------------------------------------------------------
if(MULTITAP_BUILD_TESTS)
    # MODULE = a plugin loaded with dlopen/LoadLibrary, like a real core.
    add_library(test_core MODULE tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE libretro_api)
    set_target_properties(test_core PROPERTIES PREFIX "" OUTPUT_NAME "test_core_libretro")
    multitap_set_project_flags(test_core)

    # Same core without retro_run: the host must refuse it.
    add_library(test_core_missing_symbol MODULE tests/test_core.cpp)
    target_link_libraries(test_core_missing_symbol PRIVATE libretro_api)
    target_compile_definitions(test_core_missing_symbol PRIVATE TEST_CORE_OMIT_RUN=1)
    set_target_properties(test_core_missing_symbol PROPERTIES PREFIX "" OUTPUT_NAME "test_core_missing_symbol_libretro")
    multitap_set_project_flags(test_core_missing_symbol)

    set(TEST_CONTENT_GOOD "${CMAKE_CURRENT_BINARY_DIR}/test_content_good.bin")
    set(TEST_CONTENT_BAD  "${CMAKE_CURRENT_BINARY_DIR}/test_content_bad.bin")
    file(WRITE "${TEST_CONTENT_GOOD}" "MULTITAP test content\n")
    file(WRITE "${TEST_CONTENT_BAD}"  "not the right marker\n")

    # Anything in this list appearing in the output fails a test, whatever the exit code.
    set(TEST_CORE_ERRORS "XRGB8888 refused|host accepted an invalid pixel format|host accepted GET_CAN_DUPE with NULL data|GET_INPUT_BITMASKS[^\n]*REJECTED|no current instance|ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")

    add_test(NAME host_libretro_no_game
             COMMAND libretro_core_info --system /tmp $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_no_game PROPERTIES FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    add_test(NAME host_libretro_content_from_memory
             COMMAND libretro_core_info $<TARGET_FILE:test_core> "${TEST_CONTENT_GOOD}")
    set_tests_properties(host_libretro_content_from_memory PROPERTIES FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # The core rejects this content: the tool must exit non-zero and still unload cleanly.
    add_test(NAME host_libretro_content_rejected
             COMMAND libretro_core_info $<TARGET_FILE:test_core> "${TEST_CONTENT_BAD}")
    set_tests_properties(host_libretro_content_rejected PROPERTIES WILL_FAIL TRUE)

    add_test(NAME host_libretro_missing_content_file
             COMMAND libretro_core_info $<TARGET_FILE:test_core> "${CMAKE_CURRENT_BINARY_DIR}/does_not_exist.bin")
    set_tests_properties(host_libretro_missing_content_file PROPERTIES WILL_FAIL TRUE)

    add_test(NAME host_libretro_missing_symbol
             COMMAND libretro_core_info $<TARGET_FILE:test_core_missing_symbol>)
    set_tests_properties(host_libretro_missing_symbol PROPERTIES PASS_REGULAR_EXPRESSION "does not export retro_run")

    # A handled command sent with NULL data must be refused and logged, not crash.
    add_test(NAME host_libretro_null_data_refused
             COMMAND libretro_core_info $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_null_data_refused PROPERTIES PASS_REGULAR_EXPRESSION "GET_CAN_DUPE[^\n]*REJECTED[^\n]*NULL data")

    add_test(NAME host_libretro_missing_core_file
             COMMAND libretro_core_info "${CMAKE_CURRENT_BINARY_DIR}/no_such_core.so")
    set_tests_properties(host_libretro_missing_core_file PROPERTIES WILL_FAIL TRUE)

    # Pixel format conversion (all three libretro formats -> XRGB8888 + alpha).
    add_executable(host_libretro_video_test tests/video_conversion_test.cpp)
    target_link_libraries(host_libretro_video_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_video_test)
    add_test(NAME host_libretro_video_conversion COMMAND host_libretro_video_test)

    # Per-frame audio buffer (single-sample and batch appends, growth, clear).
    add_executable(host_libretro_audio_test tests/audio_buffer_test.cpp)
    target_link_libraries(host_libretro_audio_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_audio_test)
    add_test(NAME host_libretro_audio_buffer COMMAND host_libretro_audio_test)

    # Core options: v0/v1/v2 conversion, defaults, bad definitions, set/get/update, visibility.
    add_executable(host_libretro_options_test tests/options_test.cpp)
    target_link_libraries(host_libretro_options_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_options_test)
    add_test(NAME host_libretro_options COMMAND host_libretro_options_test)

    # Our test core declares its option with SET_VARIABLES: keeps the oldest format covered end to end.
    add_test(NAME host_libretro_options_v0_test_core
             COMMAND libretro_core_info $<TARGET_FILE:test_core>)
    set_tests_properties(host_libretro_options_v0_test_core PROPERTIES
        PASS_REGULAR_EXPRESSION "Option test_core_speed = normal \\(normal\\|fast\\)"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    # The interactive test wizard core lives in its own repo, pulled in as a
    # submodule. Built when present. Its pages need a human, so CTest only runs a
    # headless smoke test with it (below); the full walk-through is manual.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test-libretro/CMakeLists.txt")
        add_subdirectory(test-libretro)

        # Headless smoke test: 120 frames of the wizard (environment page) through the host.
        # Checks retro_run, video copy/conversion and the input path under the sanitizers.
        add_test(NAME host_libretro_wizard_runs
                 COMMAND libretro_core_info --frames 120 $<TARGET_FILE:test_wizard_libretro>)
        set_tests_properties(host_libretro_wizard_runs PROPERTIES
            PASS_REGULAR_EXPRESSION "Ran 120 frames: 120 video frames \\(640x480\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        # Core options through each API the wizard can pick (it follows the version
        # we report), then a width change at runtime: the core must re-read its
        # options, send SET_GEOMETRY and produce 720x480 frames.
        foreach(_version 2 1 0)
            add_test(NAME host_libretro_wizard_options_v${_version}
                     COMMAND libretro_core_info --options-version ${_version} --frames 10
                             --set testwizard_width=720 $<TARGET_FILE:test_wizard_libretro>)
            set_tests_properties(host_libretro_wizard_options_v${_version} PROPERTIES
                PASS_REGULAR_EXPRESSION "Options: 2 declared with API v${_version}.*Option testwizard_width = 640 \\(640\\|720\\).*SET_GEOMETRY +\\( *37\\) handled +720x480.*Ran 10 frames: 10 video frames \\(720x480\\)"
                FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")
        endforeach()

        # A value that isn't one of the option's values must be refused.
        add_test(NAME host_libretro_option_bad_value
                 COMMAND libretro_core_info --set testwizard_width=800 $<TARGET_FILE:test_wizard_libretro>)
        set_tests_properties(host_libretro_option_bad_value PROPERTIES
            PASS_REGULAR_EXPRESSION "testwizard_width: '800' is not one of its values")
    endif()
endif()
