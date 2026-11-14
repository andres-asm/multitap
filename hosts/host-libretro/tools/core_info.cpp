// libretro_core_info: loads a libretro core (and optionally content) through
// host-libretro, logs every environment call, prints system and AV info,
// optionally runs some frames headless (no window, no input), then unloads
// everything cleanly. A development tool for testing the host, not a frontend.
//
// Usage:
//   libretro_core_info [--verbose] [--frames N] [--system DIR] [--save DIR]
//                      [--options-version N] [--data-dir DIR] [--load-state N] [--save-state N]
//                      [--set KEY=VALUE]... [--saved KEY=VALUE]... <core> [content]
//
// Without content, cores that support it are started with no game.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "host_log.h"
#include "libretro_host.h"

static void print_usage()
{
    std::fprintf(stderr,
                 "usage: libretro_core_info [--verbose] [--frames N] [--system DIR] [--save DIR]\n"
                 "                          [--options-version N] [--data-dir DIR] [--load-state N] [--save-state N]\n"
                 "                          [--set KEY=VALUE]... [--saved KEY=VALUE]... <core> [content]\n"
                 "  --verbose            also log repeated environment calls and core debug output\n"
                 "  --frames N           run N frames after loading (headless, no input)\n"
                 "  --system DIR         directory returned for GET_SYSTEM_DIRECTORY (default: none)\n"
                 "  --save DIR           directory returned for GET_SAVE_DIRECTORY (default: none)\n"
                 "  --options-version N  core options version to report: 2, 1 or 0 (default 2)\n"
                 "  --data-dir DIR       SRAM in DIR/saves, savestates in DIR/states (default: none)\n"
                 "  --peek               only ask the core about itself (no retro_init), print it, exit\n"
                 "  --load-state N       load savestate slot N before running frames\n"
                 "  --save-state N       save savestate slot N after running frames\n"
                 "  --set KEY=VALUE      change a core option after loading the game (repeatable)\n"
                 "  --saved KEY=VALUE    a saved core option value, applied when the core declares it (repeatable)\n");
}

// Prints every option the core declared, one line each, in a fixed format that
// tests can match: "Option KEY = VALUE (v1|v2|...) [category] [hidden]".
static void print_options(const LibretroOptions* options)
{
    host_log(LogLevel::info, "Options: %u declared with API v%u, %u categories", options->count, options->api_used,
             options->category_count);
    for (uint32_t i = 0; i < options->count; i++)
    {
        const LibretroOption* option = &options->options[i];
        char values[512] = {};
        size_t used = 0;
        for (uint32_t v = 0; v < option->value_count && used < sizeof(values); v++)
        {
            int written = std::snprintf(values + used, sizeof(values) - used, "%s%s", v > 0 ? "|" : "", option->values[v]);
            if (written < 0)
            {
                break;
            }
            used += static_cast<size_t>(written);
        }
        host_log(LogLevel::info, "Option %s = %s (%s)%s%s%s%s", option->key, option->values[option->current_index], values,
                 option->category_key != nullptr ? " [" : "", option->category_key != nullptr ? option->category_key : "",
                 option->category_key != nullptr ? "]" : "", option->visible ? "" : " [hidden]");
    }
}

constexpr int MAX_OPTION_SETS = 16;

int main(int argc, char** argv)
{
    LibretroHostConfig config = {};
    std::string data_saves;    // --data-dir: the config points into these
    std::string data_states;
    const char* option_sets[MAX_OPTION_SETS] = {};
    bool peek_only = false;
    long load_state_slot = -1;   // -1 = don't
    long save_state_slot = -1;
    int option_set_count = 0;
    char saved_keys[MAX_OPTION_SETS][256] = {};
    LibretroOptionValue saved[MAX_OPTION_SETS] = {};
    int saved_count = 0;
    const char* core_path = nullptr;
    const char* content_path = nullptr;
    unsigned long frames_to_run = 0;

    for (int i = 1; i < argc; i++)
    {
        const char* argument = argv[i];

        if (std::strcmp(argument, "--verbose") == 0)
        {
            host_log_set_min_level(LogLevel::debug);
        }
        else if (std::strcmp(argument, "--frames") == 0 && i + 1 < argc)
        {
            i++;
            frames_to_run = std::strtoul(argv[i], nullptr, 10);
        }
        else if (std::strcmp(argument, "--system") == 0 && i + 1 < argc)
        {
            i++;
            config.system_directory = argv[i];
        }
        else if (std::strcmp(argument, "--save") == 0 && i + 1 < argc)
        {
            i++;
            config.save_directory = argv[i];
        }
        else if (std::strcmp(argument, "--options-version") == 0 && i + 1 < argc)
        {
            i++;
            config.core_options_version = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
        }
        else if (std::strcmp(argument, "--peek") == 0)
        {
            peek_only = true;
        }
        else if (std::strcmp(argument, "--load-state") == 0 && i + 1 < argc)
        {
            i++;
            load_state_slot = static_cast<long>(std::strtol(argv[i], nullptr, 10));
        }
        else if (std::strcmp(argument, "--save-state") == 0 && i + 1 < argc)
        {
            i++;
            save_state_slot = static_cast<long>(std::strtol(argv[i], nullptr, 10));
        }
        else if (std::strcmp(argument, "--data-dir") == 0 && i + 1 < argc)
        {
            i++;
            // <dir>/saves and <dir>/states, like a program folder.
            data_saves = argv[i];
            data_saves += "/saves";
            data_states = argv[i];
            data_states += "/states";
            config.saves_directory = data_saves.c_str();
            config.states_directory = data_states.c_str();
        }
        else if (std::strcmp(argument, "--set") == 0 && i + 1 < argc && option_set_count < MAX_OPTION_SETS)
        {
            i++;
            option_sets[option_set_count] = argv[i];
            option_set_count++;
        }
        else if (std::strcmp(argument, "--saved") == 0 && i + 1 < argc && saved_count < MAX_OPTION_SETS)
        {
            i++;
            const char* equals = std::strchr(argv[i], '=');
            size_t key_length = equals != nullptr ? static_cast<size_t>(equals - argv[i]) : 0;
            if (equals == nullptr || key_length == 0 || key_length >= sizeof(saved_keys[0]))
            {
                print_usage();
                return 2;
            }
            std::memcpy(saved_keys[saved_count], argv[i], key_length);
            saved[saved_count].key = saved_keys[saved_count];
            saved[saved_count].value = equals + 1;
            saved_count++;
        }
        else if (argument[0] == '-')
        {
            print_usage();
            return 2;
        }
        else if (core_path == nullptr)
        {
            core_path = argument;
        }
        else if (content_path == nullptr)
        {
            content_path = argument;
        }
        else
        {
            print_usage();
            return 2;
        }
    }

    if (core_path == nullptr)
    {
        print_usage();
        return 2;
    }

    if (peek_only)
    {
        LibretroCoreInfo info = {};
        if (!libretro_host_peek_core(core_path, &info))
        {
            host_log(LogLevel::error, "Peek: %s", info.error);
            return 1;
        }
        host_log(LogLevel::info, "Peek: name \"%s\", version \"%s\"", info.name, info.version);
        host_log(LogLevel::info, "Peek: extensions \"%s\", need_fullpath %s, block_extract %s, no game %s",
                 info.extensions, info.need_fullpath ? "yes" : "no", info.block_extract ? "yes" : "no",
                 info.supports_no_game ? "yes" : "no");
        host_log(LogLevel::info, "Peek: %u core options (API v%u), libretro API %u", info.option_count,
                 info.options_api, info.api_version);
        if (content_path != nullptr)
        {
            host_log(LogLevel::info, "Peek: %s %s", libretro_core_info_accepts(&info, content_path) ? "accepts" : "does NOT accept",
                     content_path);
        }
        return 0;
    }

    // LibretroHost holds a few path buffers (~16 KB); keep it off the stack.
    LibretroHost* host = new LibretroHost{};
    int exit_code = 0;

    config.saved_options = saved;
    config.saved_option_count = static_cast<uint32_t>(saved_count);
    if (!libretro_host_load_core(host, core_path, &config))
    {
        exit_code = 1;
    }
    else if (content_path != nullptr || host->supports_no_game)
    {
        if (!libretro_host_load_game(host, content_path))
        {
            exit_code = 1;
        }
    }
    else
    {
        host_log(LogLevel::info, "No content given and the core needs content: skipping retro_load_game");
    }

    if (host->core.library.handle != nullptr)
    {
        print_options(&host->options);
    }

    // --set KEY=VALUE: like a user changing an option in a menu while the game runs.
    for (int i = 0; exit_code == 0 && i < option_set_count; i++)
    {
        char key[256] = {};
        const char* equals = std::strchr(option_sets[i], '=');
        size_t key_length = equals != nullptr ? static_cast<size_t>(equals - option_sets[i]) : 0;
        if (equals == nullptr || key_length == 0 || key_length >= sizeof(key))
        {
            host_log(LogLevel::error, "--set needs KEY=VALUE, got '%s'", option_sets[i]);
            exit_code = 2;
            break;
        }
        std::memcpy(key, option_sets[i], key_length);
        if (!libretro_host_set_option(host, key, equals + 1))
        {
            exit_code = 1;
        }
    }

    if (exit_code == 0 && host->game_loaded && load_state_slot >= 0 &&
        !libretro_host_load_state(host, static_cast<uint32_t>(load_state_slot)))
    {
        exit_code = 1;
    }

    if (exit_code == 0 && host->game_loaded && frames_to_run > 0)
    {
        unsigned long frame = 0;
        for (frame = 0; frame < frames_to_run; frame++)
        {
            if (!libretro_host_run_frame(host))
            {
                break;
            }
        }
        const LibretroHostStats* stats = &host->stats;
        host_log(LogLevel::info, "Ran %llu frames: %llu video frames (%ux%u), %llu duplicates, %llu video errors, "
                                 "%llu audio frames, %llu input polls",
                 static_cast<unsigned long long>(stats->frames_run), static_cast<unsigned long long>(stats->video_frames),
                 host->video.width, host->video.height, static_cast<unsigned long long>(stats->video_duplicates),
                 static_cast<unsigned long long>(stats->video_errors), static_cast<unsigned long long>(stats->audio_frames),
                 static_cast<unsigned long long>(stats->input_polls));
        if (stats->video_errors > 0)
        {
            exit_code = 1;
        }
    }

    if (exit_code == 0 && host->game_loaded && save_state_slot >= 0 &&
        !libretro_host_save_state(host, static_cast<uint32_t>(save_state_slot)))
    {
        exit_code = 1;
    }

    libretro_host_unload_core(host);
    delete host;
    return exit_code;
}
