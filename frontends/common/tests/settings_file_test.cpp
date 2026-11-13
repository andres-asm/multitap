// Checks the settings file format rules written in settings_file.h.
//
// Usage: frontend_settings_file_test <scratch directory>
// The directory is created if needed; the test writes settings files in it.
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>

#include "host_fs.h"
#include "settings_file.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

// Checks one value: `expected` nullptr means "must not be set".
static void check_value(const SettingsFile* file, const char* section, const char* key, const char* expected)
{
    const char* value = settings_file_get(file, section, key);
    bool same = expected == nullptr ? value == nullptr : value != nullptr && std::strcmp(value, expected) == 0;
    if (!same)
    {
        std::printf("FAIL [%s] %s: expected %s%s%s, got %s%s%s\n", section, key, expected != nullptr ? "\"" : "",
                    expected != nullptr ? expected : "(unset)", expected != nullptr ? "\"" : "",
                    value != nullptr ? "\"" : "", value != nullptr ? value : "(unset)", value != nullptr ? "\"" : "");
        g_failures++;
    }
}

static void parse_string(SettingsFile* file, const char* text)
{
    settings_file_parse(file, text, std::strlen(text), "test");
}

// The text form of `file` as a NUL-terminated new[] string.
static char* text_of(const SettingsFile* file, const char* header)
{
    char* text = nullptr;
    size_t size = 0;
    if (!settings_file_to_text(file, header, &text, &size))
    {
        return nullptr;
    }
    char* terminated = new char[size + 1];
    std::memcpy(terminated, text, size);
    terminated[size] = '\0';
    delete[] text;
    return terminated;
}

static void check_text(const SettingsFile* file, const char* header, const char* expected, const char* what)
{
    char* text = text_of(file, header);
    bool same = text != nullptr && std::strcmp(text, expected) == 0;
    check(same, what);
    if (!same)
    {
        std::printf("--- expected:\n%s--- got:\n%s---\n", expected, text != nullptr ? text : "(null)");
    }
    delete[] text;
}

static void test_basic_rules()
{
    SettingsFile file = {};
    parse_string(&file,
                 "\xEF\xBB\xBF# piccolo settings\r\n"
                 "video_aspect = core\r\n"
                 "   video_scaling=auto   \r\n"
                 "\n"
                 "  # an indented comment\n"
                 "title = Super Mario # not a comment\n"
                 "formula = a = b\n"
                 "empty =\n"
                 "\n"
                 "[core:snes9x]\n"
                 "video_aspect = 8:7\n"
                 "[ game:snes9x/Super Mario World (USA) ]\n"
                 "video_scaling = integer_over\n");

    check_value(&file, "", "video_aspect", "core");
    check_value(&file, "", "video_scaling", "auto");
    check_value(&file, "", "title", "Super Mario # not a comment");
    check_value(&file, "", "formula", "a = b");
    check_value(&file, "", "empty", "");
    check_value(&file, "", "# an indented comment", nullptr);
    check_value(&file, "core:snes9x", "video_aspect", "8:7");
    check_value(&file, "game:snes9x/Super Mario World (USA)", "video_scaling", "integer_over");
    check_value(&file, "game:snes9x/Super Mario World (USA)", "video_aspect", nullptr);
    check(file.section_count == 3, "three sections (global, core, game)");
    settings_file_destroy(&file);
}

static void test_section_names()
{
    SettingsFile file = {};
    parse_string(&file,
                 "[game:snes9x/Pang [b]]\n"
                 "a = 1\n"
                 "[game:mesen/Tetris [!] (World)] trailing text\n"
                 "b = 2\n"
                 "[broken header\n"
                 "lost = yes\n"
                 "[core:snes9x]\n"
                 "c = 3\n"
                 "[game:snes9x/Pang [b]]\n"
                 "d = 4\n"
                 "a = 5\n"
                 "no equal sign here\n"
                 " = no key\n");

    check_value(&file, "game:snes9x/Pang [b]", "a", "5");
    check_value(&file, "game:snes9x/Pang [b]", "d", "4");
    check_value(&file, "game:mesen/Tetris [!] (World)", "b", "2");
    check_value(&file, "core:snes9x", "c", "3");
    check_value(&file, "", "lost", nullptr);
    check_value(&file, "core:snes9x", "lost", nullptr);
    check(settings_file_find_section(&file, "broken header") < 0, "a header without ']' makes no section");
    check(settings_file_find_section(&file, "game:snes9x/Pang [b]") == 1, "a repeated header continues the section");
    settings_file_destroy(&file);
}

static void test_utf8()
{
    SettingsFile file = {};
    parse_string(&file,
                 "nombre = Ñandú ¿qué? ¡sí!\n"
                 "[game:mgba/Pokémon ポケモン (日本)]\n"
                 "título = Pokémon\n");
    check_value(&file, "", "nombre", "Ñandú ¿qué? ¡sí!");
    check_value(&file, "game:mgba/Pokémon ポケモン (日本)", "título", "Pokémon");
    settings_file_destroy(&file);
}

static void test_round_trip()
{
    const char* original =
        "# a comment that will not survive\n"
        "video_aspect = core\n"
        "some_future_key = from a newer version\n"
        "[core:snes9x]\n"
        "unknown_core_thing = 42\n"
        "[game:snes9x/Pang [b]]\n"
        "video_scaling = integer_over\n"
        "[empty section]\n";

    SettingsFile file = {};
    parse_string(&file, original);
    check_text(&file, "piccolo settings\nComments are not kept.",
               "# piccolo settings\n"
               "# Comments are not kept.\n"
               "\n"
               "video_aspect = core\n"
               "some_future_key = from a newer version\n"
               "\n"
               "[core:snes9x]\n"
               "unknown_core_thing = 42\n"
               "\n"
               "[game:snes9x/Pang [b]]\n"
               "video_scaling = integer_over\n",
               "written text matches the expected layout");

    // Write, read back, write again: the same text both times.
    char* first = text_of(&file, "header");
    SettingsFile again = {};
    if (first != nullptr)
    {
        parse_string(&again, first);
    }
    char* second = text_of(&again, "header");
    check(first != nullptr && second != nullptr && std::strcmp(first, second) == 0, "round trip is stable");
    check_value(&again, "", "some_future_key", "from a newer version");
    check_value(&again, "core:snes9x", "unknown_core_thing", "42");
    check_value(&again, "game:snes9x/Pang [b]", "video_scaling", "integer_over");
    delete[] first;
    delete[] second;
    settings_file_destroy(&again);
    settings_file_destroy(&file);

    SettingsFile empty = {};
    check_text(&empty, nullptr, "", "an empty file writes nothing");
}

static void test_set_get_remove()
{
    SettingsFile file = {};
    check(settings_file_set(&file, "", "video_filter", "nearest"), "set a global key");
    check(settings_file_set(&file, "core:snes9x", "video_filter", "bilinear"), "set a core key");
    check(settings_file_set(&file, "core:snes9x", "video_filter", "sharp_bilinear"), "replace a core key");
    check(settings_file_set(&file, "game:snes9x/Pang [b]", "crop_top", ""), "set an empty value");
    check_value(&file, "", "video_filter", "nearest");
    check_value(&file, "core:snes9x", "video_filter", "sharp_bilinear");
    check_value(&file, "game:snes9x/Pang [b]", "crop_top", "");

    check(!settings_file_set(&file, "", "", "x"), "empty key refused");
    check(!settings_file_set(&file, "", "a=b", "x"), "key with '=' refused");
    check(!settings_file_set(&file, "", "#key", "x"), "key starting with '#' refused");
    check(!settings_file_set(&file, "", "[key", "x"), "key starting with '[' refused");
    check(!settings_file_set(&file, "", " key", "x"), "key with outer blanks refused");
    check(!settings_file_set(&file, "", "key", "two\nlines"), "value with a line break refused");
    check(!settings_file_set(&file, "", "key", "x "), "value with outer blanks refused");
    check(!settings_file_set(&file, "a\nb", "key", "x"), "section with a line break refused");
    check(!settings_file_set(&file, " core:x", "key", "x"), "section with outer blanks refused");
    check_value(&file, "", "key", nullptr);

    check(settings_file_remove(&file, "core:snes9x", "video_filter"), "remove an existing key");
    check(!settings_file_remove(&file, "core:snes9x", "video_filter"), "remove it again: not there");
    check(!settings_file_remove(&file, "core:nothing", "video_filter"), "remove from a missing section");
    check_value(&file, "core:snes9x", "video_filter", nullptr);
    check_value(&file, "", "video_filter", "nearest");

    // Many keys and sections: the arrays have to grow several times.
    char section[64];
    char key[64];
    for (int s = 0; s < 40; s++)
    {
        std::snprintf(section, sizeof(section), "game:core/Game %d", s);
        for (int k = 0; k < 40; k++)
        {
            std::snprintf(key, sizeof(key), "key_%d", k);
            settings_file_set(&file, section, key, key);
        }
    }
    check_value(&file, "game:core/Game 39", "key_39", "key_39");
    check_value(&file, "game:core/Game 0", "key_0", "key_0");
    settings_file_destroy(&file);
    check(file.section_count == 0 && file.sections == nullptr, "destroy leaves an empty file");
}

static void test_save_and_load(const char* directory)
{
    char path[1024];
    char temporary[1100];
    std::snprintf(path, sizeof(path), "%s/sub dir/settings.cfg", directory);
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    // Leftovers from an earlier run would hide a failed save.
    std::remove(path);
    std::remove(temporary);

    SettingsFile missing = {};
    check(settings_file_set(&missing, "", "stale", "value"), "set before loading a missing file");
    check(settings_file_load(&missing, "/nonexistent/multitap/settings.cfg"), "loading a missing file succeeds");
    check(missing.section_count == 0, "a missing file loads empty");
    settings_file_destroy(&missing);

    SettingsFile file = {};
    settings_file_set(&file, "", "video_aspect", "core");
    settings_file_set(&file, "game:mgba/Pokémon ポケモン", "video_scaling", "auto");
    check(settings_file_save(&file, path, "test settings"), "save (creates the directory)");
    check(!host_fs_exists(temporary), "no temporary file left after saving");

    settings_file_set(&file, "", "video_aspect", "4:3");
    check(settings_file_save(&file, path, "test settings"), "save over an existing file");
    check(!host_fs_exists(temporary), "no temporary file left after saving again");

    SettingsFile loaded = {};
    check(settings_file_load(&loaded, path), "load the saved file");
    check_value(&loaded, "", "video_aspect", "4:3");
    check_value(&loaded, "game:mgba/Pokémon ポケモン", "video_scaling", "auto");
    settings_file_destroy(&loaded);
    settings_file_destroy(&file);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }

    test_basic_rules();
    test_section_names();
    test_utf8();
    test_round_trip();
    test_set_get_remove();
    test_save_and_load(argv[1]);

    if (g_failures == 0)
    {
        std::printf("settings file: all checks passed\n");
        return 0;
    }
    std::printf("settings file: %d check(s) failed\n", g_failures);
    return 1;
}
