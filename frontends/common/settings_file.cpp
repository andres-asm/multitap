#include "settings_file.h"

#include <cstring>

#include <SDL3/SDL.h>

#include "host_fs.h"

// Arrays start this big and double when full.
constexpr uint32_t SETTINGS_FILE_FIRST_CAPACITY = 8;

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

// [begin, begin + length) as a new[] NUL-terminated string.
static char* copy_string(const char* begin, size_t length)
{
    char* copy = new char[length + 1];
    std::memcpy(copy, begin, length);
    copy[length] = '\0';
    return copy;
}

// Narrows [*begin, *end) to drop blanks on both sides.
static void trim(const char** begin, const char** end)
{
    while (*begin < *end && is_blank(**begin))
    {
        (*begin)++;
    }
    while (*end > *begin && is_blank(*(*end - 1)))
    {
        (*end)--;
    }
}

static bool has_line_break(const char* text)
{
    for (const char* c = text; *c != '\0'; c++)
    {
        if (is_line_break(*c))
        {
            return true;
        }
    }
    return false;
}

static bool has_outer_blanks(const char* text)
{
    size_t length = std::strlen(text);
    return length > 0 && (is_blank(text[0]) || is_blank(text[length - 1]));
}

// Same text as [begin, begin + length)?
static bool equals(const char* text, const char* begin, size_t length)
{
    return std::strncmp(text, begin, length) == 0 && text[length] == '\0';
}

// ---------------------------------------------------------------------------
// Sections and entries
// ---------------------------------------------------------------------------

static void free_section(SettingsFileSection* section)
{
    for (uint32_t i = 0; i < section->entry_count; i++)
    {
        delete[] section->entries[i].key;
        delete[] section->entries[i].value;
    }
    delete[] section->entries;
    delete[] section->name;
    *section = SettingsFileSection{};
}

void settings_file_destroy(SettingsFile* file)
{
    for (uint32_t i = 0; i < file->section_count; i++)
    {
        free_section(&file->sections[i]);
    }
    delete[] file->sections;
    *file = SettingsFile{};
}

static int32_t find_section(const SettingsFile* file, const char* name, size_t length)
{
    for (uint32_t i = 0; i < file->section_count; i++)
    {
        if (equals(file->sections[i].name, name, length))
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t settings_file_find_section(const SettingsFile* file, const char* section)
{
    return find_section(file, section, std::strlen(section));
}

// The section called [name, name + length), created at the end if missing.
static SettingsFileSection* get_or_add_section(SettingsFile* file, const char* name, size_t length)
{
    int32_t index = find_section(file, name, length);
    if (index >= 0)
    {
        return &file->sections[index];
    }

    if (file->section_count == file->section_capacity)
    {
        uint32_t capacity = file->section_capacity == 0 ? SETTINGS_FILE_FIRST_CAPACITY : file->section_capacity * 2;
        SettingsFileSection* sections = new SettingsFileSection[capacity];
        for (uint32_t i = 0; i < file->section_count; i++)
        {
            sections[i] = file->sections[i];
        }
        delete[] file->sections;
        file->sections = sections;
        file->section_capacity = capacity;
    }

    SettingsFileSection* section = &file->sections[file->section_count];
    file->section_count++;
    *section = SettingsFileSection{};
    section->name = copy_string(name, length);
    return section;
}

static int32_t find_entry(const SettingsFileSection* section, const char* key, size_t length)
{
    for (uint32_t i = 0; i < section->entry_count; i++)
    {
        if (equals(section->entries[i].key, key, length))
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// Adds or replaces. Strings are [begin, begin + length) ranges.
static void put_entry(SettingsFileSection* section, const char* key, size_t key_length, const char* value,
                      size_t value_length)
{
    int32_t index = find_entry(section, key, key_length);
    if (index >= 0)
    {
        SettingsFileEntry* entry = &section->entries[index];
        delete[] entry->value;
        entry->value = copy_string(value, value_length);
        return;
    }

    if (section->entry_count == section->entry_capacity)
    {
        uint32_t capacity = section->entry_capacity == 0 ? SETTINGS_FILE_FIRST_CAPACITY : section->entry_capacity * 2;
        SettingsFileEntry* entries = new SettingsFileEntry[capacity];
        for (uint32_t i = 0; i < section->entry_count; i++)
        {
            entries[i] = section->entries[i];
        }
        delete[] section->entries;
        section->entries = entries;
        section->entry_capacity = capacity;
    }

    SettingsFileEntry* entry = &section->entries[section->entry_count];
    section->entry_count++;
    entry->key = copy_string(key, key_length);
    entry->value = copy_string(value, value_length);
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

void settings_file_parse(SettingsFile* file, const char* text, size_t size, const char* source_name)
{
    settings_file_destroy(file);

    const char* cursor = text;
    const char* text_end = text + size;

    // Editors on Windows like to start UTF-8 files with a byte order mark.
    if (size >= 3 && std::memcmp(text, "\xEF\xBB\xBF", 3) == 0)
    {
        cursor += 3;
    }

    // Where keys go. Starts as the global section; nullptr after a broken
    // header, so its keys are dropped instead of landing in the wrong section.
    SettingsFileSection* current = get_or_add_section(file, "", 0);
    uint32_t line_number = 0;

    while (cursor < text_end)
    {
        line_number++;
        const char* line = cursor;
        const char* line_end = cursor;
        while (line_end < text_end && *line_end != '\n')
        {
            line_end++;
        }
        cursor = line_end < text_end ? line_end + 1 : text_end;

        trim(&line, &line_end);
        if (line == line_end || *line == '#')
        {
            continue;
        }

        if (*line == '[')
        {
            const char* close = line_end - 1;
            while (close > line && *close != ']')
            {
                close--;
            }
            if (close == line)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%u: section header without ']', its keys are ignored",
                            source_name, line_number);
                current = nullptr;
                continue;
            }
            if (close + 1 != line_end)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%u: text after the section header ignored",
                            source_name, line_number);
            }
            const char* name = line + 1;
            const char* name_end = close;
            trim(&name, &name_end);
            current = get_or_add_section(file, name, static_cast<size_t>(name_end - name));
            continue;
        }

        const char* equal_sign = line;
        while (equal_sign < line_end && *equal_sign != '=')
        {
            equal_sign++;
        }
        if (equal_sign == line_end)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%u: line without '=' ignored", source_name, line_number);
            continue;
        }

        const char* key = line;
        const char* key_end = equal_sign;
        trim(&key, &key_end);
        const char* value = equal_sign + 1;
        const char* value_end = line_end;
        trim(&value, &value_end);
        if (key == key_end)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%u: empty key ignored", source_name, line_number);
            continue;
        }
        if (current == nullptr)
        {
            continue;
        }
        put_entry(current, key, static_cast<size_t>(key_end - key), value, static_cast<size_t>(value_end - value));
    }
}

bool settings_file_load(SettingsFile* file, const char* path)
{
    settings_file_destroy(file);
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

// Appends `length` bytes at *position. With out == nullptr it only counts,
// so the same code measures the text first and then writes it.
static void append(char* out, size_t* position, const char* text, size_t length)
{
    if (out != nullptr)
    {
        std::memcpy(out + *position, text, length);
    }
    *position += length;
}

static void append_string(char* out, size_t* position, const char* text)
{
    append(out, position, text, std::strlen(text));
}

static void write_entries(char* out, size_t* position, const SettingsFileSection* section)
{
    for (uint32_t i = 0; i < section->entry_count; i++)
    {
        append_string(out, position, section->entries[i].key);
        append_string(out, position, " = ");
        append_string(out, position, section->entries[i].value);
        append_string(out, position, "\n");
    }
}

// Returns the number of bytes written (or that would be written).
static size_t write_text(const SettingsFile* file, const char* header, char* out)
{
    size_t position = 0;
    bool wrote_something = false;

    if (header != nullptr && header[0] != '\0')
    {
        const char* line = header;
        while (true)
        {
            const char* line_end = line;
            while (*line_end != '\0' && *line_end != '\n')
            {
                line_end++;
            }
            append_string(out, &position, "# ");
            append(out, &position, line, static_cast<size_t>(line_end - line));
            append_string(out, &position, "\n");
            if (*line_end == '\0')
            {
                break;
            }
            line = line_end + 1;
        }
        wrote_something = true;
    }

    int32_t global = find_section(file, "", 0);
    if (global >= 0 && file->sections[global].entry_count > 0)
    {
        if (wrote_something)
        {
            append_string(out, &position, "\n");
        }
        write_entries(out, &position, &file->sections[global]);
        wrote_something = true;
    }

    for (uint32_t i = 0; i < file->section_count; i++)
    {
        const SettingsFileSection* section = &file->sections[i];
        if (section->name[0] == '\0' || section->entry_count == 0)
        {
            continue;
        }
        if (wrote_something)
        {
            append_string(out, &position, "\n");
        }
        append_string(out, &position, "[");
        append_string(out, &position, section->name);
        append_string(out, &position, "]\n");
        write_entries(out, &position, section);
        wrote_something = true;
    }
    return position;
}

bool settings_file_to_text(const SettingsFile* file, const char* header, char** text, size_t* size)
{
    size_t length = write_text(file, header, nullptr);
    // At least one byte so an empty file still gets a buffer to delete[].
    char* buffer = new char[length > 0 ? length : 1];
    size_t written = write_text(file, header, buffer);
    if (written != length)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings text size changed while writing (%zu, then %zu)",
                     length, written);
        delete[] buffer;
        return false;
    }
    *text = buffer;
    *size = length;
    return true;
}

bool settings_file_save(const SettingsFile* file, const char* path, const char* header)
{
    char* text = nullptr;
    size_t size = 0;
    if (!settings_file_to_text(file, header, &text, &size))
    {
        return false;
    }
    bool saved = host_fs_write_file(path, text, size);
    delete[] text;
    return saved;
}

// ---------------------------------------------------------------------------
// Get / set / remove
// ---------------------------------------------------------------------------

const char* settings_file_get(const SettingsFile* file, const char* section, const char* key)
{
    int32_t section_index = settings_file_find_section(file, section);
    if (section_index < 0)
    {
        return nullptr;
    }
    const SettingsFileSection* found = &file->sections[section_index];
    int32_t entry_index = find_entry(found, key, std::strlen(key));
    if (entry_index < 0)
    {
        return nullptr;
    }
    return found->entries[entry_index].value;
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

    SettingsFileSection* found = get_or_add_section(file, section, std::strlen(section));
    put_entry(found, key, std::strlen(key), value, std::strlen(value));
    return true;
}

bool settings_file_remove(SettingsFile* file, const char* section, const char* key)
{
    int32_t section_index = settings_file_find_section(file, section);
    if (section_index < 0)
    {
        return false;
    }
    SettingsFileSection* found = &file->sections[section_index];
    int32_t entry_index = find_entry(found, key, std::strlen(key));
    if (entry_index < 0)
    {
        return false;
    }

    uint32_t index = static_cast<uint32_t>(entry_index);
    delete[] found->entries[index].key;
    delete[] found->entries[index].value;
    for (uint32_t i = index + 1; i < found->entry_count; i++)
    {
        found->entries[i - 1] = found->entries[i];
    }
    found->entry_count--;
    found->entries[found->entry_count] = SettingsFileEntry{};
    return true;
}
