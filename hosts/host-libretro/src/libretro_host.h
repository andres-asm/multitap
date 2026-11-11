#pragma once

// host-libretro: our libretro implementation.
//
// A LibretroHost owns one loaded core and everything the core was told or
// asked for. Lifecycle, in this order:
//
//   libretro_host_load_core()     dlopen, symbol check, set_environment, get_system_info, retro_init
//   libretro_host_load_game()     retro_load_game, retro_get_system_av_info
//   (running frames: next feature)
//   libretro_host_unload_game()   retro_unload_game
//   libretro_host_unload_core()   retro_deinit, dlclose
//
// Multi-instancing: libretro callbacks carry no userdata pointer, so the host
// keeps ONE thread_local "current instance" pointer. It is set right before
// every call into the core and cleared right after (see libretro_host.cpp).
// That is the only hidden global state in the host, by design.

#include <cstddef>
#include <cstdint>

#include "libretro.h"
#include "libretro_core.h"

constexpr size_t LIBRETRO_PATH_MAX = 4096;

// Directories we hand to the core. Empty string = "not defined".
struct LibretroHostConfig
{
    const char* system_directory = "";
    const char* save_directory   = "";
};

struct LibretroHost
{
    LibretroCore core = {};

    // Our own copies: the core may keep the pointers we give it, so these
    // must live as long as the core is loaded.
    char core_path[LIBRETRO_PATH_MAX]        = {};
    char system_directory[LIBRETRO_PATH_MAX] = {};
    char save_directory[LIBRETRO_PATH_MAX]   = {};
    char content_path[LIBRETRO_PATH_MAX]     = {};

    // Content loaded into memory (when the core doesn't need a path).
    // Kept until retro_unload_game(): the core may keep pointing into it.
    uint8_t* content_data = nullptr;
    size_t   content_size = 0;

    bool core_initialized = false;   // retro_init() ran
    bool game_loaded      = false;   // retro_load_game() succeeded

    // Filled from the core.
    retro_system_info    system_info = {};
    retro_system_av_info av_info     = {};

    // What the core asked for through the environment callback.
    retro_pixel_format pixel_format       = RETRO_PIXEL_FORMAT_0RGB1555;   // libretro's default
    bool               supports_no_game   = false;
    bool               shutdown_requested = false;
    unsigned           performance_level  = 0;

    // Environment statistics (for logging and the debug UI later).
    // Indexed by the command number without the EXPERIMENTAL/PRIVATE bits.
    uint32_t environment_call_counts[256] = {};
};

bool libretro_host_load_core(LibretroHost* host, const char* core_path, const LibretroHostConfig* config);

// content_path == nullptr starts the core without content (needs SET_SUPPORT_NO_GAME).
bool libretro_host_load_game(LibretroHost* host, const char* content_path);

// Both are safe to call when nothing is loaded.
void libretro_host_unload_game(LibretroHost* host);
void libretro_host_unload_core(LibretroHost* host);

// The instance the core is currently running in, or nullptr outside core calls.
LibretroHost* libretro_host_current();
