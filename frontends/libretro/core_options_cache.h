#pragma once

// What each core declared as its options, kept on disk
// (settings/core_options_cache.cfg), so a frontend can show and clean a
// core's stored option values WITHOUT loading the core: the way back when an
// option change makes the core crash on start (decided by Andrés,
// 2026-11-14). Also tells which stored keys a core no longer declares.
//
// Updated whenever a running core (re)declares its options. A section per
// option, in the order the core declared them:
//   [snes9x/snes9x_region]
//   label = Console Region
//   category = System             (the category's label; absent = none)
//   info = ...                    (help text, line breaks as spaces; absent = none)
//   default = auto
//   value1 = auto                 label1 = Auto        (label absent = show the value)
//   value2 = ntsc ...
// One entry per value, so no character in a value needs escaping. An option
// whose text the file can't hold (a value with leading spaces...) is skipped.

#include <cstdint>
#include <string>
#include <vector>

#include "libretro_options.h"
#include "settings_file.h"

struct CachedCoreOption
{
    std::string key;
    std::string label;
    std::string category;        // label, "" = none
    std::string info;            // "" = none
    std::string default_value;
    std::vector<std::string> values;
    std::vector<std::string> labels;   // same count as values; "" = show the value
};

struct CoreOptionsCache
{
    SettingsFile file;
    bool         dirty = false;
};

bool core_options_cache_load(CoreOptionsCache* cache, const char* path);
bool core_options_cache_save(CoreOptionsCache* cache, const char* path);

// Replaces `core`'s cached options with what it declared. Marks the cache
// dirty only when something changed.
void core_options_cache_update(CoreOptionsCache* cache, const char* core, const LibretroOptions* options);

// The cores with cached options, in file order.
void core_options_cache_cores(const CoreOptionsCache* cache, std::vector<std::string>* cores);

// `core`'s options, in the order it declared them (empty if never cached).
void core_options_cache_get(const CoreOptionsCache* cache, const char* core, std::vector<CachedCoreOption>* options);

// False if `core` never declared `key` (as far as the cache knows).
bool core_options_cache_has(const CoreOptionsCache* cache, const char* core, const char* key);
