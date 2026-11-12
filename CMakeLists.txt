cmake_minimum_required(VERSION 3.25)

project(multitap LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MULTITAP_SYSTEM_SDL3        "Use the system SDL3 instead of deps/SDL" OFF)
option(MULTITAP_WARNINGS_AS_ERRORS "Treat warnings in our own code as errors"    OFF)
option(MULTITAP_SANITIZE           "Build our own code with ASan + UBSan"         OFF)
option(MULTITAP_BUILD_TESTS        "Build test cores and register CTest tests"    ON)

# Where builds are copied so they're easy to find and run:
#   MULTITAP_BIN_DIR    the programs (test frontend, core info tool, piccolo)
#   MULTITAP_CORES_DIR  the libretro test core, next to any other cores dropped
#                       there (the test frontend lists that folder)
# Only from normal builds: sanitizer builds need the ASan runtime (an ASan core
# can't be loaded by a normal frontend), so they keep everything in the build
# folder. Empty = don't copy.
if(MULTITAP_SANITIZE)
    set(_data_dir_default "")
elseif(WIN32)
    set(_data_dir_default "$ENV{USERPROFILE}/.host-libretro")
else()
    set(_data_dir_default "$ENV{HOME}/.host-libretro")
endif()
if(_data_dir_default)
    set(_cores_dir_default "${_data_dir_default}/cores")
else()
    set(_cores_dir_default "")
endif()
set(MULTITAP_BIN_DIR "${_data_dir_default}" CACHE PATH "Copy programs here after building (empty = don't)")
set(MULTITAP_CORES_DIR "${_cores_dir_default}" CACHE PATH "Copy test cores here after building (empty = don't)")

# Copies `target`'s file into `directory` after every build. Written to a
# temporary name first, then renamed over the old one: a program or core that
# is running keeps its old copy (overwriting a running executable fails, and
# overwriting a loaded .so in place can crash the program using it).
function(multitap_copy_after_build target directory)
    if(NOT directory)
        return()
    endif()
    add_custom_target(copy_${target} ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> "${directory}/$<TARGET_FILE_NAME:${target}>.new"
        COMMAND ${CMAKE_COMMAND} -E rename "${directory}/$<TARGET_FILE_NAME:${target}>.new" "${directory}/$<TARGET_FILE_NAME:${target}>"
        DEPENDS ${target}
        COMMENT "Copying ${target} to ${directory}"
        VERBATIM)
endfunction()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Submodule check
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui.cpp")
    message(FATAL_ERROR "deps/imgui is empty. Run: git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies (built with their own defaults, not our warnings)
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

if(MULTITAP_SYSTEM_SDL3)
    find_package(SDL3 CONFIG REQUIRED)
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL/CMakeLists.txt")
        message(FATAL_ERROR "deps/SDL is empty. Run: git submodule update --init --recursive")
    endif()
    set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
    add_subdirectory(deps/SDL EXCLUDE_FROM_ALL SYSTEM)
endif()

# Dear ImGui core + SDL3 platform backend + Vulkan renderer backend.
# Sources come straight from the submodule; nothing is copied or patched.
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui")
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)

# libretro API header. Only include/libretro*.h is used; none of
# libretro-common's .c files are compiled (see CLAUDE.md).
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/libretro-common/include/libretro.h")
    message(FATAL_ERROR "deps/libretro-common is empty. Run: git submodule update --init --recursive")
endif()
add_library(libretro_api INTERFACE)
target_include_directories(libretro_api SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/deps/libretro-common/include")

# ---------------------------------------------------------------------------
# Flags for OUR code only. Apply with: multitap_set_project_flags(<target>)
# ---------------------------------------------------------------------------
function(multitap_set_project_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(MULTITAP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wconversion -Wsign-conversion -Wshadow
            -Wold-style-cast -Wcast-qual -Wnull-dereference
            # Designated initializers leave unnamed fields zeroed on purpose.
            -Wno-missing-field-initializers
            -fno-exceptions -fno-rtti
        )
        if(MULTITAP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(MULTITAP_SANITIZE)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

if(MULTITAP_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# Programs
# ---------------------------------------------------------------------------
add_subdirectory(hosts/common)
add_subdirectory(hosts/host-libretro)
add_subdirectory(frontends/common)
add_subdirectory(frontends/libretro)
# piccolo lives in its own repository (submodule). Built when present.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/frontends/piccolo/CMakeLists.txt")
    add_subdirectory(frontends/piccolo)
else()
    message(STATUS "frontends/piccolo is empty (submodule not checked out): not building piccolo. "
                   "Run: git submodule update --init --recursive")
endif()
