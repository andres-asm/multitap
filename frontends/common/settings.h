#pragma once

// Settings: what each setting is (descriptors), and its value in three
// layers: global -> core -> game. The effective value is the most specific
// layer that defines a valid one, else the descriptor's default. Each
// frontend has its own descriptor table; this module knows none of them.
//
// Storage is one settings file (settings_file.h), one section per layer:
//   ""                        global
//   "core:<core>"             one core          ("core:snes9x")
//   "game:<core>/<game>"      one game on one core ("game:snes9x/Super Mario World (USA)")
// <core> is the core's short name ("snes9x" from snes9x_libretro.so) and
// <game> the content file's stem: the same names the save files use. A game
// override belongs to the core it was set with, since the same file name
// exists on several systems.
//
// Values are kept as text, exactly as in the file. Sections and keys we
// don't know are kept and written back (newer versions, other cores).
// A value that isn't valid for its setting (out of range, unknown enum key,
// a global-only setting in a core section...) is reported on load and
// skipped when reading: the next layer down applies.

#include <cstdint>
#include <string>

#include "settings_file.h"

enum class SettingType
{
    boolean,       // "true" / "false"
    integer,       // whole number in [min, max]
    real,          // number in [min_real, max_real], "." as decimal point
    enumeration,   // one of `values` (by key)
    path,          // any text (UTF-8 path)
    text,          // any text
};

// Where a value comes from, and which layer an edit goes to.
enum class SettingLayer
{
    defaults,   // the descriptor's default (never edited)
    global,
    core,
    game,
};

struct SettingEnumValue
{
    const char* key;     // file value, stable, never translated
    const char* label;   // English for now; a string ID once localization lands (plan step 9)
};

struct SettingDescriptor
{
    const char* key = nullptr;             // file key, stable, never translated
    SettingType type = SettingType::boolean;
    const char* default_value = nullptr;   // text form, as written in the file
    bool overridable = false;              // may be set per core / per game
    int32_t min = 0;                       // integer
    int32_t max = 0;
    int32_t step = 1;
    double min_real = 0.0;                 // real
    double max_real = 0.0;
    double step_real = 0.0;
    const SettingEnumValue* values = nullptr;   // enumeration
    uint32_t value_count = 0;
    const char* label = nullptr;           // English for now (see SettingEnumValue::label)
    const char* help = nullptr;
};

struct Settings
{
    const SettingDescriptor* descriptors = nullptr;   // the frontend's table, not owned
    uint32_t descriptor_count = 0;
    SettingsFile file;
    std::string core_section;   // "core:snes9x", or "" when no core is running
    std::string game_section;   // "game:snes9x/<game>", or "" when no game is running
    bool dirty = false;         // changed since the last load/save
};

// Uses `descriptors` (must outlive the Settings). Checks the table: unique
// keys and a valid default for each; returns false (and logs) if not.
bool settings_init(Settings* settings, const SettingDescriptor* descriptors, uint32_t descriptor_count);

// Loads `path` (a missing file means "all defaults") and reports every value
// that won't be used. Keeps the descriptors and the core/game context.
bool settings_load(Settings* settings, const char* path);

// Writes every layer (atomic). `title` is the first line of the header
// comment ("piccolo settings").
bool settings_save(Settings* settings, const char* path, const char* title);

// Which core and game are running, for the core/game layers. nullptr or ""
// for none. A game needs a core. Names may not contain line breaks, and a
// core name may not contain '/'.
bool settings_set_context(Settings* settings, const char* core_name, const char* game_name);

// nullptr if `key` isn't in the table.
const SettingDescriptor* settings_find(const Settings* settings, const char* key);

// Is `value` valid for this setting?
bool settings_value_is_valid(const SettingDescriptor* descriptor, const char* value);

// The effective value as text, and (if `source` isn't nullptr) the layer it
// comes from. nullptr (and a logged error) for a key not in the table.
const char* settings_get(const Settings* settings, const char* key, SettingLayer* source);

// Typed effective values. A key not in the table, or of another type, is a
// programming error: logged, and false / 0 / "" returned.
bool settings_get_bool(const Settings* settings, const char* key);
int32_t settings_get_int(const Settings* settings, const char* key);
double settings_get_real(const Settings* settings, const char* key);
uint32_t settings_get_enum(const Settings* settings, const char* key);   // index into `values`
const char* settings_get_text(const Settings* settings, const char* key); // path or text

// What one layer itself defines (valid or not), or nullptr. For the UI.
const char* settings_get_layer(const Settings* settings, SettingLayer layer, const char* key);

// Sets `key` in one layer. Refused (logged, false) when the value isn't
// valid, the layer is `defaults`, the setting is global-only and the layer
// isn't global, or the layer has no core/game running.
bool settings_set(Settings* settings, SettingLayer layer, const char* key, const char* value);

// Removes `key` from one layer, so the layer below applies again ("reset to
// global" in a core/game layer, "reset to default" in the global one).
// Returns true if the layer had it.
bool settings_reset(Settings* settings, SettingLayer layer, const char* key);
