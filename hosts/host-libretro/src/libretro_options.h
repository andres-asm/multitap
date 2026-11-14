#pragma once

// Core options, kept by the host.
//
// libretro has three ways to declare options, and cores pick one based on what
// GET_CORE_OPTIONS_VERSION answers:
//
//   v0  SET_VARIABLES           "Label; first|second|third", first value = default
//   v1  SET_CORE_OPTIONS(_INTL) label, info text, values with labels, explicit default
//   v2  SET_CORE_OPTIONS_V2(_INTL)  v1 + categories + shorter labels for use inside a category
//
// All three are converted into the same LibretroOptions list, so the rest of
// the host and every frontend deal with one format. Strings are COPIED: the
// core's pointers aren't ours to keep (libretro.h requires the frontend to copy).
//
// For _INTL variants only the English ("us") set is used for now.

#include <cstddef>
#include <cstdint>

#include "libretro.h"

struct LibretroOptionCategory
{
    char* key   = nullptr;
    char* label = nullptr;
    char* info  = nullptr;   // may be nullptr
};

struct LibretroOption
{
    char* key              = nullptr;
    char* label            = nullptr;   // full label ("Video > Width" in many v2 cores)
    char* label_in_category = nullptr;  // v2 desc_categorized; nullptr = use `label`
    char* info             = nullptr;   // help text; may be nullptr
    char* category_key     = nullptr;   // v2 only; nullptr = no category

    char**   values       = nullptr;    // value_count strings, what GET_VARIABLE returns
    char**   value_labels = nullptr;    // value_count strings; entries may be nullptr (= show the value)
    uint32_t value_count  = 0;

    uint32_t default_index = 0;
    uint32_t current_index = 0;
    bool     visible       = true;      // SET_CORE_OPTIONS_DISPLAY
};

// A value the frontend kept for an option (key and value as GET_VARIABLE
// would return them).
struct LibretroOptionValue
{
    const char* key   = nullptr;
    const char* value = nullptr;
};

struct LibretroOptions
{
    LibretroOption*         options        = nullptr;
    uint32_t                count          = 0;
    LibretroOptionCategory* categories     = nullptr;
    uint32_t                category_count = 0;

    uint32_t api_used = 0;        // 0, 1 or 2: the format of the last successful registration
    bool     updated  = false;    // a value changed since the core last asked (GET_VARIABLE_UPDATE)
    uint32_t serial   = 0;        // +1 on every successful registration (frontends notice re-declarations)

    // Saved values (libretro_options_set_saved), our own copies.
    char**   saved_keys   = nullptr;
    char**   saved_values = nullptr;
    uint32_t saved_count  = 0;
};

// Replace the whole option list. Options that existed before under the same
// key keep their current value if it's still one of the allowed values.
// Return false (and log why) if the definitions are unusable; the old list is kept then.
bool libretro_options_register_v0(LibretroOptions* options, const retro_variable* variables);
bool libretro_options_register_v1(LibretroOptions* options, const retro_core_option_definition* definitions);
bool libretro_options_register_v2(LibretroOptions* options, const retro_core_options_v2* definitions);

// The values the frontend saved for this core (its options file), copied.
// Each is applied when its option is declared for the first time (before the
// core reads it); an option the core declares again keeps its current value.
// A saved value that isn't one of the option's values is skipped (logged):
// the default applies. Call before the core declares its options (the host
// does it at load, from LibretroHostConfig). False only if out of memory.
bool libretro_options_set_saved(LibretroOptions* options, const LibretroOptionValue* values, uint32_t count);

// nullptr if the key is unknown.
const LibretroOption* libretro_options_find(const LibretroOptions* options, const char* key);

// Current value for GET_VARIABLE, or nullptr if the key is unknown.
// The string stays valid until the options are registered again or freed.
const char* libretro_options_get(const LibretroOptions* options, const char* key);

// Sets a value (from the frontend). Fails (and logs) for an unknown key or a
// value that isn't in the option's list. Setting the value it already has is
// accepted and doesn't mark the options as updated.
bool libretro_options_set(LibretroOptions* options, const char* key, const char* value);

// SET_CORE_OPTIONS_DISPLAY. False for an unknown key.
bool libretro_options_set_visible(LibretroOptions* options, const char* key, bool visible);

// GET_VARIABLE_UPDATE: returns the "updated" flag and clears it.
bool libretro_options_take_update(LibretroOptions* options);

void libretro_options_free(LibretroOptions* options);
