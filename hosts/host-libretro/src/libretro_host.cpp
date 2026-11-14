#include "libretro_host.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "host_fs.h"
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

// The callbacks below find their host through g_current_host. They are only
// valid inside retro_run (and load/unload); outside it they log and do nothing.

static void RETRO_CALLCONV video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch)
{
    LibretroHost* host = g_current_host;
    if (host == nullptr)
    {
        host_log(LogLevel::error, "video_refresh called with no current instance");
        return;
    }

    // NULL = "same image as last frame" (we said yes to GET_CAN_DUPE). Keep the old copy.
    if (data == nullptr)
    {
        host->video.duplicates++;
        host->stats.video_duplicates++;
        return;
    }

    // Hardware-rendered frames (OpenGL/Vulkan cores) aren't supported yet.
    if (data == RETRO_HW_FRAME_BUFFER_VALID)
    {
        if (host->stats.video_errors == 0)
        {
            host_log(LogLevel::error, "The core sent a hardware-rendered frame; not supported yet");
        }
        host->stats.video_errors++;
        return;
    }

    if (!libretro_video_store_frame(&host->video, data, width, height, pitch, host->pixel_format))
    {
        if (host->stats.video_errors == 0)
        {
            host_log(LogLevel::error, "Could not store a %ux%u video frame", width, height);
        }
        host->stats.video_errors++;
        return;
    }
    host->stats.video_frames++;
}

// Both audio callbacks copy into host->audio (the core's pointer is only valid
// during the call). The frontend plays it after retro_run returns.
static void store_audio(LibretroHost* host, const int16_t* samples, size_t frames)
{
    if (!libretro_audio_append(&host->audio, samples, frames))
    {
        if (host->stats.audio_errors == 0)
        {
            host_log(LogLevel::error, "Could not store %zu audio frames (out of memory)", frames);
        }
        host->stats.audio_errors++;
        return;
    }
    host->stats.audio_frames += frames;
}

static void RETRO_CALLCONV audio_sample_callback(int16_t left, int16_t right)
{
    LibretroHost* host = g_current_host;
    if (host == nullptr)
    {
        host_log(LogLevel::error, "audio_sample called with no current instance");
        return;
    }
    int16_t frame[2] = { left, right };
    store_audio(host, frame, 1);
}

static size_t RETRO_CALLCONV audio_sample_batch_callback(const int16_t* data, size_t frames)
{
    LibretroHost* host = g_current_host;
    if (host == nullptr)
    {
        host_log(LogLevel::error, "audio_sample_batch called with no current instance");
        return 0;
    }
    if (data == nullptr)
    {
        return 0;
    }
    store_audio(host, data, frames);

    // We always report everything as consumed, even if storing failed: the
    // return value is meant for cores that pace themselves on it, and telling
    // them "0" would make some of them retry forever inside retro_run.
    return frames;
}

static void RETRO_CALLCONV input_poll_callback(void)
{
    // The frontend already handed us this frame's input (libretro_host_set_input).
    LibretroHost* host = g_current_host;
    if (host != nullptr)
    {
        host->stats.input_polls++;
    }
}

static int16_t joypad_state(const LibretroInputState* input, unsigned port, unsigned id)
{
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    {
        // All 16 buttons at once (we answered yes to GET_INPUT_BITMASKS).
        // int16_t holds 16 bits; the core reads them back as a bitmask.
        return static_cast<int16_t>(input->joypad_buttons[port]);
    }
    if (id < 16)
    {
        return (input->joypad_buttons[port] & (1u << id)) != 0 ? 1 : 0;
    }
    return 0;
}

static int16_t analog_state(const LibretroInputState* input, unsigned port, unsigned index, unsigned id)
{
    if ((index == RETRO_DEVICE_INDEX_ANALOG_LEFT || index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) &&
        (id == RETRO_DEVICE_ID_ANALOG_X || id == RETRO_DEVICE_ID_ANALOG_Y))
    {
        return input->analog[port][index][id];
    }
    if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON && id < 16)
    {
        // Analog buttons: we only have digital ones, so it's fully pressed or not.
        return (input->joypad_buttons[port] & (1u << id)) != 0 ? 0x7fff : 0;
    }
    return 0;
}

static int16_t pointer_state(const LibretroInputState* input, unsigned port, unsigned index, unsigned id)
{
    if (port != 0 || index != 0 || !input->pointer_valid)
    {
        return 0;
    }
    switch (id)
    {
        case RETRO_DEVICE_ID_POINTER_X:       return input->pointer_x;
        case RETRO_DEVICE_ID_POINTER_Y:       return input->pointer_y;
        case RETRO_DEVICE_ID_POINTER_PRESSED: return input->pointer_pressed ? 1 : 0;
        case RETRO_DEVICE_ID_POINTER_COUNT:   return input->pointer_pressed ? 1 : 0;
        default:                              return 0;
    }
}

static int16_t RETRO_CALLCONV input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id)
{
    LibretroHost* host = g_current_host;
    if (host == nullptr || port >= LIBRETRO_MAX_PORTS)
    {
        return 0;
    }

    // Cores may ask for subclassed devices (e.g. a specific pad type); the low bits are the base device.
    switch (device & RETRO_DEVICE_MASK)
    {
        case RETRO_DEVICE_JOYPAD:  return joypad_state(&host->input, port, id);
        case RETRO_DEVICE_ANALOG:  return analog_state(&host->input, port, index, id);
        case RETRO_DEVICE_POINTER: return pointer_state(&host->input, port, index, id);
        // One keyboard for every port, like other frontends.
        case RETRO_DEVICE_KEYBOARD: return id < LIBRETRO_KEY_COUNT ? static_cast<int16_t>(host->input.keys[id]) : 0;
        default:                   return 0;   // mouse, lightgun: later
    }
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

static const char* safe_string(const char* text)
{
    return text != nullptr ? text : "(null)";
}

// ---------------------------------------------------------------------------
// Per-game files (SRAM, RTC, later savestates)
// ---------------------------------------------------------------------------

bool libretro_core_short_name(const char* core_path, char* out, size_t out_size)
{
    if (!host_fs_stem(core_path, out, out_size))
    {
        return false;
    }
    static const char SUFFIX[] = "_libretro";
    size_t length = std::strlen(out);
    size_t suffix_length = sizeof(SUFFIX) - 1;
    if (length > suffix_length && std::strcmp(out + length - suffix_length, SUFFIX) == 0)
    {
        out[length - suffix_length] = '\0';
    }
    return true;
}

// <directory>/<core>/<game><extension>. The game is the content file's
// name without extension, or the core's name when it runs without content.
// False if `directory` is empty or the path doesn't fit.
static bool game_file_path(const LibretroHost* host, const char* directory, const char* extension, char* out, size_t out_size)
{
    char core_name[256] = {};
    char game_name[256] = {};
    if (directory[0] == '\0' || !libretro_core_short_name(host->core_path, core_name, sizeof(core_name)))
    {
        return false;
    }
    bool named = host->content_path[0] != '\0' ? host_fs_stem(host->content_path, game_name, sizeof(game_name))
                                                : libretro_core_short_name(host->core_path, game_name, sizeof(game_name));
    if (!named)
    {
        return false;
    }
    int written = std::snprintf(out, out_size, "%s/%s/%s%s", directory, core_name, game_name, extension);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

// The core memory regions we keep in files. libretro only offers a raw view
// of the core's memory (no slots, no versions): we copy it in and out as is.
struct MemoryRegion
{
    unsigned    id;
    const char* extension;
    const char* label;
};

static const MemoryRegion MEMORY_REGIONS[] = {
    { RETRO_MEMORY_SAVE_RAM, ".srm", "SRAM" },
    { RETRO_MEMORY_RTC,      ".rtc", "RTC" },
};

constexpr size_t MEMORY_REGION_COUNT = sizeof(MEMORY_REGIONS) / sizeof(MEMORY_REGIONS[0]);

// The core's buffer for a region, or nullptr (size 0) if it has none.
static void* memory_region(LibretroHost* host, unsigned id, size_t* size)
{
    host_enter(host);
    void* data = host->core.retro_get_memory_data(id);
    *size = host->core.retro_get_memory_size(id);
    host_leave(host);
    if (data == nullptr || *size == 0)
    {
        *size = 0;
        return nullptr;
    }
    return data;
}

// Right after retro_load_game: the memory pointers are valid from then on.
static void load_memory_regions(LibretroHost* host)
{
    for (size_t i = 0; i < MEMORY_REGION_COUNT; i++)
    {
        const MemoryRegion* region = &MEMORY_REGIONS[i];
        size_t size = 0;
        void* data = memory_region(host, region->id, &size);
        char path[LIBRETRO_PATH_MAX] = {};
        if (data == nullptr || !game_file_path(host, host->saves_directory, region->extension, path, sizeof(path)))
        {
            continue;
        }
        if (!host_fs_exists(path))
        {
            host_log(LogLevel::info, "%s: %zu bytes, no %s yet (new game)", region->label, size, path);
            continue;
        }

        uint8_t* file_data = nullptr;
        size_t file_size = 0;
        if (!host_fs_read_file(path, &file_data, &file_size))
        {
            continue;
        }
        // Sizes can legitimately differ (core update, another core's file):
        // copy what fits and say so rather than refuse.
        size_t copy = file_size < size ? file_size : size;
        std::memcpy(data, file_data, copy);
        delete[] file_data;
        if (file_size != size)
        {
            host_log(LogLevel::warning, "%s: %s has %zu bytes, the core has %zu; loaded %zu", region->label, path,
                     file_size, size, copy);
        }
        else
        {
            host_log(LogLevel::info, "%s: loaded %zu bytes from %s", region->label, size, path);
        }
    }
}

bool libretro_host_save_memory(LibretroHost* host)
{
    if (!host->game_loaded)
    {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < MEMORY_REGION_COUNT; i++)
    {
        const MemoryRegion* region = &MEMORY_REGIONS[i];
        size_t size = 0;
        void* data = memory_region(host, region->id, &size);
        char path[LIBRETRO_PATH_MAX] = {};
        if (data == nullptr || !game_file_path(host, host->saves_directory, region->extension, path, sizeof(path)))
        {
            continue;
        }
        if (host_fs_write_file(path, data, size))
        {
            host_log(LogLevel::info, "%s: wrote %zu bytes to %s", region->label, size, path);
        }
        else
        {
            ok = false;
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Savestates
// ---------------------------------------------------------------------------

bool libretro_host_state_path(const LibretroHost* host, uint32_t slot, char* out, size_t out_size)
{
    char extension[32] = {};
    std::snprintf(extension, sizeof(extension), ".state%u", slot);
    return game_file_path(host, host->states_directory, extension, out, out_size);
}

// The core's state size, or 0 if it can't save states (or nothing runs).
static size_t state_size(LibretroHost* host)
{
    if (!host->game_loaded)
    {
        return 0;
    }
    host_enter(host);
    size_t size = host->core.retro_serialize_size();
    host_leave(host);
    return size;
}

bool libretro_host_save_state(LibretroHost* host, uint32_t slot)
{
    char path[LIBRETRO_PATH_MAX] = {};
    if (slot >= LIBRETRO_STATE_SLOTS || !libretro_host_state_path(host, slot, path, sizeof(path)))
    {
        host_log(LogLevel::error, "Savestate: no slot %u (or no data directory)", slot);
        return false;
    }
    size_t size = state_size(host);
    if (size == 0)
    {
        host_log(LogLevel::error, "Savestate: the core doesn't support savestates (size 0)");
        return false;
    }

    uint8_t* buffer = new uint8_t[size];
    host_enter(host);
    bool serialized = host->core.retro_serialize(buffer, size);
    host_leave(host);

    bool ok = serialized && host_fs_write_file(path, buffer, size);
    delete[] buffer;
    if (!serialized)
    {
        host_log(LogLevel::error, "Savestate: retro_serialize failed");
        return false;
    }
    if (ok)
    {
        host_log(LogLevel::info, "Savestate: saved slot %u (%zu bytes) to %s", slot, size, path);
    }
    return ok;
}

bool libretro_host_load_state(LibretroHost* host, uint32_t slot)
{
    char path[LIBRETRO_PATH_MAX] = {};
    if (!host->game_loaded || slot >= LIBRETRO_STATE_SLOTS || !libretro_host_state_path(host, slot, path, sizeof(path)))
    {
        host_log(LogLevel::error, "Savestate: no slot %u (or nothing running, or no data directory)", slot);
        return false;
    }
    if (!host_fs_exists(path))
    {
        host_log(LogLevel::warning, "Savestate: slot %u is empty (%s)", slot, path);
        return false;
    }

    uint8_t* data = nullptr;
    size_t size = 0;
    if (!host_fs_read_file(path, &data, &size))
    {
        return false;
    }
    // The core decides whether the size is acceptable (some cores' sizes vary
    // between versions or with options); we only pass what the file holds.
    host_enter(host);
    bool ok = host->core.retro_unserialize(data, size);
    host_leave(host);
    delete[] data;

    if (ok)
    {
        host_log(LogLevel::info, "Savestate: loaded slot %u (%zu bytes) from %s", slot, size, path);
    }
    else
    {
        host_log(LogLevel::error, "Savestate: the core refused %s (retro_unserialize failed)", path);
    }
    return ok;
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
        !copy_path(host->save_directory, sizeof(host->save_directory), config->save_directory, "Save directory") ||
        !copy_path(host->saves_directory, sizeof(host->saves_directory), config->saves_directory, "Saves directory") ||
        !copy_path(host->states_directory, sizeof(host->states_directory), config->states_directory, "States directory"))
    {
        return false;
    }

    // Before set_environment: cores ask GET_CORE_OPTIONS_VERSION from inside it.
    host->core_options_version = config->core_options_version <= LIBRETRO_CORE_OPTIONS_VERSION_MAX
                                     ? config->core_options_version : LIBRETRO_CORE_OPTIONS_VERSION_MAX;
    host->language = config->language;
    // Also before set_environment: that's where cores declare their options.
    if (!libretro_options_set_saved(&host->options, config->saved_options, config->saved_option_count))
    {
        host_log(LogLevel::error, "Out of memory keeping %u saved core options", config->saved_option_count);
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

static void copy_text(char* destination, size_t size, const char* source)
{
    std::snprintf(destination, size, "%s", source != nullptr ? source : "");
}

bool libretro_host_peek_core(const char* core_path, LibretroCoreInfo* info)
{
    *info = LibretroCoreInfo{};

    // A full LibretroHost is ~20 KB of path buffers: keep it off the stack.
    LibretroHost* host = new LibretroHost{};
    host->peeking = true;
    host->core_options_version = LIBRETRO_CORE_OPTIONS_VERSION_MAX;   // so cores declare their newest options
    if (!copy_path(host->core_path, sizeof(host->core_path), core_path, "Core"))
    {
        copy_text(info->error, sizeof(info->error), "path too long");
        delete host;
        return false;
    }
    // The same library loaded twice is one library: its retro_set_environment
    // would run again on the live core. Checked by the OS, so a symlink or
    // "dir//core.so" can't slip past.
    if (dynamic_library_is_loaded(host->core_path))
    {
        copy_text(info->error, sizeof(info->error), "already loaded (running?): not peeked");
        info->already_loaded = true;
        delete host;
        return false;
    }
    if (!libretro_core_load(&host->core, host->core_path))
    {
        copy_text(info->error, sizeof(info->error), "not a usable libretro core (can't load it, or a retro_* symbol is missing)");
        delete host;
        return false;
    }

    host_enter(host);
    info->api_version = host->core.retro_api_version();
    if (info->api_version != RETRO_API_VERSION)
    {
        host_leave(host);
        std::snprintf(info->error, sizeof(info->error), "libretro API version %u, we support %u", info->api_version,
                      RETRO_API_VERSION);
        libretro_host_unload_core(host);
        delete host;
        return false;
    }
    host->core.retro_set_environment(environment_callback);
    retro_system_info system = {};
    host->core.retro_get_system_info(&system);
    host_leave(host);

    // Copy now: these strings belong to the core and go away when it's unloaded.
    copy_text(info->name, sizeof(info->name), system.library_name);
    copy_text(info->version, sizeof(info->version), system.library_version);
    copy_text(info->extensions, sizeof(info->extensions), system.valid_extensions);
    info->need_fullpath = system.need_fullpath;
    info->block_extract = system.block_extract;
    info->supports_no_game = host->supports_no_game;
    info->option_count = host->options.count;
    info->options_api = host->options.api_used;
    info->usable = true;

    // retro_init never ran, so this only frees the options and closes the library.
    libretro_host_unload_core(host);
    delete host;
    return true;
}

// Case-insensitive ASCII comparison of `length` characters.
static bool same_text_ignoring_case(const char* a, const char* b, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        char x = a[i];
        char y = b[i];
        x = (x >= 'A' && x <= 'Z') ? static_cast<char>(x - 'A' + 'a') : x;
        y = (y >= 'A' && y <= 'Z') ? static_cast<char>(y - 'A' + 'a') : y;
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

bool libretro_core_info_accepts(const LibretroCoreInfo* info, const char* content_path)
{
    if (!info->usable)
    {
        return false;
    }
    if (content_path == nullptr || content_path[0] == '\0')
    {
        return info->supports_no_game;
    }
    if (info->extensions[0] == '\0')
    {
        return true;   // the core didn't say: let it try
    }

    // The extension of the file name, not of a directory ("dir.v2/game").
    const char* dot = std::strrchr(content_path, '.');
    const char* slash = std::strrchr(content_path, '/');
    const char* backslash = std::strrchr(content_path, '\\');
    if (backslash != nullptr && (slash == nullptr || backslash > slash))
    {
        slash = backslash;
    }
    if (dot == nullptr || (slash != nullptr && dot < slash))
    {
        return false;
    }
    const char* extension = dot + 1;
    size_t extension_length = std::strlen(extension);

    // Walk "sfc|smc|swc": compare each entry with the content's extension.
    const char* entry = info->extensions;
    while (*entry != '\0')
    {
        const char* bar = std::strchr(entry, '|');
        size_t entry_length = bar != nullptr ? static_cast<size_t>(bar - entry) : std::strlen(entry);
        if (entry_length == extension_length && same_text_ignoring_case(entry, extension, entry_length))
        {
            return true;
        }
        if (bar == nullptr)
        {
            break;
        }
        entry = bar + 1;
    }
    return false;
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
        host_log(host->peeking ? LogLevel::debug : LogLevel::info, "Unloading core %s", host->core_path);
        libretro_core_unload(&host->core);
    }

    // The option list was declared by this core; it goes with it.
    libretro_options_free(&host->options);

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
            if (!host_fs_read_file(host->content_path, &host->content_data, &host->content_size))
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

    load_memory_regions(host);

    const retro_game_geometry* geometry = &host->av_info.geometry;
    host_log(LogLevel::info, "Video: base %ux%u, max %ux%u, aspect %.4f, %.4f fps",
             geometry->base_width, geometry->base_height, geometry->max_width, geometry->max_height,
             static_cast<double>(geometry->aspect_ratio), host->av_info.timing.fps);
    host_log(LogLevel::info, "Audio: %.1f Hz", host->av_info.timing.sample_rate);
    return true;
}

void libretro_host_reset(LibretroHost* host)
{
    if (!host->game_loaded)
    {
        return;
    }
    host_enter(host);
    host->core.retro_reset();
    host_leave(host);
    host_log(LogLevel::info, "Reset");
}

void libretro_host_unload_game(LibretroHost* host)
{
    if (host->game_loaded)
    {
        // Before retro_unload_game: after it, the memory pointers are gone.
        libretro_host_save_memory(host);

        host_enter(host);
        host->core.retro_unload_game();
        host_leave(host);
        host->game_loaded = false;
        host_log(LogLevel::info, "Content unloaded");
    }

    libretro_video_free(&host->video);
    libretro_audio_free(&host->audio);

    // Only now: the core may have pointed into this buffer until retro_unload_game().
    delete[] host->content_data;
    host->content_data = nullptr;
    host->content_size = 0;
    host->content_path[0] = '\0';
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

void libretro_host_set_input(LibretroHost* host, const LibretroInputState* input)
{
    host->input = *input;
}

bool libretro_host_keyboard_event(LibretroHost* host, bool down, unsigned keycode, uint32_t character, uint16_t modifiers)
{
    if (host->keyboard_callback == nullptr || !host->core_initialized)
    {
        return false;
    }
    // A call into the core like any other: the core may call back into us from it.
    host_enter(host);
    host->keyboard_callback(down, keycode, character, modifiers);
    host_leave(host);
    return true;
}

bool libretro_host_set_option(LibretroHost* host, const char* key, const char* value)
{
    return libretro_options_set(&host->options, key, value);
}

bool libretro_host_run_frame(LibretroHost* host)
{
    if (!host->game_loaded || host->shutdown_requested)
    {
        return false;
    }

    // host->audio holds only this frame's audio; the frontend consumed the last one.
    libretro_audio_clear(&host->audio);

    host_enter(host);
    host->core.retro_run();
    host_leave(host);

    host->stats.frames_run++;
    return true;
}
