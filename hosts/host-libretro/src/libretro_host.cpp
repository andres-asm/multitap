#include "libretro_host.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "host_log.h"
#include "libretro_environment.h"

// ---------------------------------------------------------------------------
// Current instance
// ---------------------------------------------------------------------------
//
// libretro callbacks have no userdata pointer, so a callback can't know which
// LibretroHost it belongs to. We set this pointer before every call into the
// core and clear it after. thread_local because each instance's core runs on
// one thread; two instances on two threads each see their own pointer.
//
// Rule: every call into the core goes through host_enter()/host_leave().

static thread_local LibretroHost* g_current_host = nullptr;

static void host_enter(LibretroHost* host)
{
    // No nesting: the core never calls back into "load core" etc.
    assert(g_current_host == nullptr);
    g_current_host = host;
}

static void host_leave(LibretroHost* host)
{
    assert(g_current_host == host);
    (void)host;
    g_current_host = nullptr;
}

LibretroHost* libretro_host_current()
{
    return g_current_host;
}

// ---------------------------------------------------------------------------
// Callbacks given to the core
// ---------------------------------------------------------------------------

static bool RETRO_CALLCONV environment_callback(unsigned command, void* data)
{
    LibretroHost* host = g_current_host;
    if (host == nullptr)
    {
        // A core calling us outside retro_* calls (e.g. from its own thread).
        // Log it so we notice; refusing is safer than guessing an instance.
        host_log(LogLevel::error, "env %s called with no current instance (core thread?)",
                 libretro_environment_command_name(command));
        return false;
    }
    return libretro_environment_handle(host, command, data);
}

// Video, audio and input are the next feature. These stubs exist because the
// API requires the callbacks to be set before retro_run().

static void RETRO_CALLCONV video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch)
{
    (void)data;
    (void)width;
    (void)height;
    (void)pitch;
}

static void RETRO_CALLCONV audio_sample_callback(int16_t left, int16_t right)
{
    (void)left;
    (void)right;
}

static size_t RETRO_CALLCONV audio_sample_batch_callback(const int16_t* data, size_t frames)
{
    (void)data;
    return frames;   // "consumed everything"
}

static void RETRO_CALLCONV input_poll_callback(void)
{
}

static int16_t RETRO_CALLCONV input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port;
    (void)device;
    (void)index;
    (void)id;
    return 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Copies `source` into a fixed buffer. Fails (and logs) instead of truncating.
static bool copy_path(char* destination, size_t destination_size, const char* source, const char* what)
{
    size_t length = std::strlen(source);
    if (length >= destination_size)
    {
        host_log(LogLevel::error, "%s path is too long (%zu bytes, max %zu)", what, length, destination_size - 1);
        return false;
    }
    std::memcpy(destination, source, length + 1);
    return true;
}

// Reads a whole file into a new[] buffer. On success the caller owns *data.
static bool read_whole_file(const char* path, uint8_t** data, size_t* size)
{
    *data = nullptr;
    *size = 0;

    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        host_log(LogLevel::error, "Could not open %s", path);
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        host_log(LogLevel::error, "Could not seek in %s", path);
        std::fclose(file);
        return false;
    }
    long end = std::ftell(file);
    if (end < 0)
    {
        host_log(LogLevel::error, "Could not get the size of %s", path);
        std::fclose(file);
        return false;
    }
    std::rewind(file);

    size_t file_size = static_cast<size_t>(end);
    // new[] of size 0 is valid; an empty file is still "content".
    uint8_t* buffer = new uint8_t[file_size > 0 ? file_size : 1];

    size_t read = std::fread(buffer, 1, file_size, file);
    std::fclose(file);

    if (read != file_size)
    {
        host_log(LogLevel::error, "Short read on %s (%zu of %zu bytes)", path, read, file_size);
        delete[] buffer;
        return false;
    }

    *data = buffer;
    *size = file_size;
    return true;
}

static const char* safe_string(const char* text)
{
    return text != nullptr ? text : "(null)";
}

// ---------------------------------------------------------------------------
// Core
// ---------------------------------------------------------------------------

bool libretro_host_load_core(LibretroHost* host, const char* core_path, const LibretroHostConfig* config)
{
    if (host->core.library.handle != nullptr)
    {
        host_log(LogLevel::error, "A core is already loaded in this host");
        return false;
    }

    if (!copy_path(host->core_path, sizeof(host->core_path), core_path, "Core") ||
        !copy_path(host->system_directory, sizeof(host->system_directory), config->system_directory, "System directory") ||
        !copy_path(host->save_directory, sizeof(host->save_directory), config->save_directory, "Save directory"))
    {
        return false;
    }

    host_log(LogLevel::info, "Loading core %s", host->core_path);
    if (!libretro_core_load(&host->core, host->core_path))
    {
        return false;
    }

    host_enter(host);

    unsigned api_version = host->core.retro_api_version();
    if (api_version != RETRO_API_VERSION)
    {
        host_leave(host);
        host_log(LogLevel::error, "Core uses libretro API version %u, we support %u", api_version, RETRO_API_VERSION);
        libretro_core_unload(&host->core);
        return false;
    }

    // Order matters: set_environment must come before anything else, cores
    // already call the environment from inside it (SET_SUPPORT_NO_GAME, options...).
    host->core.retro_set_environment(environment_callback);

    host->system_info = retro_system_info{};
    host->core.retro_get_system_info(&host->system_info);

    host->core.retro_init();
    host->core_initialized = true;

    host->core.retro_set_video_refresh(video_refresh_callback);
    host->core.retro_set_audio_sample(audio_sample_callback);
    host->core.retro_set_audio_sample_batch(audio_sample_batch_callback);
    host->core.retro_set_input_poll(input_poll_callback);
    host->core.retro_set_input_state(input_state_callback);

    host_leave(host);

    host_log(LogLevel::info, "Core: %s %s", safe_string(host->system_info.library_name),
             safe_string(host->system_info.library_version));
    host_log(LogLevel::info, "  extensions: %s, need_fullpath: %s, block_extract: %s",
             safe_string(host->system_info.valid_extensions),
             host->system_info.need_fullpath ? "yes" : "no",
             host->system_info.block_extract ? "yes" : "no");
    return true;
}

void libretro_host_unload_core(LibretroHost* host)
{
    libretro_host_unload_game(host);

    if (host->core_initialized)
    {
        host_enter(host);
        host->core.retro_deinit();
        host_leave(host);
        host->core_initialized = false;
    }

    if (host->core.library.handle != nullptr)
    {
        host_log(LogLevel::info, "Unloading core %s", host->core_path);
        libretro_core_unload(&host->core);
    }

    // Back to a clean state so the same LibretroHost can load another core.
    *host = LibretroHost{};
}

// ---------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------

bool libretro_host_load_game(LibretroHost* host, const char* content_path)
{
    if (!host->core_initialized)
    {
        host_log(LogLevel::error, "Load a core before loading content");
        return false;
    }
    if (host->game_loaded)
    {
        host_log(LogLevel::error, "Content is already loaded; unload it first");
        return false;
    }

    bool loaded = false;

    if (content_path == nullptr)
    {
        if (!host->supports_no_game)
        {
            host_log(LogLevel::error, "This core needs content (it never sent SET_SUPPORT_NO_GAME)");
            return false;
        }

        host_log(LogLevel::info, "Starting core without content");
        host_enter(host);
        loaded = host->core.retro_load_game(nullptr);
        host_leave(host);
    }
    else
    {
        if (!copy_path(host->content_path, sizeof(host->content_path), content_path, "Content"))
        {
            return false;
        }

        retro_game_info game = {};
        game.path = host->content_path;

        if (!host->system_info.need_fullpath)
        {
            if (!read_whole_file(host->content_path, &host->content_data, &host->content_size))
            {
                return false;
            }
            game.data = host->content_data;
            game.size = host->content_size;
        }

        host_log(LogLevel::info, "Loading content %s (%s)", host->content_path,
                 host->system_info.need_fullpath ? "by path" : "from memory");

        host_enter(host);
        loaded = host->core.retro_load_game(&game);
        host_leave(host);
    }

    if (!loaded)
    {
        host_log(LogLevel::error, "retro_load_game failed");
        delete[] host->content_data;
        host->content_data = nullptr;
        host->content_size = 0;
        host->content_path[0] = '\0';
        return false;
    }

    host->game_loaded = true;

    host_enter(host);
    host->av_info = retro_system_av_info{};
    host->core.retro_get_system_av_info(&host->av_info);
    host_leave(host);

    const retro_game_geometry* geometry = &host->av_info.geometry;
    host_log(LogLevel::info, "Video: base %ux%u, max %ux%u, aspect %.4f, %.4f fps",
             geometry->base_width, geometry->base_height, geometry->max_width, geometry->max_height,
             static_cast<double>(geometry->aspect_ratio), host->av_info.timing.fps);
    host_log(LogLevel::info, "Audio: %.1f Hz", host->av_info.timing.sample_rate);
    return true;
}

void libretro_host_unload_game(LibretroHost* host)
{
    if (host->game_loaded)
    {
        host_enter(host);
        host->core.retro_unload_game();
        host_leave(host);
        host->game_loaded = false;
        host_log(LogLevel::info, "Content unloaded");
    }

    // Only now: the core may have pointed into this buffer until retro_unload_game().
    delete[] host->content_data;
    host->content_data = nullptr;
    host->content_size = 0;
    host->content_path[0] = '\0';
}
