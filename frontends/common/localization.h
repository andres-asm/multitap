#pragma once

// UI text in several languages. Each frontend has a table of its strings
// (key + English text) indexed by a string ID (its own enum). English is
// compiled in; other languages come from gettext .po files, which tools like
// Weblate, Crowdin and Poedit edit. A string without a translation shows in
// English.
//
// The .po subset we read (and write):
//
//   # comment
//   msgctxt "menu_load_game"          <- the key from the table
//   msgid "LOAD GAME..."              <- the English text (for translators)
//   msgstr "CARGAR JUEGO..."          <- the translation
//
//   - A string may continue on the next lines: "..." lines are appended.
//   - Escapes: \" \\ \n \t \r.
//   - Entries marked "#, fuzzy" are skipped (not reviewed yet), and so is the
//     header entry (msgid ""), entries without msgctxt, with an empty msgstr
//     or a key that's not in the table (from an older or newer version).
//   - Plurals: msgstr[0] is used. (No string needs plural forms yet.)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct LocalizedString
{
    const char* key;       // stable, never translated (the .po msgctxt)
    const char* english;
};

struct Localization
{
    const LocalizedString* table = nullptr;   // the frontend's, not owned
    uint32_t               count = 0;
    std::vector<std::string> translations;    // per ID; "" = use English
    std::string            language;          // "" = English only
};

// Checks the table (keys unique and not empty, English present) and starts
// in English. False (logged) if the table is broken.
bool localization_init(Localization* localization, const LocalizedString* table, uint32_t count);

// Back to English only.
void localization_use_english(Localization* localization);

// Reads translations from .po text (replacing any loaded before) and calls
// them `language`. Returns how many strings got a translation.
uint32_t localization_parse_po(Localization* localization, const char* text, size_t size, const char* source_name,
                               const char* language);

// Same from a file. False (and English only) if it can't be read.
bool localization_load_po(Localization* localization, const char* path, const char* language);

// The text for `id`: its translation, or English. "?" for an ID outside the table.
const char* localization_get(const Localization* localization, uint32_t id);

// The ID of `key`, or UINT32_MAX.
uint32_t localization_find(const Localization* localization, const char* key);
