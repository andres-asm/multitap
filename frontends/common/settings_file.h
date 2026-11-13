#pragma once

// Settings file: our own small INI dialect. Holds the raw text of every
// key/value per section; knows nothing about what the settings mean (that is
// settings.*). Kept behind this interface so the format can change (TOML once
// nested data arrives) without touching the callers.
//
// Format rules (tested in settings_file_test):
//   - UTF-8. A UTF-8 byte order mark at the start is skipped. Lines end in
//     "\n" or "\r\n". Bytes are not validated: names and values pass through.
//   - Leading whitespace on a line is ignored.
//   - A line starting with '#' is a comment. '#' anywhere else is ordinary
//     text ("key = a # b" has the value "a # b").
//   - "[name]" starts a section. The name runs from the first '[' to the LAST
//     ']' on the line, so names may contain brackets: "[game:snes9x/Pang [b]]"
//     is the section "game:snes9x/Pang [b]". Whitespace around the name is
//     trimmed. A header without ']' is reported and its keys are dropped up to
//     the next valid header. A repeated header continues the earlier section.
//   - "key = value": the key runs to the first '='; the value is the rest of
//     the line. Whitespace around both is trimmed. No quoting or escaping.
//     A repeated key keeps the last value. Lines without '=' are reported and
//     skipped.
//   - Keys before the first header belong to the global section, named "".
//   - Every key is kept, known or not, and written back on save (forward
//     compatibility). Comments are NOT kept: saving rewrites the file.
//
// Writing: the header comment, then the global keys, then each section in
// the order it first appeared. Sections without keys are not written.

#include <cstddef>
#include <cstdint>

struct SettingsFileEntry
{
    char* key = nullptr;     // new[], owned
    char* value = nullptr;   // new[], owned
};

struct SettingsFileSection
{
    char* name = nullptr;    // new[], owned; "" for the global section
    SettingsFileEntry* entries = nullptr;
    uint32_t entry_count = 0;
    uint32_t entry_capacity = 0;
};

// A zero-initialized SettingsFile is an empty file, ready to use.
struct SettingsFile
{
    SettingsFileSection* sections = nullptr;
    uint32_t section_count = 0;
    uint32_t section_capacity = 0;
};

// Frees everything; the file is empty (and usable) afterwards.
void settings_file_destroy(SettingsFile* file);

// Replaces the contents with what `text` holds. Malformed lines are logged
// (with `source_name` and the line number) and skipped; they don't fail.
void settings_file_parse(SettingsFile* file, const char* text, size_t size, const char* source_name);

// Loads `path`. A missing file is not an error: the file is left empty
// (first run). Fails only if the file exists but can't be read.
bool settings_file_load(SettingsFile* file, const char* path);

// The file as text, in a new[] buffer the caller owns (delete[]), not
// NUL-terminated. `header` (may be nullptr) is written as "# " comment lines
// at the top, one per "\n"-separated line.
bool settings_file_to_text(const SettingsFile* file, const char* header, char** text, size_t* size);

// Writes the file with host_fs's atomic write (never left half-written).
bool settings_file_save(const SettingsFile* file, const char* path, const char* header);

// The value of `key` in `section` ("" = global), or nullptr if it isn't set.
// The pointer stays valid until the entry is changed or removed.
const char* settings_file_get(const SettingsFile* file, const char* section, const char* key);

// Sets (adds or replaces) a value, creating the section if needed. Refuses
// (logs, returns false) anything that would not read back the same:
//   section: no line break; no leading/trailing whitespace;
//   key:     not empty; no '=' or line break; doesn't start with '#' or '[';
//            no leading/trailing whitespace;
//   value:   no line break; no leading/trailing whitespace (may be empty).
bool settings_file_set(SettingsFile* file, const char* section, const char* key, const char* value);

// Removes one key. Returns true if it was there.
bool settings_file_remove(SettingsFile* file, const char* section, const char* key);

// Index of `section`, or -1 if it doesn't exist. For walking every key of a
// section through file->sections[index].entries.
int32_t settings_file_find_section(const SettingsFile* file, const char* section);
