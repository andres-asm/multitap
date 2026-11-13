// Checks that the host refuses to peek a core whose library is already loaded,
// however its path is spelled (fix for review 2026-11-12, fix 1): peeking the
// running core would call retro_set_environment a second time on it.
//
// Usage: host_libretro_peek_loaded_test <core> <same core, other spelling>...
//   1. load <core> in a host and run a few frames;
//   2. peek every other spelling: must fail with already_loaded;
//   3. the loaded core must still run frames;
//   4. after unloading, peeking those spellings must work again.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>

#include "libretro_host.h"

static int g_failures = 0;

static void check(bool condition, const char* what, const char* detail)
{
    if (!condition)
    {
        std::printf("FAIL %s (%s)\n", what, detail);
        g_failures++;
    }
}

static bool run_frames(LibretroHost* host, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (!libretro_host_run_frame(host))
        {
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: %s <core> <other spelling of the same core>...\n", argv[0]);
        return 2;
    }

    // ~20 KB of path buffers: on the heap.
    LibretroHost* host = new LibretroHost{};
    LibretroHostConfig config = {};
    bool loaded = libretro_host_load_core(host, argv[1], &config) && libretro_host_load_game(host, nullptr);
    check(loaded, "load the core", argv[1]);
    check(loaded && run_frames(host, 5), "run frames before peeking", argv[1]);

    for (int i = 2; i < argc; i++)
    {
        LibretroCoreInfo info = {};
        bool peeked = libretro_host_peek_core(argv[i], &info);
        check(!peeked, "peek of a loaded core refused", argv[i]);
        check(info.already_loaded, "refusal says already_loaded", argv[i]);
        std::printf("peek %s while loaded: %s\n", argv[i], peeked ? "allowed" : info.error);
    }

    check(loaded && run_frames(host, 5), "the loaded core still runs frames after the refused peeks", argv[1]);

    libretro_host_unload_core(host);
    delete host;

    for (int i = 1; i < argc; i++)
    {
        LibretroCoreInfo info = {};
        bool peeked = libretro_host_peek_core(argv[i], &info);
        check(peeked && !info.already_loaded, "peek after unload works", argv[i]);
        std::printf("peek %s after unload: %s\n", argv[i], peeked ? info.name : info.error);
    }

    if (g_failures == 0)
    {
        std::printf("peek loaded: all checks passed\n");
        return 0;
    }
    std::printf("peek loaded: %d check(s) failed\n", g_failures);
    return 1;
}
