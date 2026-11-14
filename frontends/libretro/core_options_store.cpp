#include "core_options_store.h"

#include <cstring>

#include <SDL3/SDL.h>

bool core_options_store_load(CoreOptionsStore* store, const char* path)
{
    store->dirty = false;
    return settings_file_load(&store->file, path);
}

bool core_options_store_save(CoreOptionsStore* store, const char* path)
{
    if (!settings_file_save(&store->file, path, "Core options per core, content folder and game\n"
                                                "[core], [core/dir:folder name], [core/game:game name]"))
    {
        return false;
    }
    store->dirty = false;
    return true;
}

bool core_options_store_set_context(CoreOptionsStore* store, const char* core, const char* folder, const char* game)
{
    store->core_section.clear();
    store->directory_section.clear();
    store->game_section.clear();
    if (core == nullptr || core[0] == '\0')
    {
        return true;
    }
    if (std::strchr(core, '/') != nullptr || std::strchr(core, '\n') != nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Core options: core name \"%s\" can't be a section", core);
        return false;
    }
    store->core_section = core;
    if (folder != nullptr && folder[0] != '\0')
    {
        store->directory_section = store->core_section + "/dir:" + folder;
    }
    if (game != nullptr && game[0] != '\0')
    {
        store->game_section = store->core_section + "/game:" + game;
    }
    return true;
}

const std::string* core_options_store_section(const CoreOptionsStore* store, CoreOptionLayer layer)
{
    switch (layer)
    {
    case CoreOptionLayer::core:
        return &store->core_section;
    case CoreOptionLayer::directory:
        return &store->directory_section;
    case CoreOptionLayer::game:
        return &store->game_section;
    }
    return &store->core_section;
}

const char* core_options_store_get_layer(const CoreOptionsStore* store, CoreOptionLayer layer, const char* key)
{
    const std::string* section = core_options_store_section(store, layer);
    if (section->empty())
    {
        return nullptr;
    }
    return settings_file_get(&store->file, section->c_str(), key);
}

// The layers from `layer` down, most specific first.
static uint32_t layers_from(CoreOptionLayer layer, CoreOptionLayer* layers)
{
    uint32_t count = 0;
    if (layer == CoreOptionLayer::game)
    {
        layers[count++] = CoreOptionLayer::game;
    }
    if (layer == CoreOptionLayer::game || layer == CoreOptionLayer::directory)
    {
        layers[count++] = CoreOptionLayer::directory;
    }
    layers[count++] = CoreOptionLayer::core;
    return count;
}

const char* core_options_store_get_at(const CoreOptionsStore* store, CoreOptionLayer layer, const char* key,
                                      CoreOptionLayer* source)
{
    CoreOptionLayer layers[3] = {};
    uint32_t count = layers_from(layer, layers);
    for (uint32_t i = 0; i < count; i++)
    {
        const char* value = core_options_store_get_layer(store, layers[i], key);
        if (value != nullptr)
        {
            if (source != nullptr)
            {
                *source = layers[i];
            }
            return value;
        }
    }
    return nullptr;
}

const char* core_options_store_get(const CoreOptionsStore* store, const char* key, CoreOptionLayer* source)
{
    return core_options_store_get_at(store, CoreOptionLayer::game, key, source);
}

// What applies below `layer` (nullptr = the default).
static const char* value_below(const CoreOptionsStore* store, CoreOptionLayer layer, const char* key)
{
    switch (layer)
    {
    case CoreOptionLayer::game:
        return core_options_store_get_at(store, CoreOptionLayer::directory, key, nullptr);
    case CoreOptionLayer::directory:
        return core_options_store_get_at(store, CoreOptionLayer::core, key, nullptr);
    case CoreOptionLayer::core:
        return nullptr;
    }
    return nullptr;
}

bool core_options_store_set(CoreOptionsStore* store, CoreOptionLayer layer, const char* key, const char* value,
                            const char* default_value)
{
    const std::string* section = core_options_store_section(store, layer);
    if (section->empty() || key == nullptr || value == nullptr)
    {
        return false;
    }
    const char* below = value_below(store, layer, key);
    if (below == nullptr)
    {
        below = default_value;
    }
    if (below != nullptr && std::strcmp(below, value) == 0)
    {
        core_options_store_reset(store, layer, key);   // the same as below: not written
        return true;
    }
    const char* current = settings_file_get(&store->file, section->c_str(), key);
    if (current != nullptr && std::strcmp(current, value) == 0)
    {
        return true;
    }
    if (!settings_file_set(&store->file, section->c_str(), key, value))
    {
        return false;
    }
    store->dirty = true;
    return true;
}

bool core_options_store_reset(CoreOptionsStore* store, CoreOptionLayer layer, const char* key)
{
    const std::string* section = core_options_store_section(store, layer);
    if (section->empty() || !settings_file_remove(&store->file, section->c_str(), key))
    {
        return false;
    }
    store->dirty = true;
    return true;
}

bool core_options_store_layer_has_values(const CoreOptionsStore* store, CoreOptionLayer layer)
{
    const std::string* section = core_options_store_section(store, layer);
    if (section->empty())
    {
        return false;
    }
    int32_t index = settings_file_find_section(&store->file, section->c_str());
    return index >= 0 && !store->file.sections[static_cast<size_t>(index)].entries.empty();
}

void core_options_store_effective(const CoreOptionsStore* store, std::vector<LibretroOptionValue>* values)
{
    values->clear();
    // Most specific first; a key already taken from a more specific layer is skipped.
    CoreOptionLayer layers[3] = {CoreOptionLayer::game, CoreOptionLayer::directory, CoreOptionLayer::core};
    for (uint32_t l = 0; l < 3; l++)
    {
        const std::string* section = core_options_store_section(store, layers[l]);
        int32_t index = section->empty() ? -1 : settings_file_find_section(&store->file, section->c_str());
        if (index < 0)
        {
            continue;
        }
        const SettingsFileSection* entries = &store->file.sections[static_cast<size_t>(index)];
        for (size_t e = 0; e < entries->entries.size(); e++)
        {
            const char* key = entries->entries[e].key.c_str();
            bool taken = false;
            for (size_t v = 0; v < values->size(); v++)
            {
                if (std::strcmp((*values)[v].key, key) == 0)
                {
                    taken = true;
                    break;
                }
            }
            if (!taken)
            {
                LibretroOptionValue value = {};
                value.key = key;
                value.value = entries->entries[e].value.c_str();
                values->push_back(value);
            }
        }
    }
}

void core_options_store_sections_of(const CoreOptionsStore* store, const char* core, std::vector<std::string>* sections)
{
    sections->clear();
    size_t length = std::strlen(core);
    for (size_t i = 0; i < store->file.sections.size(); i++)
    {
        const std::string& name = store->file.sections[i].name;
        bool own = name == core;
        bool below = name.size() > length && name.compare(0, length, core) == 0 && name[length] == '/';
        if ((own || below) && !store->file.sections[i].entries.empty())
        {
            sections->push_back(name);
        }
    }
}

bool core_options_store_remove_section(CoreOptionsStore* store, const char* section)
{
    int32_t index = settings_file_find_section(&store->file, section);
    if (index < 0)
    {
        return false;
    }
    store->file.sections.erase(store->file.sections.begin() + index);
    store->dirty = true;
    return true;
}

bool core_options_store_remove(CoreOptionsStore* store, const char* section, const char* key)
{
    if (!settings_file_remove(&store->file, section, key))
    {
        return false;
    }
    store->dirty = true;
    return true;
}
