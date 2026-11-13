// Checks the string table and the .po subset (localization.h).
//
// Usage: frontend_localization_test <scratch directory>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <string>

#include "host_fs.h"
#include "localization.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static void check_text(const Localization* localization, uint32_t id, const char* expected, const char* what)
{
    const char* text = localization_get(localization, id);
    if (std::strcmp(text, expected) != 0)
    {
        std::printf("FAIL %s: expected \"%s\", got \"%s\"\n", what, expected, text);
        g_failures++;
    }
}

// String IDs of the test table (a frontend has an enum class; plain numbers do here).
constexpr uint32_t LOAD_GAME = 0;
constexpr uint32_t SETTINGS = 1;
constexpr uint32_t QUIT = 2;
constexpr uint32_t HELP = 3;
constexpr uint32_t UNTRANSLATED = 4;
constexpr uint32_t STRING_COUNT = 5;

static const LocalizedString TABLE[STRING_COUNT] = {
    {"menu_load_game", "LOAD GAME..."},
    {"menu_settings", "SETTINGS..."},
    {"menu_quit", "QUIT"},
    {"help_text", "Two\nlines"},
    {"untranslated", "Only in English"},
};

static const char* const SPANISH =
    "# Spanish translation\n"
    "msgid \"\"\n"
    "msgstr \"\"\n"
    "\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
    "\n"
    "msgctxt \"menu_load_game\"\n"
    "msgid \"LOAD GAME...\"\n"
    "msgstr \"CARGAR JUEGO...\"\n"
    "\n"
    "#, fuzzy\n"
    "msgctxt \"menu_settings\"\n"
    "msgid \"SETTINGS...\"\n"
    "msgstr \"AJUSTES (sin revisar)\"\n"
    "\n"
    "# a translator comment\n"
    "msgctxt \"help_text\"\n"
    "msgid \"Two\\nlines\"\n"
    "msgstr \"\"\n"
    "\"Dos \\\"l\xC3\xADneas\\\"\\n\"\n"
    "\"y una barra \\\\\"\n"
    "msgctxt \"menu_quit\"\n"
    "msgid \"QUIT\"\n"
    "msgstr \"SALIR\"\n"
    "\n"
    "msgctxt \"from_a_newer_version\"\n"
    "msgid \"NEW\"\n"
    "msgstr \"NUEVO\"\n"
    "\n"
    "msgid \"No context\"\n"
    "msgstr \"Sin contexto\"\n"
    "\n"
    "msgctxt \"untranslated\"\n"
    "msgid \"Only in English\"\n"
    "msgstr \"\"\n";

static void test_table()
{
    Localization localization = {};
    check(localization_init(&localization, TABLE, STRING_COUNT), "the test table is valid");
    check_text(&localization, LOAD_GAME, "LOAD GAME...", "English before any .po");
    check_text(&localization, STRING_COUNT, "?", "an ID outside the table");
    check(localization_find(&localization, "menu_quit") == QUIT, "find a key");
    check(localization_find(&localization, "nope") == UINT32_MAX, "an unknown key");

    static const LocalizedString DUPLICATE[] = {{"a", "A"}, {"a", "B"}};
    check(!localization_init(&localization, DUPLICATE, 2), "a repeated key is caught");
    static const LocalizedString EMPTY_KEY[] = {{"", "A"}};
    check(!localization_init(&localization, EMPTY_KEY, 1), "an empty key is caught");
}

static void test_po()
{
    Localization localization = {};
    localization_init(&localization, TABLE, STRING_COUNT);
    uint32_t translated = localization_parse_po(&localization, SPANISH, std::strlen(SPANISH), "es.po", "es");
    check(translated == 3, "three entries used (load game, help, quit)");
    check(localization.language == "es", "the language is recorded");
    check_text(&localization, LOAD_GAME, "CARGAR JUEGO...", "a plain entry");
    check_text(&localization, SETTINGS, "SETTINGS...", "a fuzzy entry is skipped");
    check_text(&localization, HELP, "Dos \"l\xC3\xADneas\"\ny una barra \\", "continuation lines and escapes");
    check_text(&localization, QUIT, "SALIR", "an entry right after another (no blank line)");
    check_text(&localization, UNTRANSLATED, "Only in English", "an empty msgstr falls back to English");

    localization_use_english(&localization);
    check_text(&localization, LOAD_GAME, "LOAD GAME...", "back to English");
    check(localization.language.empty(), "English has no language code");

    // Plurals: the first form is used.
    const char* plural =
        "msgctxt \"menu_quit\"\n"
        "msgid \"QUIT\"\n"
        "msgid_plural \"QUITS\"\n"
        "msgstr[0] \"SALIR\"\n"
        "msgstr[1] \"SALIDAS\"\n";
    localization_parse_po(&localization, plural, std::strlen(plural), "plural.po", "es");
    check_text(&localization, QUIT, "SALIR", "plural entry: msgstr[0]");

    // Windows line endings.
    const char* crlf = "msgctxt \"menu_quit\"\r\nmsgid \"QUIT\"\r\nmsgstr \"SALIR\"\r\n";
    localization_parse_po(&localization, crlf, std::strlen(crlf), "crlf.po", "es");
    check_text(&localization, QUIT, "SALIR", "CRLF line endings");
}

static void test_files(const char* directory)
{
    Localization localization = {};
    localization_init(&localization, TABLE, STRING_COUNT);
    std::string path = directory;
    path += "/es.po";
    check(host_fs_write_file(path.c_str(), SPANISH, std::strlen(SPANISH)), "write es.po");
    check(localization_load_po(&localization, path.c_str(), "es"), "load es.po");
    check_text(&localization, LOAD_GAME, "CARGAR JUEGO...", "translated from the file");

    std::string missing = directory;
    missing += "/xx.po";
    std::remove(missing.c_str());
    check(!localization_load_po(&localization, missing.c_str(), "xx"), "a missing file fails");
    check_text(&localization, LOAD_GAME, "LOAD GAME...", "a missing file leaves English");
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }
    test_table();
    test_po();
    test_files(argv[1]);
    if (g_failures == 0)
    {
        std::printf("localization: all checks passed\n");
        return 0;
    }
    std::printf("localization: %d check(s) failed\n", g_failures);
    return 1;
}
