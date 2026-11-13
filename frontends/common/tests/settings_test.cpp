// Checks the layered settings store (settings.h): defaults, global -> core ->
// game, sources, validation, global-only settings, reset, load/save.
//
// Usage: frontend_settings_test <scratch directory>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <string>

#include "host_fs.h"
#include "settings.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static const char* layer_text(SettingLayer layer)
{
    switch (layer)
    {
    case SettingLayer::defaults:
        return "defaults";
    case SettingLayer::global:
        return "global";
    case SettingLayer::core:
        return "core";
    case SettingLayer::game:
        return "game";
    }
    return "?";
}

// The effective value and where it comes from.
static void check_effective(const Settings* settings, const char* key, const char* expected, SettingLayer expected_source,
                            const char* what)
{
    SettingLayer source = SettingLayer::defaults;
    const char* value = settings_get(settings, key, &source);
    bool same = value != nullptr && std::strcmp(value, expected) == 0 && source == expected_source;
    if (!same)
    {
        std::printf("FAIL %s: %s expected \"%s\" from %s, got \"%s\" from %s\n", what, key, expected,
                    layer_text(expected_source), value != nullptr ? value : "(null)", layer_text(source));
        g_failures++;
    }
}

static const SettingEnumValue SCALING_VALUES[] = {
    {"full", "Full"},
    {"integer_under", "Integer (under)"},
    {"integer_over", "Integer (over)"},
    {"auto", "Auto"},
};

static const SettingDescriptor DESCRIPTORS[] = {
    {.key = "video_scaling", .type = SettingType::enumeration, .default_value = "auto", .overridable = true,
     .values = SCALING_VALUES, .value_count = 4, .label = "Scaling"},
    {.key = "video_crop_top", .type = SettingType::integer, .default_value = "0", .overridable = true, .min = 0,
     .max = 64, .label = "Crop top"},
    {.key = "video_max_offscreen", .type = SettingType::real, .default_value = "5", .overridable = true,
     .min_real = 0.0, .max_real = 50.0, .step_real = 0.5, .label = "Max offscreen %"},
    {.key = "pause_on_focus_loss", .type = SettingType::boolean, .default_value = "true", .overridable = false,
     .label = "Pause when the window loses focus"},
    {.key = "path_saves", .type = SettingType::path, .default_value = "", .overridable = false, .label = "Saves"},
};
static const uint32_t DESCRIPTOR_COUNT = sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]);

static void test_table_checks()
{
    Settings settings = {};
    check(settings_init(&settings, DESCRIPTORS, DESCRIPTOR_COUNT), "the test table is valid");

    static const SettingDescriptor BAD_DEFAULT[] = {
        {.key = "crop", .type = SettingType::integer, .default_value = "99", .min = 0, .max = 10},
    };
    check(!settings_init(&settings, BAD_DEFAULT, 1), "a default out of range is caught");

    static const SettingDescriptor DUPLICATE[] = {
        {.key = "a", .type = SettingType::boolean, .default_value = "true"},
        {.key = "a", .type = SettingType::boolean, .default_value = "false"},
    };
    check(!settings_init(&settings, DUPLICATE, 2), "a repeated key is caught");
}

static void test_validation()
{
    const SettingDescriptor* scaling = &DESCRIPTORS[0];
    const SettingDescriptor* crop = &DESCRIPTORS[1];
    const SettingDescriptor* offscreen = &DESCRIPTORS[2];
    const SettingDescriptor* pause = &DESCRIPTORS[3];
    const SettingDescriptor* saves = &DESCRIPTORS[4];

    check(settings_value_is_valid(scaling, "integer_over"), "enum key accepted");
    check(!settings_value_is_valid(scaling, "Integer (over)"), "enum label is not a key");
    check(settings_value_is_valid(crop, "64") && settings_value_is_valid(crop, "0"), "int range ends accepted");
    check(!settings_value_is_valid(crop, "65") && !settings_value_is_valid(crop, "-1"), "int out of range refused");
    check(!settings_value_is_valid(crop, "12px") && !settings_value_is_valid(crop, ""), "int with junk refused");
    check(settings_value_is_valid(offscreen, "2.5"), "real accepted");
    check(!settings_value_is_valid(offscreen, "2,5"), "real with a comma refused");
    check(!settings_value_is_valid(offscreen, "nan") && !settings_value_is_valid(offscreen, "51"),
          "real nan / out of range refused");
    check(settings_value_is_valid(pause, "false") && !settings_value_is_valid(pause, "yes"), "bool true/false only");
    check(settings_value_is_valid(saves, "/home/user/Partidas guardadas/ñ"), "UTF-8 path accepted");
    check(!settings_value_is_valid(saves, "a\nb") && !settings_value_is_valid(saves, " /x"),
          "path with a line break / outer blanks refused");
}

static void test_layers()
{
    Settings settings = {};
    settings_init(&settings, DESCRIPTORS, DESCRIPTOR_COUNT);

    check_effective(&settings, "video_scaling", "auto", SettingLayer::defaults, "nothing set: default");
    check(settings_get_enum(&settings, "video_scaling") == 3, "enum index of the default");
    check(settings_get_bool(&settings, "pause_on_focus_loss"), "bool default");
    check(std::strcmp(settings_get_text(&settings, "path_saves"), "") == 0, "empty path default");

    // No core running: only the global layer exists.
    check(!settings_set(&settings, SettingLayer::core, "video_scaling", "full"), "no core: core layer refused");
    check(!settings_set(&settings, SettingLayer::defaults, "video_scaling", "full"), "defaults can't be set");
    check(settings_set(&settings, SettingLayer::global, "video_scaling", "full"), "set global");
    check(settings.dirty, "a change marks the settings dirty");
    check_effective(&settings, "video_scaling", "full", SettingLayer::global, "global overrides the default");

    check(settings_set_context(&settings, "snes9x", "Super Mario World (USA)"), "context: snes9x + game");
    check_effective(&settings, "video_scaling", "full", SettingLayer::global, "core/game empty: global applies");
    check(settings_set(&settings, SettingLayer::core, "video_scaling", "integer_under"), "set core");
    check_effective(&settings, "video_scaling", "integer_under", SettingLayer::core, "core overrides global");
    check(settings_set(&settings, SettingLayer::game, "video_scaling", "integer_over"), "set game");
    check_effective(&settings, "video_scaling", "integer_over", SettingLayer::game, "game overrides core");
    check(settings_get_enum(&settings, "video_scaling") == 2, "enum index of the game value");
    check(settings_set(&settings, SettingLayer::game, "video_crop_top", "8"), "set an int per game");
    check(settings_get_int(&settings, "video_crop_top") == 8, "typed int from the game layer");

    // Global-only settings.
    check(!settings_set(&settings, SettingLayer::core, "pause_on_focus_loss", "false"), "global-only refused per core");
    check(!settings_set(&settings, SettingLayer::game, "path_saves", "/x"), "global-only refused per game");
    check(!settings_set(&settings, SettingLayer::global, "video_crop_top", "100"), "invalid value refused");
    check(!settings_set(&settings, SettingLayer::global, "no_such_setting", "1"), "unknown key refused");

    // The same game name on another core: none of snes9x's overrides.
    check(settings_set_context(&settings, "mesen", "Super Mario World (USA)"), "context: other core, same game");
    check_effective(&settings, "video_scaling", "full", SettingLayer::global, "other core: global again");
    check(settings_get_int(&settings, "video_crop_top") == 0, "other core: game crop not applied");

    // Same core, another game: the core value, not the game one.
    check(settings_set_context(&settings, "snes9x", "Tetris"), "context: same core, other game");
    check_effective(&settings, "video_scaling", "integer_under", SettingLayer::core, "other game: core value");

    // Core running without content.
    check(settings_set_context(&settings, "snes9x", nullptr), "context: core only");
    check(!settings_set(&settings, SettingLayer::game, "video_scaling", "full"), "no game: game layer refused");
    check(!settings_set_context(&settings, nullptr, "Tetris"), "a game without a core is refused");
    check(!settings_set_context(&settings, "a/b", nullptr), "a core name with '/' is refused");

    // Reset.
    check(settings_set_context(&settings, "snes9x", "Super Mario World (USA)"), "back to the game");
    check(settings_reset(&settings, SettingLayer::game, "video_scaling"), "reset the game value");
    check_effective(&settings, "video_scaling", "integer_under", SettingLayer::core, "after game reset: core");
    check(!settings_reset(&settings, SettingLayer::game, "video_scaling"), "reset again: nothing there");
    check(settings_reset(&settings, SettingLayer::core, "video_scaling"), "reset the core value");
    check_effective(&settings, "video_scaling", "full", SettingLayer::global, "after core reset: global");
    check(settings_reset(&settings, SettingLayer::global, "video_scaling"), "reset the global value");
    check_effective(&settings, "video_scaling", "auto", SettingLayer::defaults, "after global reset: default");

    const char* layer_value = settings_get_layer(&settings, SettingLayer::game, "video_crop_top");
    check(layer_value != nullptr && std::strcmp(layer_value, "8") == 0, "what the game layer itself defines");
    check(settings_get_layer(&settings, SettingLayer::core, "video_crop_top") == nullptr, "core layer has no crop");
}

static void test_get_at_and_typed_setters()
{
    Settings settings = {};
    settings_init(&settings, DESCRIPTORS, DESCRIPTOR_COUNT);
    settings_set_context(&settings, "snes9x", "Tetris");

    check(settings_set_enum(&settings, SettingLayer::global, "video_scaling", 0), "set enum by index (global)");
    check(settings_set_enum(&settings, SettingLayer::game, "video_scaling", 2), "set enum by index (game)");
    check(!settings_set_enum(&settings, SettingLayer::global, "video_scaling", 4), "enum index out of range refused");
    check(!settings_set_enum(&settings, SettingLayer::global, "video_crop_top", 0), "enum setter on an int refused");

    SettingLayer source = SettingLayer::defaults;
    const char* value = settings_get_at(&settings, SettingLayer::global, "video_scaling", &source);
    check(value != nullptr && std::strcmp(value, "full") == 0 && source == SettingLayer::global,
          "at global: the global value, not the game override");
    value = settings_get_at(&settings, SettingLayer::core, "video_scaling", &source);
    check(value != nullptr && std::strcmp(value, "full") == 0 && source == SettingLayer::global,
          "at core with no core value: global");
    value = settings_get_at(&settings, SettingLayer::game, "video_scaling", &source);
    check(value != nullptr && std::strcmp(value, "integer_over") == 0 && source == SettingLayer::game, "at game: game");
    value = settings_get_at(&settings, SettingLayer::defaults, "video_scaling", &source);
    check(value != nullptr && std::strcmp(value, "auto") == 0 && source == SettingLayer::defaults, "at defaults: default");

    check(settings_set_int(&settings, SettingLayer::core, "video_crop_top", 12), "set int");
    check(settings_get_int(&settings, "video_crop_top") == 12, "int read back");
    check(!settings_set_int(&settings, SettingLayer::core, "video_crop_top", 65), "int out of range refused");
    check(settings_set_int(&settings, SettingLayer::core, "video_crop_top", 0), "set int 0");
    const char* zero = settings_get_layer(&settings, SettingLayer::core, "video_crop_top");
    check(zero != nullptr && std::strcmp(zero, "0") == 0, "int written as \"0\"");

    check(settings_set_real(&settings, SettingLayer::global, "video_max_offscreen", 2.5), "set real");
    const char* real = settings_get_layer(&settings, SettingLayer::global, "video_max_offscreen");
    check(real != nullptr && std::strcmp(real, "2.5") == 0, "real written as \"2.5\"");
    check(settings_set_real(&settings, SettingLayer::global, "video_max_offscreen", 10.0), "set whole real");
    real = settings_get_layer(&settings, SettingLayer::global, "video_max_offscreen");
    check(real != nullptr && std::strcmp(real, "10") == 0, "whole real written as \"10\"");

    check(settings_set_bool(&settings, SettingLayer::global, "pause_on_focus_loss", false), "set bool");
    check(!settings_get_bool(&settings, "pause_on_focus_loss"), "bool read back");
}

static void test_load_save(const char* directory)
{
    std::string path = directory;
    path += "/settings.cfg";
    std::remove(path.c_str());

    // A hand-edited file: bad values and misplaced global-only settings are
    // skipped (the layer below applies), unknown things are kept.
    const char* text =
        "video_scaling = integer_under\n"
        "video_crop_top = 999\n"
        "video_max_offscreen = 2.5\n"
        "pause_on_focus_loss = false\n"
        "from_the_future = 1\n"
        "[core:snes9x]\n"
        "video_scaling = stretchy\n"
        "pause_on_focus_loss = true\n"
        "video_crop_top = 4\n"
        "[game:snes9x/Pokémon ポケモン]\n"
        "video_scaling = full\n"
        "[some_other_tool]\n"
        "x = y\n";
    check(host_fs_write_file(path.c_str(), text, std::strlen(text)), "write the hand-edited file");

    Settings settings = {};
    settings_init(&settings, DESCRIPTORS, DESCRIPTOR_COUNT);
    check(settings_load(&settings, path.c_str()), "load");
    check(!settings.dirty, "freshly loaded: not dirty");
    check(settings_set_context(&settings, "snes9x", "Pokémon ポケモン"), "context with a UTF-8 game");

    check_effective(&settings, "video_scaling", "full", SettingLayer::game, "UTF-8 game section applies");
    check_effective(&settings, "video_crop_top", "4", SettingLayer::core, "core value over an invalid global");
    check(settings_get_real(&settings, "video_max_offscreen") == 2.5, "real from the file");
    check_effective(&settings, "pause_on_focus_loss", "false", SettingLayer::global,
                    "global-only setting in a core section ignored");

    check(settings_set_context(&settings, "snes9x", "Other"), "context: other game");
    check_effective(&settings, "video_scaling", "integer_under", SettingLayer::global,
                    "invalid core value skipped: global applies");
    check(settings_set_context(&settings, nullptr, nullptr), "context: nothing running");
    check_effective(&settings, "video_crop_top", "0", SettingLayer::defaults, "invalid global value: default");

    check(settings_set(&settings, SettingLayer::global, "video_crop_top", "2"), "fix the bad value");
    check(settings_save(&settings, path.c_str(), "test settings"), "save");
    check(!settings.dirty, "saved: not dirty");

    Settings again = {};
    settings_init(&again, DESCRIPTORS, DESCRIPTOR_COUNT);
    check(settings_load(&again, path.c_str()), "load the saved file");
    check_effective(&again, "video_crop_top", "2", SettingLayer::global, "saved value read back");
    const char* future = settings_file_get(&again.file, "", "from_the_future");
    check(future != nullptr && std::strcmp(future, "1") == 0, "unknown global key kept");
    const char* other = settings_file_get(&again.file, "some_other_tool", "x");
    check(other != nullptr && std::strcmp(other, "y") == 0, "unknown section kept");
    check(settings_set_context(&again, "snes9x", "Pokémon ポケモン"), "context again");
    check_effective(&again, "video_scaling", "full", SettingLayer::game, "game override survives save/load");

    Settings missing = {};
    settings_init(&missing, DESCRIPTORS, DESCRIPTOR_COUNT);
    std::string missing_path = directory;
    missing_path += "/does-not-exist.cfg";
    std::remove(missing_path.c_str());
    check(settings_load(&missing, missing_path.c_str()), "a missing file loads");
    check_effective(&missing, "video_scaling", "auto", SettingLayer::defaults, "missing file: defaults");
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }

    test_table_checks();
    test_validation();
    test_layers();
    test_get_at_and_typed_setters();
    test_load_save(argv[1]);

    if (g_failures == 0)
    {
        std::printf("settings: all checks passed\n");
        return 0;
    }
    std::printf("settings: %d check(s) failed\n", g_failures);
    return 1;
}
