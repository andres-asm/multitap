// Checks core_options_cache: what a core declares (through the host's own
// option registration) is kept and read back without the core, re-declaring
// the same is not a change, and another core's entries are left alone.
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <string>

#include "core_options_cache.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static retro_core_option_v2_category CATEGORIES[] = {
    {"system", "System", "Console settings."},
    {nullptr, nullptr, nullptr},
};

static retro_core_option_v2_definition DEFINITIONS[] = {
    {"snes9x_region", "System > Console Region", "Console Region", "Which console to emulate.\nAuto: from the game.",
     nullptr, "system", {{"auto", "Auto"}, {"ntsc", "NTSC"}, {"pal", "PAL"}, {nullptr, nullptr}}, "auto"},
    {"snes9x_overclock", "SuperFX Overclock", nullptr, nullptr, nullptr, nullptr,
     {{"1x", nullptr}, {"2x", nullptr}, {nullptr, nullptr}}, "1x"},
    {"snes9x_bad", "Bad", nullptr, nullptr, nullptr, nullptr, {{" spaced", nullptr}, {nullptr, nullptr}}, " spaced"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {{nullptr, nullptr}}, nullptr},
};

int main()
{
    LibretroOptions options = {};
    retro_core_options_v2 declared = {CATEGORIES, DEFINITIONS};
    check(libretro_options_register_v2(&options, &declared), "the host takes the definitions");

    CoreOptionsCache cache = {};
    core_options_cache_update(&cache, "mgba", &options);   // another core, to check it's left alone
    cache.dirty = false;
    core_options_cache_update(&cache, "snes9x", &options);
    check(cache.dirty, "a new core's options change the cache");

    std::vector<CachedCoreOption> cached;
    core_options_cache_get(&cache, "snes9x", &cached);
    check(cached.size() == 2, "two options cached (the one with unstorable values skipped)");
    if (cached.size() == 2)
    {
        const CachedCoreOption* region = &cached[0];
        check(region->key == "snes9x_region", "declared order kept");
        check(region->label == "System > Console Region", "label");
        check(region->category == "System", "category label");
        check(region->info == "Which console to emulate. Auto: from the game.", "info with line breaks flattened");
        check(region->default_value == "auto", "default");
        check(region->values.size() == 3 && region->values[2] == "pal", "values");
        check(region->labels.size() == 3 && region->labels[1] == "NTSC", "value labels");
        check(cached[1].category.empty() && cached[1].labels[0].empty(), "no category, no labels");
    }

    // The text survives a save / load round trip.
    std::string text;
    settings_file_to_text(&cache.file, nullptr, &text);
    CoreOptionsCache reloaded = {};
    settings_file_parse(&reloaded.file, text.c_str(), text.size(), "test");
    std::vector<CachedCoreOption> again;
    core_options_cache_get(&reloaded, "snes9x", &again);
    check(again.size() == 2 && again[0].labels[2] == "PAL", "read back the same from the file");

    cache.dirty = false;
    core_options_cache_update(&cache, "snes9x", &options);
    check(!cache.dirty, "declaring the same again is not a change");

    std::vector<std::string> cores;
    core_options_cache_cores(&cache, &cores);
    check(cores.size() == 2, "both cores listed");
    check(core_options_cache_has(&cache, "snes9x", "snes9x_overclock"), "has a declared key");
    check(!core_options_cache_has(&cache, "snes9x", "snes9x_gone"), "doesn't have an undeclared one");

    // Fewer options now: the core's entries are replaced, mgba's kept.
    DEFINITIONS[1] = DEFINITIONS[3];
    check(libretro_options_register_v2(&options, &declared), "re-declared with fewer options");
    core_options_cache_update(&cache, "snes9x", &options);
    core_options_cache_get(&cache, "snes9x", &cached);
    check(cache.dirty && cached.size() == 1, "re-declaring fewer options replaces them");
    core_options_cache_get(&cache, "mgba", &cached);
    check(cached.size() == 2, "another core's entries are untouched");

    libretro_options_free(&options);
    if (g_failures == 0)
    {
        std::printf("core options cache: all checks passed\n");
        return 0;
    }
    std::printf("core options cache: %d check(s) failed\n", g_failures);
    return 1;
}
