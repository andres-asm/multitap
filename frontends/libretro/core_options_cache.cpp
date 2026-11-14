#include "core_options_cache.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

bool core_options_cache_load(CoreOptionsCache* cache, const char* path)
{
    cache->dirty = false;
    return settings_file_load(&cache->file, path);
}

bool core_options_cache_save(CoreOptionsCache* cache, const char* path)
{
    if (!settings_file_save(&cache->file, path, "Core options each core declared (rebuilt when it runs)"))
    {
        return false;
    }
    cache->dirty = false;
    return true;
}

// Does `name` belong to `core` ("core/<key>")?
static bool section_of(const std::string& name, const char* core, size_t core_length)
{
    return name.size() > core_length + 1 && name.compare(0, core_length, core) == 0 && name[core_length] == '/';
}

// Text the file can hold: line breaks become spaces, outer spaces trimmed.
static std::string flat(const char* text)
{
    std::string result = text != nullptr ? text : "";
    for (size_t i = 0; i < result.size(); i++)
    {
        if (result[i] == '\n' || result[i] == '\r' || result[i] == '\t')
        {
            result[i] = ' ';
        }
    }
    size_t start = result.find_first_not_of(' ');
    size_t end = result.find_last_not_of(' ');
    return start == std::string::npos ? std::string() : result.substr(start, end - start + 1);
}

static bool same_entries(const SettingsFileSection* a, const SettingsFileSection* b)
{
    if (a->name != b->name || a->entries.size() != b->entries.size())
    {
        return false;
    }
    for (size_t i = 0; i < a->entries.size(); i++)
    {
        if (a->entries[i].key != b->entries[i].key || a->entries[i].value != b->entries[i].value)
        {
            return false;
        }
    }
    return true;
}

static void add_entry(SettingsFileSection* section, const char* key, const std::string& value)
{
    SettingsFileEntry entry = {};
    entry.key = key;
    entry.value = value;
    section->entries.push_back(entry);
}

// The section for one option, or false if its values can't be stored as they are.
static bool build_section(const char* core, const LibretroOptions* options, const LibretroOption* option,
                          SettingsFileSection* section)
{
    section->name = std::string(core) + "/" + option->key;
    section->entries.clear();
    add_entry(section, "label", flat(option->label != nullptr ? option->label : option->key));
    for (uint32_t c = 0; option->category_key != nullptr && c < options->category_count; c++)
    {
        if (std::strcmp(options->categories[c].key, option->category_key) == 0)
        {
            add_entry(section, "category", flat(options->categories[c].label));
        }
    }
    std::string info = flat(option->info);
    if (!info.empty())
    {
        add_entry(section, "info", info);
    }
    for (uint32_t v = 0; v < option->value_count; v++)
    {
        std::string value = option->values[v] != nullptr ? option->values[v] : "";
        if (value != flat(value.c_str()))
        {
            return false;   // the file would read it back differently
        }
    }
    add_entry(section, "default", option->values[option->default_index]);
    for (uint32_t v = 0; v < option->value_count; v++)
    {
        char key[32] = {};
        std::snprintf(key, sizeof(key), "value%u", v + 1);
        add_entry(section, key, option->values[v]);
        if (option->value_labels[v] != nullptr && std::strcmp(option->value_labels[v], option->values[v]) != 0)
        {
            std::snprintf(key, sizeof(key), "label%u", v + 1);
            add_entry(section, key, flat(option->value_labels[v]));
        }
    }
    return true;
}

void core_options_cache_update(CoreOptionsCache* cache, const char* core, const LibretroOptions* options)
{
    if (core == nullptr || core[0] == '\0')
    {
        return;
    }
    std::vector<SettingsFileSection> fresh;
    for (uint32_t i = 0; i < options->count; i++)
    {
        SettingsFileSection section = {};
        if (build_section(core, options, &options->options[i], &section))
        {
            fresh.push_back(section);
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Core options cache: %s has values the file can't hold; not cached",
                        options->options[i].key);
        }
    }

    size_t core_length = std::strlen(core);
    std::vector<SettingsFileSection>* sections = &cache->file.sections;
    std::vector<size_t> old;
    for (size_t i = 0; i < sections->size(); i++)
    {
        if (section_of((*sections)[i].name, core, core_length))
        {
            old.push_back(i);
        }
    }
    bool same = old.size() == fresh.size();
    for (size_t i = 0; same && i < old.size(); i++)
    {
        same = same_entries(&(*sections)[old[i]], &fresh[i]);
    }
    if (same)
    {
        return;
    }
    for (size_t i = old.size(); i > 0; i--)
    {
        sections->erase(sections->begin() + static_cast<long>(old[i - 1]));
    }
    for (size_t i = 0; i < fresh.size(); i++)
    {
        sections->push_back(fresh[i]);
    }
    cache->dirty = true;
}

void core_options_cache_cores(const CoreOptionsCache* cache, std::vector<std::string>* cores)
{
    cores->clear();
    for (size_t i = 0; i < cache->file.sections.size(); i++)
    {
        const std::string& name = cache->file.sections[i].name;
        size_t slash = name.find('/');
        if (slash == std::string::npos || slash == 0)
        {
            continue;
        }
        std::string core = name.substr(0, slash);
        bool known = false;
        for (size_t c = 0; c < cores->size(); c++)
        {
            known = known || (*cores)[c] == core;
        }
        if (!known)
        {
            cores->push_back(core);
        }
    }
}

static const char* entry(const SettingsFileSection* section, const char* key)
{
    for (size_t i = 0; i < section->entries.size(); i++)
    {
        if (section->entries[i].key == key)
        {
            return section->entries[i].value.c_str();
        }
    }
    return nullptr;
}

void core_options_cache_get(const CoreOptionsCache* cache, const char* core, std::vector<CachedCoreOption>* options)
{
    options->clear();
    size_t core_length = std::strlen(core);
    for (size_t i = 0; i < cache->file.sections.size(); i++)
    {
        const SettingsFileSection* section = &cache->file.sections[i];
        if (!section_of(section->name, core, core_length))
        {
            continue;
        }
        CachedCoreOption option = {};
        option.key = section->name.substr(core_length + 1);
        const char* text = entry(section, "label");
        option.label = text != nullptr ? text : option.key;
        text = entry(section, "category");
        option.category = text != nullptr ? text : "";
        text = entry(section, "info");
        option.info = text != nullptr ? text : "";
        text = entry(section, "default");
        option.default_value = text != nullptr ? text : "";
        for (uint32_t v = 1;; v++)
        {
            char key[32] = {};
            std::snprintf(key, sizeof(key), "value%u", v);
            const char* value = entry(section, key);
            if (value == nullptr)
            {
                break;
            }
            std::snprintf(key, sizeof(key), "label%u", v);
            const char* label = entry(section, key);
            option.values.push_back(value);
            option.labels.push_back(label != nullptr ? label : "");
        }
        options->push_back(option);
    }
}

bool core_options_cache_has(const CoreOptionsCache* cache, const char* core, const char* key)
{
    std::string name = std::string(core) + "/" + key;
    return settings_file_find_section(&cache->file, name.c_str()) >= 0;
}
