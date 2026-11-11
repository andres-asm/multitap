// libretro_core_info: loads a libretro core (and optionally content) through
// host-libretro, logs every environment call, prints system and AV info,
// optionally runs some frames headless (no window, no input), then unloads
// everything cleanly. A development tool for testing the host, not a frontend.
//
// Usage:
//   libretro_core_info [--verbose] [--frames N] [--system DIR] [--save DIR] <core> [content]
//
// Without content, cores that support it are started with no game.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "host_log.h"
#include "libretro_host.h"

static void print_usage()
{
    std::fprintf(stderr,
                 "usage: libretro_core_info [--verbose] [--frames N] [--system DIR] [--save DIR] <core> [content]\n"
                 "  --verbose     also log repeated environment calls and core debug output\n"
                 "  --frames N    run N frames after loading (headless, no input)\n"
                 "  --system DIR  directory returned for GET_SYSTEM_DIRECTORY (default: none)\n"
                 "  --save DIR    directory returned for GET_SAVE_DIRECTORY (default: none)\n");
}

int main(int argc, char** argv)
{
    LibretroHostConfig config = {};
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

    // LibretroHost holds a few path buffers (~16 KB); keep it off the stack.
    LibretroHost* host = new LibretroHost{};
    int exit_code = 0;

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

    libretro_host_unload_core(host);
    delete host;
    return exit_code;
}
