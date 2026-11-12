#include "host_fs.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "host_log.h"

constexpr size_t HOST_FS_PATH_MAX = 4096;

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
#if defined(_WIN32)
    return _access(path, 0) == 0;
#else
    struct stat info;
    return stat(path, &info) == 0;
#endif
}

static bool make_one_directory(const char* path)
{
#if defined(_WIN32)
    int result = _mkdir(path);
#else
    int result = mkdir(path, 0755);
#endif
    return result == 0 || errno == EEXIST;
}

bool host_fs_make_directories(const char* path)
{
    char partial[HOST_FS_PATH_MAX] = {};
    size_t length = std::strlen(path);
    if (length == 0 || length >= sizeof(partial))
    {
        host_log(LogLevel::error, "Directory path empty or too long: %s", path);
        return false;
    }
    std::memcpy(partial, path, length + 1);

    // Create each parent in turn by cutting the string at every separator
    // (skipping the leading one of an absolute path).
    for (size_t i = 1; i < length; i++)
    {
        if (!is_separator(partial[i]))
        {
            continue;
        }
        char saved = partial[i];
        partial[i] = '\0';
        if (!make_one_directory(partial))
        {
            host_log(LogLevel::error, "Could not create directory %s: %s", partial, std::strerror(errno));
            return false;
        }
        partial[i] = saved;
    }
    if (!make_one_directory(partial))
    {
        host_log(LogLevel::error, "Could not create directory %s: %s", partial, std::strerror(errno));
        return false;
    }
    return true;
}

bool host_fs_read_file(const char* path, uint8_t** data, size_t* size)
{
    *data = nullptr;
    *size = 0;

    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        host_log(LogLevel::error, "Could not open %s", path);
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        host_log(LogLevel::error, "Could not seek in %s", path);
        std::fclose(file);
        return false;
    }
    long end = std::ftell(file);
    if (end < 0)
    {
        host_log(LogLevel::error, "Could not get the size of %s", path);
        std::fclose(file);
        return false;
    }
    std::rewind(file);

    size_t file_size = static_cast<size_t>(end);
    // new[] of size 0 is valid, but a 1-byte buffer keeps "data != nullptr" true.
    uint8_t* buffer = new uint8_t[file_size > 0 ? file_size : 1];

    size_t read = std::fread(buffer, 1, file_size, file);
    std::fclose(file);

    if (read != file_size)
    {
        host_log(LogLevel::error, "Short read on %s (%zu of %zu bytes)", path, read, file_size);
        delete[] buffer;
        return false;
    }

    *data = buffer;
    *size = file_size;
    return true;
}

bool host_fs_write_file(const char* path, const void* data, size_t size)
{
    // Parent directory first.
    char directory[HOST_FS_PATH_MAX] = {};
    size_t length = std::strlen(path);
    if (length >= sizeof(directory) - 4)   // room for ".tmp"
    {
        host_log(LogLevel::error, "Path too long: %s", path);
        return false;
    }
    std::memcpy(directory, path, length + 1);
    for (size_t i = length; i > 0; i--)
    {
        if (is_separator(directory[i - 1]))
        {
            directory[i - 1] = '\0';
            if (directory[0] != '\0' && !host_fs_make_directories(directory))
            {
                return false;
            }
            break;
        }
    }

    char temporary[HOST_FS_PATH_MAX] = {};
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);

    FILE* file = std::fopen(temporary, "wb");
    if (file == nullptr)
    {
        host_log(LogLevel::error, "Could not create %s: %s", temporary, std::strerror(errno));
        return false;
    }
    bool ok = size == 0 || std::fwrite(data, 1, size, file) == size;
    ok = std::fflush(file) == 0 && ok;
    ok = std::fclose(file) == 0 && ok;
    if (!ok)
    {
        host_log(LogLevel::error, "Could not write %s", temporary);
        std::remove(temporary);
        return false;
    }

#if defined(_WIN32)
    // rename() on Windows refuses to replace an existing file. Removing first
    // leaves a short window without the old file; the .tmp copy is complete by then.
    std::remove(path);
#endif
    if (std::rename(temporary, path) != 0)
    {
        host_log(LogLevel::error, "Could not replace %s: %s", path, std::strerror(errno));
        std::remove(temporary);
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
