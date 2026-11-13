#include "settings.h"

#include <charconv>
#include <cmath>
#include <cstring>

#include <SDL3/SDL.h>

static const char CORE_PREFIX[] = "core:";
static const char GAME_PREFIX[] = "game:";

static bool starts_with(const char* text, const char* prefix)
{
    return std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

static bool has_line_break(const char* text)
{
    return std::strchr(text, '\n') != nullptr || std::strchr(text, '\r') != nullptr;
}

static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

// std::from_chars: no locale (strtod would read "1,5" in some locales), and
// it tells us whether the whole text was a number.
static bool parse_int(const char* text, int32_t* out)
{
    const char* end = text + std::strlen(text);
    std::from_chars_result result = std::from_chars(text, end, *out);
    return result.ec == std::errc() && result.ptr == end && end != text;
}

static bool parse_real(const char* text, double* out)
{
    const char* end = text + std::strlen(text);
    std::from_chars_result result = std::from_chars(text, end, *out);
    return result.ec == std::errc() && result.ptr == end && end != text && std::isfinite(*out);
}

// Index into descriptor->values, or -1.
static int32_t enum_index(const SettingDescriptor* descriptor, const char* value)
{
    for (uint32_t i = 0; i < descriptor->value_count; i++)
    {
        if (std::strcmp(descriptor->values[i].key, value) == 0)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool settings_value_is_valid(const SettingDescriptor* descriptor, const char* value)
{
    switch (descriptor->type)
    {
    case SettingType::boolean:
        return std::strcmp(value, "true") == 0 || std::strcmp(value, "false") == 0;
    case SettingType::integer:
    {
        // Range only: `step` is for the UI, a hand-edited value off the step is fine.
        int32_t number = 0;
        return parse_int(value, &number) && number >= descriptor->min && number <= descriptor->max;
    }
    case SettingType::real:
    {
        double number = 0.0;
        return parse_real(value, &number) && number >= descriptor->min_real && number <= descriptor->max_real;
    }
    case SettingType::enumeration:
        return enum_index(descriptor, value) >= 0;
    case SettingType::path:
    case SettingType::text:
    {
        // Anything the settings file reads back unchanged.
        size_t length = std::strlen(value);
        bool outer_blanks = length > 0 && (is_blank(value[0]) || is_blank(value[length - 1]));
        return !has_line_break(value) && !outer_blanks;
    }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Descriptors and layers
// ---------------------------------------------------------------------------

const SettingDescriptor* settings_find(const Settings* settings, const char* key)
{
    for (uint32_t i = 0; i < settings->descriptor_count; i++)
    {
        if (std::strcmp(settings->descriptors[i].key, key) == 0)
        {
            return &settings->descriptors[i];
        }
    }
    return nullptr;
}

// The section of `layer`, or nullptr when that layer doesn't exist right now
// (defaults, or no core/game running).
static const char* layer_section(const Settings* settings, SettingLayer layer)
{
    switch (layer)
    {
    case SettingLayer::defaults:
        return nullptr;
    case SettingLayer::global:
        return "";
    case SettingLayer::core:
        return settings->core_section.empty() ? nullptr : settings->core_section.c_str();
    case SettingLayer::game:
        return settings->game_section.empty() ? nullptr : settings->game_section.c_str();
    }
    return nullptr;
}

static const char* layer_name(SettingLayer layer)
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

// Would this layer's value be used for this setting? (Global-only settings
// only count in the global layer.)
static bool layer_allowed(const SettingDescriptor* descriptor, SettingLayer layer)
{
    return layer == SettingLayer::global || descriptor->overridable;
}

bool settings_init(Settings* settings, const SettingDescriptor* descriptors, uint32_t descriptor_count)
{
    *settings = Settings{};
    settings->descriptors = descriptors;
    settings->descriptor_count = descriptor_count;

    bool ok = true;
    for (uint32_t i = 0; i < descriptor_count; i++)
    {
        const SettingDescriptor* descriptor = &descriptors[i];
        if (descriptor->key == nullptr || descriptor->key[0] == '\0' || descriptor->default_value == nullptr)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings table: entry %u has no key or no default", i);
            ok = false;
            continue;
        }
        for (uint32_t j = 0; j < i; j++)
        {
            if (descriptors[j].key != nullptr && std::strcmp(descriptors[j].key, descriptor->key) == 0)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings table: key %s appears twice", descriptor->key);
                ok = false;
            }
        }
        if (!settings_value_is_valid(descriptor, descriptor->default_value))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings table: invalid default \"%s\" for %s",
                         descriptor->default_value, descriptor->key);
            ok = false;
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Load / save / context
// ---------------------------------------------------------------------------

bool settings_load(Settings* settings, const char* path)
{
    settings->dirty = false;
    if (!settings_file_load(&settings->file, path))
    {
        return false;
    }

    // Report what won't be used. Nothing is removed: unknown keys and
    // sections may belong to a newer version, and a bad value stays visible
    // in the file until the user changes that setting.
    for (size_t i = 0; i < settings->file.sections.size(); i++)
    {
        const SettingsFileSection* section = &settings->file.sections[i];
        const char* shown_name = section->name.empty() ? "global" : section->name.c_str();
        SettingLayer layer = SettingLayer::global;
        if (starts_with(section->name.c_str(), CORE_PREFIX))
        {
            layer = SettingLayer::core;
        }
        else if (starts_with(section->name.c_str(), GAME_PREFIX))
        {
            layer = SettingLayer::game;
        }
        else if (!section->name.empty())
        {
            continue;
        }

        for (size_t j = 0; j < section->entries.size(); j++)
        {
            const SettingsFileEntry* entry = &section->entries[j];
            const SettingDescriptor* descriptor = settings_find(settings, entry->key.c_str());
            if (descriptor == nullptr)
            {
                continue;
            }
            if (!layer_allowed(descriptor, layer))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: [%s] %s is a global setting, ignored here", path,
                            shown_name, entry->key.c_str());
            }
            else if (!settings_value_is_valid(descriptor, entry->value.c_str()))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: [%s] %s = \"%s\" is not a valid value, ignored", path,
                            shown_name, entry->key.c_str(), entry->value.c_str());
            }
        }
    }
    return true;
}

bool settings_save(Settings* settings, const char* path, const char* title)
{
    std::string header = title;
    header += "\nThis file is rewritten on save: comments are not kept.";
    if (!settings_file_save(&settings->file, path, header.c_str()))
    {
        return false;
    }
    settings->dirty = false;
    return true;
}

bool settings_set_context(Settings* settings, const char* core_name, const char* game_name)
{
    bool has_core = core_name != nullptr && core_name[0] != '\0';
    bool has_game = game_name != nullptr && game_name[0] != '\0';
    if (has_game && !has_core)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: a game (%s) needs a core", game_name);
        return false;
    }
    if (has_core && (has_line_break(core_name) || std::strchr(core_name, '/') != nullptr))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: invalid core name \"%s\"", core_name);
        return false;
    }
    if (has_game && has_line_break(game_name))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: invalid game name \"%s\"", game_name);
        return false;
    }

    settings->core_section.clear();
    settings->game_section.clear();
    if (has_core)
    {
        settings->core_section = CORE_PREFIX;
        settings->core_section += core_name;
    }
    if (has_game)
    {
        settings->game_section = GAME_PREFIX;
        settings->game_section += core_name;
        settings->game_section += "/";
        settings->game_section += game_name;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Get
// ---------------------------------------------------------------------------

const char* settings_get(const Settings* settings, const char* key, SettingLayer* source)
{
    const SettingDescriptor* descriptor = settings_find(settings, key);
    if (descriptor == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: unknown setting %s", key);
        return nullptr;
    }

    // Most specific first.
    static const SettingLayer ORDER[] = {SettingLayer::game, SettingLayer::core, SettingLayer::global};
    for (uint32_t i = 0; i < sizeof(ORDER) / sizeof(ORDER[0]); i++)
    {
        const char* section = layer_section(settings, ORDER[i]);
        if (section == nullptr || !layer_allowed(descriptor, ORDER[i]))
        {
            continue;
        }
        const char* value = settings_file_get(&settings->file, section, key);
        if (value != nullptr && settings_value_is_valid(descriptor, value))
        {
            if (source != nullptr)
            {
                *source = ORDER[i];
            }
            return value;
        }
    }
    if (source != nullptr)
    {
        *source = SettingLayer::defaults;
    }
    return descriptor->default_value;
}

// The effective value of a setting that must be of `type` (or `other_type`),
// or nullptr after logging the programming error.
static const char* get_typed(const Settings* settings, const char* key, SettingType type, SettingType other_type,
                             const SettingDescriptor** descriptor)
{
    *descriptor = settings_find(settings, key);
    if (*descriptor == nullptr || ((*descriptor)->type != type && (*descriptor)->type != other_type))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: %s is unknown or not of the type asked for", key);
        return nullptr;
    }
    return settings_get(settings, key, nullptr);
}

bool settings_get_bool(const Settings* settings, const char* key)
{
    const SettingDescriptor* descriptor = nullptr;
    const char* value = get_typed(settings, key, SettingType::boolean, SettingType::boolean, &descriptor);
    return value != nullptr && std::strcmp(value, "true") == 0;
}

int32_t settings_get_int(const Settings* settings, const char* key)
{
    const SettingDescriptor* descriptor = nullptr;
    const char* value = get_typed(settings, key, SettingType::integer, SettingType::integer, &descriptor);
    int32_t number = 0;
    if (value == nullptr || !parse_int(value, &number))
    {
        return 0;
    }
    return number;
}

double settings_get_real(const Settings* settings, const char* key)
{
    const SettingDescriptor* descriptor = nullptr;
    const char* value = get_typed(settings, key, SettingType::real, SettingType::real, &descriptor);
    double number = 0.0;
    if (value == nullptr || !parse_real(value, &number))
    {
        return 0.0;
    }
    return number;
}

uint32_t settings_get_enum(const Settings* settings, const char* key)
{
    const SettingDescriptor* descriptor = nullptr;
    const char* value = get_typed(settings, key, SettingType::enumeration, SettingType::enumeration, &descriptor);
    if (value == nullptr)
    {
        return 0;
    }
    int32_t index = enum_index(descriptor, value);
    return index >= 0 ? static_cast<uint32_t>(index) : 0;
}

const char* settings_get_text(const Settings* settings, const char* key)
{
    const SettingDescriptor* descriptor = nullptr;
    const char* value = get_typed(settings, key, SettingType::path, SettingType::text, &descriptor);
    return value != nullptr ? value : "";
}

const char* settings_get_layer(const Settings* settings, SettingLayer layer, const char* key)
{
    const char* section = layer_section(settings, layer);
    if (section == nullptr)
    {
        return nullptr;
    }
    return settings_file_get(&settings->file, section, key);
}

// ---------------------------------------------------------------------------
// Set / reset
// ---------------------------------------------------------------------------

bool settings_set(Settings* settings, SettingLayer layer, const char* key, const char* value)
{
    const SettingDescriptor* descriptor = settings_find(settings, key);
    if (descriptor == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: unknown setting %s", key);
        return false;
    }
    const char* section = layer_section(settings, layer);
    if (section == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: no %s layer to set %s in", layer_name(layer), key);
        return false;
    }
    if (!layer_allowed(descriptor, layer))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: %s is global-only, can't set it per %s", key,
                     layer_name(layer));
        return false;
    }
    if (!settings_value_is_valid(descriptor, value))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: \"%s\" is not a valid value for %s", value, key);
        return false;
    }
    if (!settings_file_set(&settings->file, section, key, value))
    {
        return false;
    }
    settings->dirty = true;
    return true;
}

bool settings_reset(Settings* settings, SettingLayer layer, const char* key)
{
    const char* section = layer_section(settings, layer);
    if (section == nullptr)
    {
        return false;
    }
    if (!settings_file_remove(&settings->file, section, key))
    {
        return false;
    }
    settings->dirty = true;
    return true;
}
