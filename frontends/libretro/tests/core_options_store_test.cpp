// Checks core_options_store: layers core -> folder -> game, only differences
// written, the effective list for the host, and the manager functions.
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <string>

#include "core_options_store.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static bool same(const char* a, const char* b)
{
    return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
}

static void test_layers()
{
    CoreOptionsStore store = {};
    check(core_options_store_set_context(&store, "mgba", "Game Boy", "Tetris"), "context set");
    check(core_options_store_section(&store, CoreOptionLayer::directory)->compare("mgba/dir:Game Boy") == 0,
          "folder section name");
    check(core_options_store_section(&store, CoreOptionLayer::game)->compare("mgba/game:Tetris") == 0,
          "game section name");

    check(core_options_store_get(&store, "mgba_palette", nullptr) == nullptr, "nothing stored: the default");
    core_options_store_set(&store, CoreOptionLayer::core, "mgba_palette", "grey", "green");
    core_options_store_set(&store, CoreOptionLayer::directory, "mgba_palette", "amber", "green");
    CoreOptionLayer source = CoreOptionLayer::core;
    check(same(core_options_store_get(&store, "mgba_palette", &source), "amber") && source == CoreOptionLayer::directory,
          "the folder wins over the core");
    check(same(core_options_store_get_at(&store, CoreOptionLayer::core, "mgba_palette", nullptr), "grey"),
          "at the core layer the folder isn't seen");

    // Setting the game to what the folder gives writes nothing.
    core_options_store_set(&store, CoreOptionLayer::game, "mgba_palette", "amber", "green");
    check(core_options_store_get_layer(&store, CoreOptionLayer::game, "mgba_palette") == nullptr,
          "a value equal to the layer below isn't written");
    core_options_store_set(&store, CoreOptionLayer::game, "mgba_palette", "blue", "green");
    check(same(core_options_store_get(&store, "mgba_palette", &source), "blue") && source == CoreOptionLayer::game,
          "the game wins");
    // Back to the default at the core layer: removed.
    core_options_store_set(&store, CoreOptionLayer::core, "mgba_palette", "green", "green");
    check(core_options_store_get_layer(&store, CoreOptionLayer::core, "mgba_palette") == nullptr,
          "the default isn't written at the core layer");
    check(core_options_store_reset(&store, CoreOptionLayer::game, "mgba_palette"), "reset removes the game value");
    check(same(core_options_store_get(&store, "mgba_palette", nullptr), "amber"), "and the folder applies again");
    check(store.dirty, "changes mark the store dirty");

    // Without content there are no folder / game layers.
    core_options_store_set_context(&store, "mgba", "", "");
    check(!core_options_store_set(&store, CoreOptionLayer::game, "mgba_palette", "blue", "green"),
          "no game layer without content");
    check(core_options_store_get(&store, "mgba_palette", nullptr) == nullptr, "the folder's value is not for other content");
    check(!core_options_store_set_context(&store, "bad/name", "", ""), "a core name with '/' is refused");
}

static void test_effective_and_manager()
{
    CoreOptionsStore store = {};
    const char* text = "[snes9x]\nsnes9x_region = pal\nsnes9x_overscan = enabled\n"
                       "[snes9x/game:Super Mario World (USA)]\nsnes9x_region = ntsc\n"
                       "[snes9x/dir:Super Nintendo]\nsnes9x_overclock = 1.5x\n"
                       "[mgba]\nmgba_palette = grey\n";
    settings_file_parse(&store.file, text, std::strlen(text), "test");
    core_options_store_set_context(&store, "snes9x", "Super Nintendo", "Super Mario World (USA)");
    std::vector<LibretroOptionValue> values;
    core_options_store_effective(&store, &values);
    check(values.size() == 3, "three effective values (one key overridden)");
    bool region_ntsc = false;
    for (size_t i = 0; i < values.size(); i++)
    {
        region_ntsc = region_ntsc || (same(values[i].key, "snes9x_region") && same(values[i].value, "ntsc"));
        check(std::strncmp(values[i].key, "mgba", 4) != 0, "another core's values aren't included");
    }
    check(region_ntsc, "the most specific value is the effective one");
    check(core_options_store_layer_has_values(&store, CoreOptionLayer::game), "the game layer has values");

    std::vector<std::string> sections;
    core_options_store_sections_of(&store, "snes9x", &sections);
    check(sections.size() == 3, "every section of the core is listed");
    core_options_store_sections_of(&store, "snes", &sections);
    check(sections.empty(), "a core whose name is a prefix doesn't match");

    check(core_options_store_remove(&store, "snes9x", "snes9x_overscan"), "remove one key");
    check(core_options_store_remove_section(&store, "snes9x/game:Super Mario World (USA)"), "remove a section");
    check(same(core_options_store_get(&store, "snes9x_region", nullptr), "pal"), "the core value applies after removing the game's");
    check(!core_options_store_remove_section(&store, "nope"), "removing a missing section says so");
}

int main()
{
    test_layers();
    test_effective_and_manager();
    if (g_failures == 0)
    {
        std::printf("core options store: all checks passed\n");
        return 0;
    }
    std::printf("core options store: %d check(s) failed\n", g_failures);
    return 1;
}
