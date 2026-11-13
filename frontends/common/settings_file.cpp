#include "settings_file.h"

#include <cstring>
#include <string_view>

#include <SDL3/SDL.h>

#include "host_fs.h"

// ---------------------------------------------------------------------------
// Small string helpers
// ---------------------------------------------------------------------------

static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static bool is_line_break(char c)
{
    return c == '\n' || c == '\r';
}

// Drops blanks on both sides.
static std::string_view trim(std::string_view text)
{
    while (!text.empty() && is_blank(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_blank(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

static bool has_line_break(std::string_view text)
{
    for (size_t i = 0; i < text.size(); i++)
    {
        if (is_line_break(text[i]))
        {
            return true;
        }
    }
    return false;
}

static bool has_outer_blanks(std::string_view text)
{
    return !text.empty() && (is_blank(text.front()) || is_blank(text.back()));
}

// ---------------------------------------------------------------------------
// Sections and entries
// ---------------------------------------------------------------------------

static int32_t find_section(const SettingsFile* file, std::string_view name)
{
    for (size_t i = 0; i < file->sections.size(); i++)
    {
        if (file->sections[i].name == name)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t settings_file_find_section(const SettingsFile* file, const char* section)
{
    return find_section(file, section);
}

// The section called `name`, created at the end if missing. The pointer is
// valid until the next section is added.
static SettingsFileSection* get_or_add_section(SettingsFile* file, std::string_view name)
{
    int32_t index = find_section(file, name);
    if (index >= 0)
    {
        return &file->sections[static_cast<size_t>(index)];
    }
    SettingsFileSection section = {};
    section.name = name;
    file->sections.push_back(section);
    return &file->sections.back();
}

static int32_t find_entry(const SettingsFileSection* section, std::string_view key)
{
    for (size_t i = 0; i < section->entries.size(); i++)
    {
        if (section->entries[i].key == key)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// Adds or replaces.
static void put_entry(SettingsFileSection* section, std::string_view key, std::string_view value)
{
    int32_t index = find_entry(section, key);
    if (index >= 0)
    {
        section->entries[static_cast<size_t>(index)].value = value;
        return;
    }
    SettingsFileEntry entry = {};
    entry.key = key;
    entry.value = value;
    section->entries.push_back(entry);
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

void settings_file_parse(SettingsFile* file, const char* text, size_t size, const char* source_name)
{
    *file = SettingsFile{};
    std::string_view rest(text, size);

    // Editors on Windows like to start UTF-8 files with a byte order mark.
    if (rest.substr(0, 3) == "\xEF\xBB\xBF")
    {
        rest.remove_prefix(3);
    }

    // Index of the section keys go to. Starts as the global section; -1 after
    // a broken header, so its keys are dropped instead of landing in the
    // wrong section. An index, not a pointer: adding sections moves them.
    get_or_add_section(file, "");
    int32_t current = 0;
    uint32_t line_number = 0;

    while (!rest.empty())
    {
        line_number++;
        size_t line_end = rest.find('\n');
        std::string_view line = rest.substr(0, line_end);
        if (line_end == std::string_view::npos)
        {
            rest = std::string_view();
        }
        else
        {
            rest.remove_prefix(line_end + 1);
        }

        line = trim(line);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        if (line.front() == '[')
        {
            size_t close = line.rfind(']');
            if (close == 0 || close == std::string_view::npos)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%u: section header without ']', its keys are ignored",
                            source_name, line_number);
                current = -1;
                continue;
            }
            if (close + 1 != line.size())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%u: text after the section header ignored",
                            source_name, line_number);
            }
            std::string_view name = trim(line.substr(1, close - 1));
            get_or_add_section(file, name);
            current = find_section(file, name);
            continue;
        }

        size_t equal_sign = line.find('=');
        if (equal_sign == std::string_view::npos)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%u: line without '=' ignored", source_name, line_number);
            continue;
        }
        std::string_view key = trim(line.substr(0, equal_sign));
        std::string_view value = trim(line.substr(equal_sign + 1));
        if (key.empty())
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%u: empty key ignored", source_name, line_number);
            continue;
        }
        if (current < 0)
        {
            continue;
        }
        put_entry(&file->sections[static_cast<size_t>(current)], key, value);
    }
}

bool settings_file_load(SettingsFile* file, const char* path)
{
    *file = SettingsFile{};
    if (!host_fs_exists(path))
    {
        return true;
    }

    uint8_t* data = nullptr;
    size_t size = 0;
    if (!host_fs_read_file(path, &data, &size))
    {
        return false;
    }
    // Bytes to chars: same representation, through void* (STYLE.md: no reinterpret_cast).
    const char* text = static_cast<const char*>(static_cast<const void*>(data));
    settings_file_parse(file, text, size, path);
    delete[] data;
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

static void write_entries(std::string* text, const SettingsFileSection* section)
{
    for (size_t i = 0; i < section->entries.size(); i++)
    {
        text->append(section->entries[i].key);
        text->append(" = ");
        text->append(section->entries[i].value);
        text->append("\n");
    }
}

void settings_file_to_text(const SettingsFile* file, const char* header, std::string* text)
{
    text->clear();

    if (header != nullptr && header[0] != '\0')
    {
        std::string_view rest(header);
        while (true)
        {
            size_t line_end = rest.find('\n');
            text->append("# ");
            text->append(rest.substr(0, line_end));
            text->append("\n");
            if (line_end == std::string_view::npos)
            {
                break;
            }
            rest.remove_prefix(line_end + 1);
        }
    }

    int32_t global = find_section(file, "");
    if (global >= 0 && !file->sections[static_cast<size_t>(global)].entries.empty())
    {
        if (!text->empty())
        {
            text->append("\n");
        }
        write_entries(text, &file->sections[static_cast<size_t>(global)]);
    }

    for (size_t i = 0; i < file->sections.size(); i++)
    {
        const SettingsFileSection* section = &file->sections[i];
        if (section->name.empty() || section->entries.empty())
        {
            continue;
        }
        if (!text->empty())
        {
            text->append("\n");
        }
        text->append("[");
        text->append(section->name);
        text->append("]\n");
        write_entries(text, section);
    }
}

bool settings_file_save(const SettingsFile* file, const char* path, const char* header)
{
    std::string text;
    settings_file_to_text(file, header, &text);
    return host_fs_write_file(path, text.data(), text.size());
}

// ---------------------------------------------------------------------------
// Get / set / remove
// ---------------------------------------------------------------------------

const char* settings_file_get(const SettingsFile* file, const char* section, const char* key)
{
    int32_t section_index = find_section(file, section);
    if (section_index < 0)
    {
        return nullptr;
    }
    const SettingsFileSection* found = &file->sections[static_cast<size_t>(section_index)];
    int32_t entry_index = find_entry(found, key);
    if (entry_index < 0)
    {
        return nullptr;
    }
    return found->entries[static_cast<size_t>(entry_index)].value.c_str();
}

bool settings_file_set(SettingsFile* file, const char* section, const char* key, const char* value)
{
    if (has_line_break(section) || has_outer_blanks(section))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: invalid section name \"%s\"", section);
        return false;
    }
    if (key[0] == '\0' || key[0] == '#' || key[0] == '[' || std::strchr(key, '=') != nullptr ||
        has_line_break(key) || has_outer_blanks(key))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: invalid key \"%s\"", key);
        return false;
    }
    if (has_line_break(value) || has_outer_blanks(value))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings: invalid value for %s: \"%s\"", key, value);
        return false;
    }

    put_entry(get_or_add_section(file, section), key, value);
    return true;
}

bool settings_file_remove(SettingsFile* file, const char* section, const char* key)
{
    int32_t section_index = find_section(file, section);
    if (section_index < 0)
    {
        return false;
    }
    SettingsFileSection* found = &file->sections[static_cast<size_t>(section_index)];
    int32_t entry_index = find_entry(found, key);
    if (entry_index < 0)
    {
        return false;
    }
    // erase() only takes an iterator: begin() + index is the entry.
    found->entries.erase(found->entries.begin() + entry_index);
    return true;
}
