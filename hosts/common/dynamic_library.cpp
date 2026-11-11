#include "dynamic_library.h"

#include "host_log.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

bool dynamic_library_open(DynamicLibrary* library, const char* path)
{
    // Paths are UTF-8 everywhere in our code; Windows wants UTF-16 for non-ASCII paths.
    int wide_length = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wide_length <= 0)
    {
        host_log(LogLevel::error, "Invalid UTF-8 in library path: %s", path);
        return false;
    }

    wchar_t* wide_path = new wchar_t[static_cast<size_t>(wide_length)];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, wide_length);

    HMODULE module = LoadLibraryW(wide_path);
    delete[] wide_path;

    if (module == nullptr)
    {
        host_log(LogLevel::error, "LoadLibraryW failed for %s (error %lu)", path, GetLastError());
        return false;
    }

    library->handle = reinterpret_cast<void*>(module);
    return true;
}

GenericFunction dynamic_library_symbol(const DynamicLibrary* library, const char* name)
{
    if (library->handle == nullptr)
    {
        return nullptr;
    }
    FARPROC address = GetProcAddress(reinterpret_cast<HMODULE>(library->handle), name);
    return reinterpret_cast<GenericFunction>(address);
}

void dynamic_library_close(DynamicLibrary* library)
{
    if (library->handle != nullptr)
    {
        FreeLibrary(reinterpret_cast<HMODULE>(library->handle));
        library->handle = nullptr;
    }
}

#else

// ---------------------------------------------------------------------------
// POSIX (Linux, macOS, Android)
// ---------------------------------------------------------------------------

bool dynamic_library_open(DynamicLibrary* library, const char* path)
{
    // RTLD_LOCAL: the core's symbols don't leak into the global namespace,
    // which matters once two different cores are loaded in one process.
    // RTLD_NOW: fail here on missing dependencies, not later in the middle of a frame.
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        const char* reason = dlerror();
        host_log(LogLevel::error, "dlopen failed: %s", reason != nullptr ? reason : "unknown error");
        return false;
    }

    library->handle = handle;
    return true;
}

GenericFunction dynamic_library_symbol(const DynamicLibrary* library, const char* name)
{
    if (library->handle == nullptr)
    {
        return nullptr;
    }
    // POSIX guarantees dlsym results can be converted to function pointers.
    void* address = dlsym(library->handle, name);
    return reinterpret_cast<GenericFunction>(address);
}

void dynamic_library_close(DynamicLibrary* library)
{
    if (library->handle != nullptr)
    {
        dlclose(library->handle);
        library->handle = nullptr;
    }
}

#endif
