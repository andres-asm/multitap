// A minimal libretro core used to test host-libretro. Not an emulator.
//
// It behaves like a typical real core during startup:
//   - calls the environment from inside retro_set_environment (log interface,
//     SET_SUPPORT_NO_GAME, SET_VARIABLES) like many cores do,
//   - asks for directories and a pixel format in retro_init,
//   - accepts content from memory and checks it arrived intact,
//   - reports a 320x240 @ 60 fps, 48 kHz AV info.
//
// Build variants (CMake):
//   test_core                 the normal core
//   test_core_missing_symbol  built with TEST_CORE_OMIT_RUN: no retro_run export,
//                             so the host must refuse to load it.
//
// Global state is normal for a libretro core (the API has no instance
// pointer), so this file uses file-local statics on purpose.

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "libretro.h"

static retro_environment_t g_environment = nullptr;
static retro_log_printf_t  g_log         = nullptr;

static void core_log(retro_log_level level, const char* format, ...)
{
    if (g_log == nullptr)
    {
        return;
    }
    char message[512] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    g_log(level, "%s", message);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void retro_set_environment(retro_environment_t callback)
{
    g_environment = callback;

    retro_log_callback log_callback = {};
    if (g_environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_callback))
    {
        g_log = log_callback.log;
    }

    bool no_game = true;
    g_environment(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);

    // Old-style core options. The host doesn't implement options yet, so this
    // should be logged as "NOT handled" and the core must cope.
    static const retro_variable variables[] = {
        { "test_core_speed", "Speed; normal|fast" },
        { nullptr, nullptr },
    };
    g_environment(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable*>(variables));
}

void retro_set_video_refresh(retro_video_refresh_t callback)            { (void)callback; }
void retro_set_audio_sample(retro_audio_sample_t callback)              { (void)callback; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback)  { (void)callback; }
void retro_set_input_poll(retro_input_poll_t callback)                  { (void)callback; }
void retro_set_input_state(retro_input_state_t callback)                { (void)callback; }

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(retro_system_info* info)
{
    info->library_name     = "multitap test core";
    info->library_version  = "1";
    info->valid_extensions = "bin|txt";
    info->need_fullpath    = false;
    info->block_extract    = false;
}

void retro_init(void)
{
    const char* system_directory = nullptr;
    g_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory);
    core_log(RETRO_LOG_INFO, "system directory: %s\n", system_directory != nullptr ? system_directory : "(none)");

    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!g_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format))
    {
        core_log(RETRO_LOG_ERROR, "XRGB8888 refused\n");
    }

    // An unsupported pixel format value must be refused.
    retro_pixel_format bogus = static_cast<retro_pixel_format>(77);
    if (g_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &bogus))
    {
        core_log(RETRO_LOG_ERROR, "host accepted an invalid pixel format\n");
    }

    retro_variable variable = { "test_core_speed", nullptr };
    if (!g_environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable))
    {
        core_log(RETRO_LOG_INFO, "no value for test_core_speed, using default\n");
    }

    // A command number nobody defines: must come back "not handled".
    g_environment(9999, nullptr);

    // Specified with NULL data (the return value is the answer): must NOT be
    // reported as a core bug. Regression test for a host bug found 2026-11-11.
    g_environment(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, nullptr);

    // A command that needs data, sent with NULL: the host must refuse it, not crash.
    if (g_environment(RETRO_ENVIRONMENT_GET_CAN_DUPE, nullptr))
    {
        core_log(RETRO_LOG_ERROR, "host accepted GET_CAN_DUPE with NULL data\n");
    }
}

void retro_deinit(void)
{
    core_log(RETRO_LOG_INFO, "deinit\n");
    g_environment = nullptr;
    g_log = nullptr;
}

// ---------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------

bool retro_load_game(const retro_game_info* game)
{
    if (game == nullptr)
    {
        core_log(RETRO_LOG_INFO, "started without content\n");
        return true;
    }

    if (game->data == nullptr)
    {
        core_log(RETRO_LOG_ERROR, "need_fullpath is false but no data was passed\n");
        return false;
    }

    // The test content starts with this marker; anything else is rejected,
    // which also exercises the host's "load failed" path.
    static const char marker[] = "MULTITAP";
    if (game->size < sizeof(marker) - 1 || std::memcmp(game->data, marker, sizeof(marker) - 1) != 0)
    {
        core_log(RETRO_LOG_ERROR, "content does not start with MULTITAP (%zu bytes)\n", game->size);
        return false;
    }

    core_log(RETRO_LOG_INFO, "content OK: %s, %zu bytes\n", game->path != nullptr ? game->path : "(no path)", game->size);

    const char* message_text = "test core: content loaded";
    retro_message message = { message_text, 60 };
    g_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
    return true;
}

bool retro_load_game_special(unsigned type, const retro_game_info* info, size_t count)
{
    (void)type;
    (void)info;
    (void)count;
    return false;
}

void retro_unload_game(void)
{
    core_log(RETRO_LOG_INFO, "unload game\n");
}

void retro_get_system_av_info(retro_system_av_info* info)
{
    info->geometry.base_width   = 320;
    info->geometry.base_height  = 240;
    info->geometry.max_width    = 320;
    info->geometry.max_height   = 240;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = 48000.0;
}

// ---------------------------------------------------------------------------
// Everything else: required exports, not used by the tests yet
// ---------------------------------------------------------------------------

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

void retro_reset(void)
{
}

#if !defined(TEST_CORE_OMIT_RUN)
void retro_run(void)
{
}
#endif

size_t retro_serialize_size(void)
{
    return 0;
}

bool retro_serialize(void* data, size_t size)
{
    (void)data;
    (void)size;
    return false;
}

bool retro_unserialize(const void* data, size_t size)
{
    (void)data;
    (void)size;
    return false;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void* retro_get_memory_data(unsigned id)
{
    (void)id;
    return nullptr;
}

size_t retro_get_memory_size(unsigned id)
{
    (void)id;
    return 0;
}
