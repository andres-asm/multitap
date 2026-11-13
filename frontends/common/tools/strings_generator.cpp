// strings_generator: turns a frontend's English strings file into its string
// IDs, so IDs, English text and the translators' template can't drift apart.
// Runs at build time (CMake), not shipped.
//
// Usage: strings_generator <strings.txt> <name> <output directory>
//
// Input, one string per line (our INI dialect, no sections):
//   # comment
//   key = English text          \n = line break, \" = quote, \\ = backslash
//
// Writes, into the output directory (only when the content changed, so
// nothing rebuilds for nothing):
//   <name>_strings.h     enum class Str : uint32_t { key..., count } and the table function
//   <name>_strings.cpp   the English table (localization.h's LocalizedString)
//   <name>.pot           gettext template: msgctxt = key, msgid = English
//
// Keys: lowercase letters, digits and '_', not starting with a digit, not
// "count" (they become C++ names). Exit code 1 (and a message) on a bad file.

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "host_fs.h"

struct Entry
{
    std::string key;
    std::string text;   // as written: escapes still escaped
    uint32_t    line = 0;
};

static std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    {
        text.remove_suffix(1);
    }
    return text;
}

static bool valid_key(std::string_view key)
{
    if (key.empty() || (key.front() >= '0' && key.front() <= '9') || key == "count")
    {
        return false;
    }
    for (size_t i = 0; i < key.size(); i++)
    {
        char c = key[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

// The text inside "...": the file's escapes kept as they are (C and .po
// read them the same way), plain quotes escaped.
static std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];
        if (c == '\\' && i + 1 < text.size())
        {
            out.push_back(c);
            out.push_back(text[i + 1]);
            i++;
        }
        else if (c == '"')
        {
            out += "\\\"";
        }
        else
        {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

static bool read_entries(const char* path, std::vector<Entry>* entries)
{
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!host_fs_read_file(path, &data, &size))
    {
        return false;
    }
    std::string_view rest(static_cast<const char*>(static_cast<const void*>(data)), size);
    bool ok = true;
    uint32_t line_number = 0;
    while (!rest.empty())
    {
        line_number++;
        size_t line_end = rest.find('\n');
        std::string_view line = trim(rest.substr(0, line_end));
        rest = line_end == std::string_view::npos ? std::string_view() : rest.substr(line_end + 1);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        size_t equal_sign = line.find('=');
        std::string_view key = equal_sign == std::string_view::npos ? line : trim(line.substr(0, equal_sign));
        if (equal_sign == std::string_view::npos || !valid_key(key))
        {
            std::fprintf(stderr, "%s:%u: expected \"key = text\" with a key of a-z, 0-9, _\n", path, line_number);
            ok = false;
            continue;
        }
        for (size_t i = 0; i < entries->size(); i++)
        {
            if ((*entries)[i].key == key)
            {
                std::fprintf(stderr, "%s:%u: key %.*s already used on line %u\n", path, line_number,
                             static_cast<int>(key.size()), key.data(), (*entries)[i].line);
                ok = false;
            }
        }
        Entry entry = {};
        entry.key = std::string(key);
        entry.text = std::string(trim(line.substr(equal_sign + 1)));
        entry.line = line_number;
        entries->push_back(entry);
    }
    delete[] data;
    return ok;
}

// Writes only when different, so the build doesn't redo everything.
static bool write_if_changed(const std::string& path, const std::string& content)
{
    uint8_t* old = nullptr;
    size_t old_size = 0;
    if (host_fs_exists(path.c_str()) && host_fs_read_file(path.c_str(), &old, &old_size))
    {
        bool same = old_size == content.size() && std::memcmp(old, content.data(), old_size) == 0;
        delete[] old;
        if (same)
        {
            return true;
        }
    }
    return host_fs_write_file(path.c_str(), content.data(), content.size());
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <strings.txt> <name> <output directory>\n", argv[0]);
        return 2;
    }
    const char* input = argv[1];
    std::string name = argv[2];
    std::string directory = argv[3];

    std::vector<Entry> entries;
    if (!read_entries(input, &entries) || entries.empty())
    {
        std::fprintf(stderr, "%s: no usable strings\n", input);
        return 1;
    }

    std::string notice = "// GENERATED by strings_generator from " + std::string(input) +
                         ".\n// Edit that file, not this one.\n";

    std::string header = notice;
    header += "\n#pragma once\n\n#include <cstdint>\n\n#include \"localization.h\"\n\n";
    header += "// One ID per string, in the file's order.\nenum class Str : uint32_t\n{\n";
    for (size_t i = 0; i < entries.size(); i++)
    {
        header += "    " + entries[i].key + ",\n";
    }
    header += "    count,\n};\n\n";
    header += "// The English text and key of every Str, indexed by it.\n";
    header += "const LocalizedString* " + name + "_strings_table();\n";

    std::string source = notice;
    source += "\n#include \"" + name + "_strings.h\"\n\nstatic const LocalizedString TABLE[] = {\n";
    for (size_t i = 0; i < entries.size(); i++)
    {
        source += "    {\"" + entries[i].key + "\", " + quoted(entries[i].text) + "},\n";
    }
    source += "};\n\nstatic_assert(sizeof(TABLE) / sizeof(TABLE[0]) == static_cast<uint32_t>(Str::count),\n";
    source += "              \"one table entry per Str\");\n\n";
    source += "const LocalizedString* " + name + "_strings_table()\n{\n    return TABLE;\n}\n";

    std::string pot = "# " + name + " UI text: the template for translations (lang/<language>.po).\n";
    pot += "# GENERATED by strings_generator from strings.txt. msgctxt = the string's key.\n";
    pot += "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n";
    for (size_t i = 0; i < entries.size(); i++)
    {
        pot += "\nmsgctxt \"" + entries[i].key + "\"\nmsgid " + quoted(entries[i].text) + "\nmsgstr \"\"\n";
    }

    bool ok = write_if_changed(directory + "/" + name + "_strings.h", header) &&
              write_if_changed(directory + "/" + name + "_strings.cpp", source) &&
              write_if_changed(directory + "/" + name + ".pot", pot);
    return ok ? 0 : 1;
}
