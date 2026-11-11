#include "libretro_environment.h"

#include <cstdarg>
#include <cstdio>

#include "host_log.h"
#include "libretro.h"
#include "libretro_host.h"

// ---------------------------------------------------------------------------
// Core log interface (RETRO_ENVIRONMENT_GET_LOG_INTERFACE)
// ---------------------------------------------------------------------------

// Doesn't need the current host, so it's safe for cores that log from their
// own threads.
static void RETRO_CALLCONV core_log(enum retro_log_level level, const char* format, ...)
{
    LogLevel our_level = LogLevel::info;
    switch (level)
    {
        case RETRO_LOG_DEBUG: our_level = LogLevel::debug;   break;
        case RETRO_LOG_INFO:  our_level = LogLevel::info;    break;
        case RETRO_LOG_WARN:  our_level = LogLevel::warning; break;
        case RETRO_LOG_ERROR: our_level = LogLevel::error;   break;
        default:              our_level = LogLevel::info;    break;
    }

    va_list args;
    va_start(args, format);
    host_log_va(our_level, "[core] ", format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* pixel_format_name(retro_pixel_format format)
{
    switch (format)
    {
        case RETRO_PIXEL_FORMAT_0RGB1555: return "0RGB1555";
        case RETRO_PIXEL_FORMAT_XRGB8888: return "XRGB8888";
        case RETRO_PIXEL_FORMAT_RGB565:   return "RGB565";
        default:                          return "invalid";
    }
}

// Directory strings: empty means "not defined", which libretro expresses as NULL.
static const char* directory_or_null(const char* directory)
{
    return directory[0] != '\0' ? directory : nullptr;
}

// ---------------------------------------------------------------------------
// The handler
// ---------------------------------------------------------------------------

// What happened to one environment call. The core only sees true/false
// (handled vs. the other two); the distinction is for our log.
enum class EnvironmentResult
{
    handled,         // understood and done                      -> core sees true
    not_supported,   // a command we don't implement (yet)       -> core sees false
    rejected,        // a command we implement, with bad data    -> core sees false, always logged as a warning
};

// For commands we handle that read or write `data`: a NULL pointer there is a
// core bug. We log it instead of crashing. (Commands we DON'T handle may pass
// NULL legitimately, e.g. GET_INPUT_BITMASKS, so they never get here.)
static EnvironmentResult null_data(char* note, size_t note_size)
{
    std::snprintf(note, note_size, "core passed NULL data");
    return EnvironmentResult::rejected;
}

// Handles the command and writes a short human-readable note for the log.
static EnvironmentResult handle_command(LibretroHost* host, uint32_t command, void* data, char* note, size_t note_size)
{
    switch (command)
    {
        // --- Basics ---------------------------------------------------------

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            retro_log_callback* callback = static_cast<retro_log_callback*>(data);
            callback->log = core_log;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            retro_pixel_format format = *static_cast<const retro_pixel_format*>(data);
            if (format != RETRO_PIXEL_FORMAT_0RGB1555 && format != RETRO_PIXEL_FORMAT_XRGB8888 &&
                format != RETRO_PIXEL_FORMAT_RGB565)
            {
                std::snprintf(note, note_size, "invalid format %d", static_cast<int>(format));
                return EnvironmentResult::rejected;
            }
            host->pixel_format = format;
            std::snprintf(note, note_size, "%s", pixel_format_name(format));
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // We can show the previous frame again when the core passes NULL to video_refresh.
            *static_cast<bool*>(data) = true;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_OVERSCAN:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // Deprecated. true = keep overscan (don't crop); cropping is a frontend scaling option.
            *static_cast<bool*>(data) = true;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_LANGUAGE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            host->supports_no_game = *static_cast<const bool*>(data);
            std::snprintf(note, note_size, "%s", host->supports_no_game ? "yes" : "no");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            host->performance_level = *static_cast<const unsigned*>(data);
            std::snprintf(note, note_size, "%u", host->performance_level);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SHUTDOWN:
        {
            host->shutdown_requested = true;
            return EnvironmentResult::handled;
        }

        // --- Paths ----------------------------------------------------------

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const char* directory = directory_or_null(host->system_directory);
            *static_cast<const char**>(data) = directory;
            std::snprintf(note, note_size, "%s", directory != nullptr ? directory : "(none)");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const char* directory = directory_or_null(host->save_directory);
            *static_cast<const char**>(data) = directory;
            std::snprintf(note, note_size, "%s", directory != nullptr ? directory : "(none)");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<const char**>(data) = host->core_path;
            return EnvironmentResult::handled;
        }

        // --- Messages -------------------------------------------------------

        case RETRO_ENVIRONMENT_SET_MESSAGE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const retro_message* message = static_cast<const retro_message*>(data);
            host_log(LogLevel::info, "[core message] %s", message->msg != nullptr ? message->msg : "");
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // 1 = we accept SET_MESSAGE_EXT.
            *static_cast<unsigned*>(data) = 1;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            const retro_message_ext* message = static_cast<const retro_message_ext*>(data);
            host_log(LogLevel::info, "[core message] %s", message->msg != nullptr ? message->msg : "");
            return EnvironmentResult::handled;
        }

        // --- Video geometry (sent by cores while running) -------------------

        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // Only base size and aspect may change here; max size is fixed until SET_SYSTEM_AV_INFO.
            const retro_game_geometry* geometry = static_cast<const retro_game_geometry*>(data);
            host->av_info.geometry.base_width   = geometry->base_width;
            host->av_info.geometry.base_height  = geometry->base_height;
            host->av_info.geometry.aspect_ratio = geometry->aspect_ratio;
            std::snprintf(note, note_size, "%ux%u", geometry->base_width, geometry->base_height);
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            host->av_info = *static_cast<const retro_system_av_info*>(data);
            std::snprintf(note, note_size, "%ux%u @ %.3f fps", host->av_info.geometry.base_width,
                          host->av_info.geometry.base_height, host->av_info.timing.fps);
            return EnvironmentResult::handled;
        }

        // --- Runtime state queries -----------------------------------------

        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            // bit 0 = video, bit 1 = audio. We always want both.
            *static_cast<int*>(data) = 3;
            return EnvironmentResult::handled;
        }

        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<bool*>(data) = false;
            return EnvironmentResult::handled;
        }

        // --- Input -----------------------------------------------------------

        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        {
            // Specified with NULL data: returning true IS the answer
            // ("RETRO_DEVICE_ID_JOYPAD_MASK works"). No data check on purpose.
            return EnvironmentResult::handled;
        }

        // --- Core options (not implemented yet) -----------------------------

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            retro_variable* variable = static_cast<retro_variable*>(data);
            std::snprintf(note, note_size, "%s (options not implemented yet)",
                          variable->key != nullptr ? variable->key : "?");
            variable->value = nullptr;
            return EnvironmentResult::not_supported;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            *static_cast<bool*>(data) = false;
            return EnvironmentResult::handled;
        }

        // --- Informational: accepted, stored later when a feature needs it --

        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        {
            if (data == nullptr)
            {
                return null_data(note, note_size);
            }
            std::snprintf(note, note_size, "accepted, not used yet");
            return EnvironmentResult::handled;
        }

        default:
        {
            return EnvironmentResult::not_supported;
        }
    }
}

bool libretro_environment_handle(LibretroHost* host, uint32_t command, void* data)
{
    char note[256] = {};
    EnvironmentResult result = EnvironmentResult::not_supported;

    // Each handled command checks `data` itself: some commands are specified with
    // NULL data (SHUTDOWN, GET_INPUT_BITMASKS...), so a blanket check is wrong.
    result = handle_command(host, command, data, note, sizeof(note));

    // The low 16 bits are the command number; EXPERIMENTAL (0x10000) and PRIVATE (0x20000) sit above.
    uint32_t index = command & 0xFFFFu;
    uint32_t previous_calls = 0;
    if (index < 256)
    {
        previous_calls = host->environment_call_counts[index];
        host->environment_call_counts[index] = previous_calls + 1;
    }

    // First call of each command: info. Repeats: debug, so per-frame calls
    // (GET_VARIABLE_UPDATE...) don't flood the log. Rejections: always a warning.
    LogLevel level = previous_calls == 0 ? LogLevel::info : LogLevel::debug;
    const char* outcome = "handled";
    if (result == EnvironmentResult::not_supported)
    {
        outcome = "NOT handled";
    }
    else if (result == EnvironmentResult::rejected)
    {
        outcome = "REJECTED";
        level = LogLevel::warning;
    }

    bool experimental = (command & RETRO_ENVIRONMENT_EXPERIMENTAL) != 0;
    host_log(level, "env %-34s (%3u%s) %-11s %s",
             libretro_environment_command_name(command),
             index,
             experimental ? ", exp" : "",
             outcome,
             note);

    return result == EnvironmentResult::handled;
}
