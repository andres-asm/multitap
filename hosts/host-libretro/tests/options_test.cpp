// Checks libretro_options: all three ways a core declares options (v0, v1, v2)
// must end up as the same list, with the right defaults; bad definitions are
// skipped, not fatal; values carry over when a core declares its options again;
// set/get/update/visibility behave as GET_VARIABLE(_UPDATE) and
// SET_CORE_OPTIONS_DISPLAY need.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>

#include "libretro_options.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static bool value_is(const LibretroOptions* options, const char* key, const char* expected)
{
    const char* value = libretro_options_get(options, key);
    return value != nullptr && std::strcmp(value, expected) == 0;
}

static void test_v0()
{
    LibretroOptions options = {};
    retro_variable variables[] = {
        { "speed", "Speed; normal|fast|slow" },
        { "broken", "no separator here" },          // skipped, the rest still works
        { "empty", "Empty; " },                      // no values: skipped
        { "speed", "Speed again; a|b" },             // duplicate key: skipped
        { nullptr, nullptr },
    };

    check(libretro_options_register_v0(&options, variables), "v0: register");
    check(options.count == 1, "v0: only the valid, first 'speed' kept");
    check(options.api_used == 0, "v0: api_used");
    check(value_is(&options, "speed", "normal"), "v0: first value is the default");

    const LibretroOption* speed = libretro_options_find(&options, "speed");
    check(speed != nullptr && std::strcmp(speed->label, "Speed") == 0, "v0: label is the text before ';'");
    check(speed != nullptr && speed->value_count == 3, "v0: three values");
    check(speed != nullptr && std::strcmp(speed->values[2], "slow") == 0, "v0: last value parsed");
    check(libretro_options_find(&options, "broken") == nullptr, "v0: malformed entry skipped");

    libretro_options_free(&options);
}

static void test_v1()
{
    LibretroOptions options = {};
    retro_core_option_definition definitions[3] = {};
    definitions[0].key = "region";
    definitions[0].desc = "Region";
    definitions[0].info = "Console region";
    definitions[0].values[0] = { "auto", "Automatic" };
    definitions[0].values[1] = { "ntsc", "NTSC" };
    definitions[0].values[2] = { "pal", nullptr };
    definitions[0].default_value = "ntsc";
    definitions[1].key = "bad_default";
    definitions[1].desc = "Bad default";
    definitions[1].values[0] = { "x", nullptr };
    definitions[1].values[1] = { "y", nullptr };
    definitions[1].default_value = "z";   // not a value: falls back to the first

    check(libretro_options_register_v1(&options, definitions), "v1: register");
    check(options.count == 2 && options.api_used == 1, "v1: count and api");
    check(value_is(&options, "region", "ntsc"), "v1: explicit default");
    check(value_is(&options, "bad_default", "x"), "v1: unknown default -> first value");

    const LibretroOption* region = libretro_options_find(&options, "region");
    check(region != nullptr && std::strcmp(region->value_labels[0], "Automatic") == 0, "v1: value label copied");
    check(region != nullptr && region->value_labels[2] == nullptr, "v1: missing value label stays nullptr");
    check(region != nullptr && region->category_key == nullptr, "v1: no category");

    libretro_options_free(&options);
}

static void test_v2_and_changes()
{
    LibretroOptions options = {};
    retro_core_option_v2_category categories[2] = {};
    categories[0] = { "video", "Video", "Picture settings" };

    retro_core_option_v2_definition definitions[3] = {};
    definitions[0].key = "width";
    definitions[0].desc = "Video > Width";
    definitions[0].desc_categorized = "Width";
    definitions[0].category_key = "video";
    definitions[0].values[0] = { "640", nullptr };
    definitions[0].values[1] = { "720", nullptr };
    definitions[0].default_value = "640";
    definitions[1].key = "turbo";
    definitions[1].desc = "Turbo";
    definitions[1].values[0] = { "off", nullptr };
    definitions[1].values[1] = { "on", nullptr };
    definitions[1].default_value = "off";

    retro_core_options_v2 v2 = { categories, definitions };
    check(libretro_options_register_v2(&options, &v2), "v2: register");
    check(options.count == 2 && options.category_count == 1 && options.api_used == 2, "v2: counts and api");
    check(std::strcmp(options.categories[0].label, "Video") == 0, "v2: category label");

    const LibretroOption* width = libretro_options_find(&options, "width");
    check(width != nullptr && std::strcmp(width->category_key, "video") == 0, "v2: category key");
    check(width != nullptr && std::strcmp(width->label_in_category, "Width") == 0, "v2: label inside category");

    // A fresh list is not an "update".
    check(!libretro_options_take_update(&options), "no update after registering");

    // Changes from the frontend.
    check(libretro_options_set(&options, "width", "720"), "set width 720");
    check(value_is(&options, "width", "720"), "get after set");
    check(libretro_options_take_update(&options), "update flag set by a change");
    check(!libretro_options_take_update(&options), "update flag cleared after reading");
    check(libretro_options_set(&options, "width", "720"), "setting the same value is accepted");
    check(!libretro_options_take_update(&options), "same value: no update");
    check(!libretro_options_set(&options, "width", "800"), "value not in the list refused");
    check(!libretro_options_set(&options, "nope", "1"), "unknown key refused");
    check(value_is(&options, "width", "720"), "refused sets change nothing");

    // Visibility.
    check(libretro_options_set_visible(&options, "turbo", false), "hide turbo");
    check(!libretro_options_find(&options, "turbo")->visible, "turbo hidden");
    check(!libretro_options_set_visible(&options, "nope", false), "hide unknown key refused");

    // The core declares its options again (some do, e.g. after loading content):
    // the user's value and visibility survive when still valid.
    check(libretro_options_register_v2(&options, &v2), "v2: register again");
    check(value_is(&options, "width", "720"), "value carried over on re-register");
    check(!libretro_options_find(&options, "turbo")->visible, "visibility carried over on re-register");

    // ...but not when the value no longer exists.
    definitions[0].values[1] = { "768", nullptr };
    check(libretro_options_register_v2(&options, &v2), "v2: register with changed values");
    check(value_is(&options, "width", "640"), "vanished value -> default");

    libretro_options_free(&options);
    check(options.count == 0 && options.options == nullptr, "free resets");
    check(libretro_options_get(&options, "width") == nullptr, "get after free");
}

int main()
{
    test_v0();
    test_v1();
    test_v2_and_changes();

    if (g_failures == 0)
    {
        std::printf("options: all checks passed\n");
        return 0;
    }
    std::printf("options: %d check(s) failed\n", g_failures);
    return 1;
}
