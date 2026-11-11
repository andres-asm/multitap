#pragma once

// The libretro environment callback: how a core asks the frontend for things
// (directories, pixel format, options, interfaces...).

#include <cstdint>

struct LibretroHost;

// Handles one environment call for `host`. Returns what the core sees:
// true = we understood and handled it, false = not supported.
// Every call is logged (first time per command at info level, then at debug).
bool libretro_environment_handle(LibretroHost* host, uint32_t command, void* data);

// "SET_PIXEL_FORMAT", ... or "UNKNOWN". Generated, see libretro_environment_names.cpp.
const char* libretro_environment_command_name(uint32_t command);
