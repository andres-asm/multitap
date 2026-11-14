#pragma once

// Core option values kept per core, per content folder and per game (decided
// by Andrés, plan-milestones milestone 4): so a core that runs several
// systems (GBA and GBC) doesn't mix their options.
//
// One file, settings/core_options.cfg (settings_file's INI dialect), a
// section per layer:
//   [snes9x]                                   every game on the core
//   [snes9x/dir:Super Nintendo]                games in a folder with that name
//   [snes9x/game:Super Mario World (USA)]      one game
// <core> is the core's short name (as the save files use it), <folder> the
// content folder's NAME, not its path (survives moving the library), <game>
// the content file's stem. The effective value is the most specific layer
// that has one: game -> folder -> core -> the core's own default. Only values
// that differ from the layers below are written: setting a value equal to
// what applies below removes it from that layer instead.
//
// Keys and values are the core's (as GET_VARIABLE returns them); this module
// doesn't know which keys a core has (core_options_cache.h does).

#include <cstdint>
#include <string>
#include <vector>

#include "libretro_options.h"   // LibretroOptionValue
#include "settings_file.h"

enum class CoreOptionLayer
{
    core,
    directory,
    game,
};

struct CoreOptionsStore
{
    SettingsFile file;
    std::string  core_section;        // "snes9x", or "" when no core
    std::string  directory_section;   // "snes9x/dir:<folder>", or "" (no content)
    std::string  game_section;        // "snes9x/game:<game>", or "" (no content)
    bool         dirty = false;       // changed since load / save
};

// A missing file is an empty store. Keeps the context.
bool core_options_store_load(CoreOptionsStore* store, const char* path);
bool core_options_store_save(CoreOptionsStore* store, const char* path);

// What runs: the core's short name, the content folder's name and the
// content's stem ("" for none; folder and game need a core). A core name may
// not contain '/' or a line break.
bool core_options_store_set_context(CoreOptionsStore* store, const char* core, const char* folder, const char* game);

// The section name of `layer` for what runs, or "" (that layer doesn't exist now).
const std::string* core_options_store_section(const CoreOptionsStore* store, CoreOptionLayer layer);

// What `layer` itself stores for `key`, or nullptr.
const char* core_options_store_get_layer(const CoreOptionsStore* store, CoreOptionLayer layer, const char* key);

// The stored value as it resolves at `layer` (ignoring the layers above it),
// and where it comes from; nullptr = nothing stored (the core's default).
const char* core_options_store_get_at(const CoreOptionsStore* store, CoreOptionLayer layer, const char* key,
                                      CoreOptionLayer* source);

// The effective stored value (game -> folder -> core); nullptr = the default.
const char* core_options_store_get(const CoreOptionsStore* store, const char* key, CoreOptionLayer* source);

// Sets `key` at `layer`. If `value` is what the layers below give (or
// `default_value` when they give nothing), the layer's own entry is removed
// instead. False if the layer doesn't exist now or the value can't be stored.
bool core_options_store_set(CoreOptionsStore* store, CoreOptionLayer layer, const char* key, const char* value,
                            const char* default_value);

// Removes `key` from `layer` (back to the layer below). True if it was there.
bool core_options_store_reset(CoreOptionsStore* store, CoreOptionLayer layer, const char* key);

// Does `layer` store anything for what runs?
bool core_options_store_layer_has_values(const CoreOptionsStore* store, CoreOptionLayer layer);

// Every effective stored value for what runs (for LibretroHostConfig's
// saved options). The pointers stay valid until the store changes.
void core_options_store_effective(const CoreOptionsStore* store, std::vector<LibretroOptionValue>* values);

// For a manager screen: every section of `core` (its own, its folders', its
// games'), in file order.
void core_options_store_sections_of(const CoreOptionsStore* store, const char* core, std::vector<std::string>* sections);

// Removes a whole section, or one key from a section. True if it was there.
bool core_options_store_remove_section(CoreOptionsStore* store, const char* section);
bool core_options_store_remove(CoreOptionsStore* store, const char* section, const char* key);
