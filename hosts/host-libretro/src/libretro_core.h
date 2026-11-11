#pragma once

// A loaded libretro core: the shared library plus every retro_* entry point.
//
// This file only deals with symbols. It never calls into the core; the
// lifecycle (set_environment -> init -> load_game ...) is in libretro_host.

#include "dynamic_library.h"
#include "libretro.h"

struct LibretroCore
{
    DynamicLibrary library = {};

    // Every function a libretro core must export (libretro.h, "RETRO_API").
    // Names match the API exactly so they are easy to grep for.
    void     (*retro_set_environment)(retro_environment_t)                     = nullptr;
    void     (*retro_set_video_refresh)(retro_video_refresh_t)                 = nullptr;
    void     (*retro_set_audio_sample)(retro_audio_sample_t)                   = nullptr;
    void     (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t)       = nullptr;
    void     (*retro_set_input_poll)(retro_input_poll_t)                       = nullptr;
    void     (*retro_set_input_state)(retro_input_state_t)                     = nullptr;
    void     (*retro_init)(void)                                               = nullptr;
    void     (*retro_deinit)(void)                                             = nullptr;
    unsigned (*retro_api_version)(void)                                        = nullptr;
    void     (*retro_get_system_info)(retro_system_info*)                      = nullptr;
    void     (*retro_get_system_av_info)(retro_system_av_info*)                = nullptr;
    void     (*retro_set_controller_port_device)(unsigned, unsigned)           = nullptr;
    void     (*retro_reset)(void)                                              = nullptr;
    void     (*retro_run)(void)                                                = nullptr;
    size_t   (*retro_serialize_size)(void)                                     = nullptr;
    bool     (*retro_serialize)(void*, size_t)                                 = nullptr;
    bool     (*retro_unserialize)(const void*, size_t)                         = nullptr;
    void     (*retro_cheat_reset)(void)                                        = nullptr;
    void     (*retro_cheat_set)(unsigned, bool, const char*)                   = nullptr;
    bool     (*retro_load_game)(const retro_game_info*)                        = nullptr;
    bool     (*retro_load_game_special)(unsigned, const retro_game_info*, size_t) = nullptr;
    void     (*retro_unload_game)(void)                                        = nullptr;
    unsigned (*retro_get_region)(void)                                         = nullptr;
    void*    (*retro_get_memory_data)(unsigned)                                = nullptr;
    size_t   (*retro_get_memory_size)(unsigned)                                = nullptr;
};

// Opens the library and resolves all retro_* symbols. Fails (and closes the
// library again) if any symbol is missing: a core missing one is broken.
bool libretro_core_load(LibretroCore* core, const char* path);

// Closes the library and clears every pointer. Safe on an unloaded core.
// The caller must have called retro_deinit() first if retro_init() ran.
void libretro_core_unload(LibretroCore* core);
