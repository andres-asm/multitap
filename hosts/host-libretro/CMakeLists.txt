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
)
target_link_libraries(libretro_test_frontend PRIVATE frontend_libretro)
multitap_set_project_flags(libretro_test_frontend)
multitap_copy_after_build(libretro_test_frontend "${MULTITAP_BIN_DIR}")

# Development tool: load a core (+ content), log environment calls, print info, unload.
add_executable(libretro_core_info tools/core_info.cpp)
target_link_libraries(libretro_core_info PRIVATE host_libretro)
multitap_set_project_flags(libretro_core_info)
multitap_copy_after_build(libretro_core_info "${MULTITAP_BIN_DIR}")

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

    # Peeking: what a core says about itself without retro_init.
    add_test(NAME host_libretro_peek_test_core
             COMMAND libretro_core_info --peek $<TARGET_FILE:test_core> "some/dir.v2/GAME.BIN")
    set_tests_properties(host_libretro_peek_test_core PROPERTIES
        PASS_REGULAR_EXPRESSION "name \"multitap test core\".*extensions \"bin\\|txt\".*no game yes.*1 core options \\(API v0\\).*Peek: accepts some/dir\\.v2/GAME\\.BIN"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED|retro_init")

    add_test(NAME host_libretro_peek_wrong_extension
             COMMAND libretro_core_info --peek $<TARGET_FILE:test_core> "game.sfc")
    set_tests_properties(host_libretro_peek_wrong_extension PROPERTIES
        PASS_REGULAR_EXPRESSION "Peek: does NOT accept game\\.sfc")

    # A loaded core must not be peeked, whatever the path spelling (symlink, "//").
    add_executable(host_libretro_peek_loaded_test tests/peek_loaded_test.cpp)
    target_link_libraries(host_libretro_peek_loaded_test PRIVATE host_libretro)
    multitap_set_project_flags(host_libretro_peek_loaded_test)
    set(TEST_CORE_LINK "${CMAKE_CURRENT_BINARY_DIR}/test_core_link_libretro${CMAKE_SHARED_MODULE_SUFFIX}")
    add_custom_command(TARGET test_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:test_core> "${TEST_CORE_LINK}"
        VERBATIM)
    add_test(NAME host_libretro_peek_loaded
             COMMAND host_libretro_peek_loaded_test $<TARGET_FILE:test_core>
                     "$<TARGET_FILE_DIR:test_core>//$<TARGET_FILE_NAME:test_core>" "${TEST_CORE_LINK}")
    set_tests_properties(host_libretro_peek_loaded PROPERTIES
        PASS_REGULAR_EXPRESSION "peek loaded: all checks passed"
        FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}")

    add_test(NAME host_libretro_peek_missing_symbol
             COMMAND libretro_core_info --peek $<TARGET_FILE:test_core_missing_symbol>)
    set_tests_properties(host_libretro_peek_missing_symbol PROPERTIES
        PASS_REGULAR_EXPRESSION "Peek: not a usable libretro core")

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

    # The interactive Test libretro core (test_libretro) lives in its own repo, pulled in as a
    # submodule. Built when present. Its pages need a human, so CTest only runs a
    # headless smoke test with it (below); the full walk-through is manual.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test-libretro/CMakeLists.txt")
        add_subdirectory(test-libretro)

        # Into the shared cores folder the test frontend lists (root CMakeLists.txt).
        multitap_copy_after_build(test_libretro "${MULTITAP_CORES_DIR}")

        # Headless smoke test: 120 frames of Test libretro (environment page) through the host.
        # Checks retro_run, video copy/conversion and the input path under the sanitizers.
        add_test(NAME host_libretro_test_libretro_runs
                 COMMAND libretro_core_info --frames 120 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_test_libretro_runs PROPERTIES
            PASS_REGULAR_EXPRESSION "Ran 120 frames: 120 video frames \\(640x480\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        # Core options through each API Test libretro can pick (it follows the version
        # we report), then a width change at runtime: the core must re-read its
        # options, send SET_GEOMETRY and produce 720x480 frames.
        foreach(_version 2 1 0)
            add_test(NAME host_libretro_test_libretro_options_v${_version}
                     COMMAND libretro_core_info --options-version ${_version} --frames 10
                             --set libretro_test_width=720 $<TARGET_FILE:test_libretro>)
            set_tests_properties(host_libretro_test_libretro_options_v${_version} PROPERTIES
                PASS_REGULAR_EXPRESSION "Options: 4 declared with API v${_version}.*Option libretro_test_width = 640 \\(640\\|720\\).*SET_GEOMETRY +\\( *37\\) handled +720x480.*Ran 10 frames: 10 video frames \\(720x480\\)"
                FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")
        endforeach()

        # Save RAM: the host writes it on unload and loads it back on the next run.
        # Test libretro exposes a 60-byte block; untouched, the core reports it as "empty".
        set(SRAM_DIR "${CMAKE_CURRENT_BINARY_DIR}/sram-test")
        add_test(NAME host_libretro_sram_clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${SRAM_DIR}")
        set_tests_properties(host_libretro_sram_clean PROPERTIES FIXTURES_SETUP sram_clean)

        add_test(NAME host_libretro_sram_first_run
                 COMMAND libretro_core_info --data-dir "${SRAM_DIR}" --frames 2 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_sram_first_run PROPERTIES
            FIXTURES_REQUIRED sram_clean
            FIXTURES_SETUP sram_written
            PASS_REGULAR_EXPRESSION "SRAM: 60 bytes, no [^\n]*test\\.srm yet.*SRAM: wrote 60 bytes"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        add_test(NAME host_libretro_sram_reload
                 COMMAND libretro_core_info --data-dir "${SRAM_DIR}" --frames 2 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_sram_reload PROPERTIES
            FIXTURES_REQUIRED sram_written
            PASS_REGULAR_EXPRESSION "SRAM: loaded 60 bytes.*save RAM: empty"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED|CORRUPTED")

        # Junk in the .srm must be noticed by the core, not silently accepted.
        set(SRAM_BAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/sram-bad-test")
        file(WRITE "${SRAM_BAD_DIR}/saves/test/test.srm" "this is not the save RAM layout Test libretro writes!!!!!!!!")
        add_test(NAME host_libretro_sram_corrupted
                 COMMAND libretro_core_info --data-dir "${SRAM_BAD_DIR}" --frames 1 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_sram_corrupted PROPERTIES PASS_REGULAR_EXPRESSION "save RAM: CORRUPTED")

        # Savestates: save slot 3 after 50 frames, load it in a new run: the
        # core must be back at frame 50; an empty slot must be refused.
        set(STATE_DIR "${CMAKE_CURRENT_BINARY_DIR}/state-test")
        add_test(NAME host_libretro_state_clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${STATE_DIR}")
        set_tests_properties(host_libretro_state_clean PROPERTIES FIXTURES_SETUP state_clean)

        add_test(NAME host_libretro_state_save
                 COMMAND libretro_core_info --data-dir "${STATE_DIR}" --frames 50 --save-state 3 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_state_save PROPERTIES
            FIXTURES_REQUIRED state_clean
            FIXTURES_SETUP state_saved
            PASS_REGULAR_EXPRESSION "Savestate: saved slot 3 \\([0-9]+ bytes\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        add_test(NAME host_libretro_state_load
                 COMMAND libretro_core_info --data-dir "${STATE_DIR}" --load-state 3 --frames 5 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_state_load PROPERTIES
            FIXTURES_REQUIRED state_saved
            PASS_REGULAR_EXPRESSION "state loaded: frame 50, page Environment.*Savestate: loaded slot 3"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        add_test(NAME host_libretro_state_empty_slot
                 COMMAND libretro_core_info --data-dir "${STATE_DIR}" --load-state 7 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_state_empty_slot PROPERTIES
            FIXTURES_REQUIRED state_saved
            PASS_REGULAR_EXPRESSION "Savestate: slot 7 is empty")

        # Height and frame rate change the core's maximum size and timing: they must
        # come through SET_SYSTEM_AV_INFO, and the frames that follow must match.
        add_test(NAME host_libretro_test_libretro_av_info
                 COMMAND libretro_core_info --frames 10 --set libretro_test_height=720 --set libretro_test_fps=30
                         $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_test_libretro_av_info PROPERTIES
            PASS_REGULAR_EXPRESSION "SET_SYSTEM_AV_INFO +\\( *32\\) handled +640x720 @ 30\\.000 fps.*Ran 10 frames: 10 video frames \\(640x720\\), 0 duplicates, 0 video errors, 16000 audio frames"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED")

        add_test(NAME host_libretro_peek_test_libretro
                 COMMAND libretro_core_info --peek $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_peek_test_libretro PROPERTIES
            PASS_REGULAR_EXPRESSION "name \"Test libretro\".*no game yes.*4 core options \\(API v2\\)"
            FAIL_REGULAR_EXPRESSION "${TEST_CORE_ERRORS}|REJECTED|\\] init")

        # A value that isn't one of the option's values must be refused.
        add_test(NAME host_libretro_option_bad_value
                 COMMAND libretro_core_info --set libretro_test_width=800 $<TARGET_FILE:test_libretro>)
        set_tests_properties(host_libretro_option_bad_value PROPERTIES
            PASS_REGULAR_EXPRESSION "libretro_test_width: '800' is not one of its values")
    endif()
endif()
