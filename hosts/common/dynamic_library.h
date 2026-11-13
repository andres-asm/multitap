#pragma once

// Loading shared libraries (.so / .dll / .dylib) and looking up functions.
// Thin wrapper over dlopen/dlsym (POSIX) and LoadLibraryW/GetProcAddress (Windows).

// A function pointer of "unknown" type. Callers cast it to the real signature
// with reinterpret_cast (allowed by STYLE.md for loader function pointers).
// Casting a function pointer to another function pointer type and back is
// well-defined, which is why we use this instead of void*.
typedef void (*GenericFunction)(void);

struct DynamicLibrary
{
    void* handle = nullptr;   // dlopen handle or HMODULE
};

// path: UTF-8. Logs the OS error on failure.
bool dynamic_library_open(DynamicLibrary* library, const char* path);

// Returns nullptr if the symbol isn't exported (does not log; callers decide).
GenericFunction dynamic_library_symbol(const DynamicLibrary* library, const char* name);

// Safe on a library that was never opened.
void dynamic_library_close(DynamicLibrary* library);

// True if the library at `path` (UTF-8) is already loaded in this process,
// however the path is spelled (symlink, "dir//lib.so"...): the OS identifies
// it by the file, not the string. Never loads it and leaves its reference
// count as it was.
bool dynamic_library_is_loaded(const char* path);
