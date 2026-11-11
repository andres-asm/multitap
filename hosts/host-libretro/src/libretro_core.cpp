#include "libretro_core.h"

#include "host_log.h"

// Logs a missing symbol. Returns true when the symbol was found.
static bool check_symbol(GenericFunction function, const char* name, const char* path)
{
    if (function == nullptr)
    {
        host_log(LogLevel::error, "Core %s does not export %s", path, name);
        return false;
    }
    return true;
}

bool libretro_core_load(LibretroCore* core, const char* path)
{
    if (!dynamic_library_open(&core->library, path))
    {
        return false;
    }

    const DynamicLibrary* library = &core->library;
    bool all_found = true;
    GenericFunction function = nullptr;

    // One block per symbol on purpose: when one is missing, the log names it,
    // and a debugger can stop on the exact line.

    function = dynamic_library_symbol(library, "retro_set_environment");
    all_found = check_symbol(function, "retro_set_environment", path) && all_found;
    core->retro_set_environment = reinterpret_cast<void (*)(retro_environment_t)>(function);

    function = dynamic_library_symbol(library, "retro_set_video_refresh");
    all_found = check_symbol(function, "retro_set_video_refresh", path) && all_found;
    core->retro_set_video_refresh = reinterpret_cast<void (*)(retro_video_refresh_t)>(function);

    function = dynamic_library_symbol(library, "retro_set_audio_sample");
    all_found = check_symbol(function, "retro_set_audio_sample", path) && all_found;
    core->retro_set_audio_sample = reinterpret_cast<void (*)(retro_audio_sample_t)>(function);

    function = dynamic_library_symbol(library, "retro_set_audio_sample_batch");
    all_found = check_symbol(function, "retro_set_audio_sample_batch", path) && all_found;
    core->retro_set_audio_sample_batch = reinterpret_cast<void (*)(retro_audio_sample_batch_t)>(function);

    function = dynamic_library_symbol(library, "retro_set_input_poll");
    all_found = check_symbol(function, "retro_set_input_poll", path) && all_found;
    core->retro_set_input_poll = reinterpret_cast<void (*)(retro_input_poll_t)>(function);

    function = dynamic_library_symbol(library, "retro_set_input_state");
    all_found = check_symbol(function, "retro_set_input_state", path) && all_found;
    core->retro_set_input_state = reinterpret_cast<void (*)(retro_input_state_t)>(function);

    function = dynamic_library_symbol(library, "retro_init");
    all_found = check_symbol(function, "retro_init", path) && all_found;
    core->retro_init = reinterpret_cast<void (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_deinit");
    all_found = check_symbol(function, "retro_deinit", path) && all_found;
    core->retro_deinit = reinterpret_cast<void (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_api_version");
    all_found = check_symbol(function, "retro_api_version", path) && all_found;
    core->retro_api_version = reinterpret_cast<unsigned (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_get_system_info");
    all_found = check_symbol(function, "retro_get_system_info", path) && all_found;
    core->retro_get_system_info = reinterpret_cast<void (*)(retro_system_info*)>(function);

    function = dynamic_library_symbol(library, "retro_get_system_av_info");
    all_found = check_symbol(function, "retro_get_system_av_info", path) && all_found;
    core->retro_get_system_av_info = reinterpret_cast<void (*)(retro_system_av_info*)>(function);

    function = dynamic_library_symbol(library, "retro_set_controller_port_device");
    all_found = check_symbol(function, "retro_set_controller_port_device", path) && all_found;
    core->retro_set_controller_port_device = reinterpret_cast<void (*)(unsigned, unsigned)>(function);

    function = dynamic_library_symbol(library, "retro_reset");
    all_found = check_symbol(function, "retro_reset", path) && all_found;
    core->retro_reset = reinterpret_cast<void (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_run");
    all_found = check_symbol(function, "retro_run", path) && all_found;
    core->retro_run = reinterpret_cast<void (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_serialize_size");
    all_found = check_symbol(function, "retro_serialize_size", path) && all_found;
    core->retro_serialize_size = reinterpret_cast<size_t (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_serialize");
    all_found = check_symbol(function, "retro_serialize", path) && all_found;
    core->retro_serialize = reinterpret_cast<bool (*)(void*, size_t)>(function);

    function = dynamic_library_symbol(library, "retro_unserialize");
    all_found = check_symbol(function, "retro_unserialize", path) && all_found;
    core->retro_unserialize = reinterpret_cast<bool (*)(const void*, size_t)>(function);

    function = dynamic_library_symbol(library, "retro_cheat_reset");
    all_found = check_symbol(function, "retro_cheat_reset", path) && all_found;
    core->retro_cheat_reset = reinterpret_cast<void (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_cheat_set");
    all_found = check_symbol(function, "retro_cheat_set", path) && all_found;
    core->retro_cheat_set = reinterpret_cast<void (*)(unsigned, bool, const char*)>(function);

    function = dynamic_library_symbol(library, "retro_load_game");
    all_found = check_symbol(function, "retro_load_game", path) && all_found;
    core->retro_load_game = reinterpret_cast<bool (*)(const retro_game_info*)>(function);

    function = dynamic_library_symbol(library, "retro_load_game_special");
    all_found = check_symbol(function, "retro_load_game_special", path) && all_found;
    core->retro_load_game_special = reinterpret_cast<bool (*)(unsigned, const retro_game_info*, size_t)>(function);

    function = dynamic_library_symbol(library, "retro_unload_game");
    all_found = check_symbol(function, "retro_unload_game", path) && all_found;
    core->retro_unload_game = reinterpret_cast<void (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_get_region");
    all_found = check_symbol(function, "retro_get_region", path) && all_found;
    core->retro_get_region = reinterpret_cast<unsigned (*)(void)>(function);

    function = dynamic_library_symbol(library, "retro_get_memory_data");
    all_found = check_symbol(function, "retro_get_memory_data", path) && all_found;
    core->retro_get_memory_data = reinterpret_cast<void* (*)(unsigned)>(function);

    function = dynamic_library_symbol(library, "retro_get_memory_size");
    all_found = check_symbol(function, "retro_get_memory_size", path) && all_found;
    core->retro_get_memory_size = reinterpret_cast<size_t (*)(unsigned)>(function);

    if (!all_found)
    {
        libretro_core_unload(core);
        return false;
    }
    return true;
}

void libretro_core_unload(LibretroCore* core)
{
    dynamic_library_close(&core->library);
    *core = LibretroCore{};
}
