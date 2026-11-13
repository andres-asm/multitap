#include "host_fs.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include "host_log.h"

// The only file that uses <filesystem> (STYLE.md). Every call takes a
// std::error_code: those overloads report errors there and never throw
// (we build with -fno-exceptions).

// Our paths are UTF-8. A path built from plain char text uses the system's
// narrow encoding, which isn't UTF-8 on Windows; char8_t text is always read
// as UTF-8 (C++20). Same bytes, so the cast through void* is exact.
static std::filesystem::path utf8_path(const char* path)
{
    const char8_t* text = static_cast<const char8_t*>(static_cast<const void*>(path));
    return std::filesystem::path(text);
}

// fopen with a UTF-8 path: Windows needs the UTF-16 one (_wfopen).
static FILE* open_file(const std::filesystem::path* path, bool for_writing)
{
#if defined(_WIN32)
    return _wfopen(path->c_str(), for_writing ? L"wb" : L"rb");
#else
    return std::fopen(path->c_str(), for_writing ? "wb" : "rb");
#endif
}

// Windows also accepts '\\' as a separator.
static bool is_separator(char c)
{
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

bool host_fs_exists(const char* path)
{
    std::error_code error;
    return std::filesystem::exists(utf8_path(path), error);
}

bool host_fs_make_directories(const char* path)
{
    if (path[0] == '\0')
    {
        host_log(LogLevel::error, "Empty directory path");
        return false;
    }
    std::filesystem::path directory = utf8_path(path);
    std::error_code error;
    // Returns false without an error when it already exists.
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        host_log(LogLevel::error, "Could not create directory %s: %s", path, error.message().c_str());
        return false;
    }
    if (!std::filesystem::is_directory(directory, error))
    {
        host_log(LogLevel::error, "Not a directory: %s", path);
        return false;
    }
    return true;
}

bool host_fs_read_file(const char* path, uint8_t** data, size_t* size)
{
    *data = nullptr;
    *size = 0;

    std::filesystem::path file_path = utf8_path(path);
    std::error_code error;
    uintmax_t file_size = std::filesystem::file_size(file_path, error);
    if (error)
    {
        host_log(LogLevel::error, "Could not get the size of %s: %s", path, error.message().c_str());
        return false;
    }
    if (file_size > SIZE_MAX)
    {
        host_log(LogLevel::error, "File too big: %s", path);
        return false;
    }

    FILE* file = open_file(&file_path, false);
    if (file == nullptr)
    {
        host_log(LogLevel::error, "Could not open %s", path);
        return false;
    }

    size_t length = static_cast<size_t>(file_size);
    // new[] of size 0 is valid, but a 1-byte buffer keeps "data != nullptr" true.
    uint8_t* buffer = new uint8_t[length > 0 ? length : 1];
    size_t read = std::fread(buffer, 1, length, file);
    std::fclose(file);

    if (read != length)
    {
        host_log(LogLevel::error, "Short read on %s (%zu of %zu bytes)", path, read, length);
        delete[] buffer;
        return false;
    }

    *data = buffer;
    *size = length;
    return true;
}

bool host_fs_write_file(const char* path, const void* data, size_t size)
{
    std::filesystem::path file_path = utf8_path(path);
    std::error_code error;

    std::filesystem::path directory = file_path.parent_path();
    if (!directory.empty())
    {
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            host_log(LogLevel::error, "Could not create the directory for %s: %s", path, error.message().c_str());
            return false;
        }
    }

    std::filesystem::path temporary = file_path;
    temporary += ".tmp";

    FILE* file = open_file(&temporary, true);
    if (file == nullptr)
    {
        host_log(LogLevel::error, "Could not create %s.tmp", path);
        return false;
    }
    bool ok = size == 0 || std::fwrite(data, 1, size, file) == size;
    ok = std::fflush(file) == 0 && ok;
    ok = std::fclose(file) == 0 && ok;
    if (!ok)
    {
        host_log(LogLevel::error, "Could not write %s.tmp", path);
        std::filesystem::remove(temporary, error);
        return false;
    }

    // Replaces an existing file in one step, on Windows too (MoveFileExW).
    std::filesystem::rename(temporary, file_path, error);
    if (error)
    {
        host_log(LogLevel::error, "Could not replace %s: %s", path, error.message().c_str());
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool host_fs_copy(const char* from, const char* to)
{
    std::filesystem::path source = utf8_path(from);
    std::filesystem::path destination = utf8_path(to);
    std::error_code error;

    std::filesystem::path parent = destination.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error);
        if (error)
        {
            host_log(LogLevel::error, "Could not create the directory for %s: %s", to, error.message().c_str());
            return false;
        }
    }
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing,
                          error);
    if (error)
    {
        host_log(LogLevel::error, "Could not copy %s to %s: %s", from, to, error.message().c_str());
        return false;
    }
    return true;
}

bool host_fs_stem(const char* path, char* out, size_t out_size)
{
    const char* name = path;
    for (const char* cursor = path; *cursor != '\0'; cursor++)
    {
        if (is_separator(*cursor))
        {
            name = cursor + 1;
        }
    }
    const char* dot = std::strrchr(name, '.');
    size_t length = dot != nullptr && dot != name ? static_cast<size_t>(dot - name) : std::strlen(name);
    if (length + 1 > out_size)
    {
        return false;
    }
    std::memcpy(out, name, length);
    out[length] = '\0';
    return true;
}
