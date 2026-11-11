#pragma once

// Logging for host code. Writes to stderr.
//
// Hosts don't link SDL (they will also run as separate processes later),
// so they can't use SDL_Log. This is the host-side equivalent.

#include <cstdarg>

enum class LogLevel
{
    debug,
    info,
    warning,
    error,
};

// Messages below this level are dropped. Default: LogLevel::info.
void host_log_set_min_level(LogLevel level);

// printf-style. A newline is added if `format` doesn't end with one.
#if defined(__GNUC__)
void host_log(LogLevel level, const char* format, ...) __attribute__((format(printf, 2, 3)));
#else
void host_log(LogLevel level, const char* format, ...);
#endif

// Same as host_log(), for callers that already have a va_list
// (e.g. the libretro log callback). `prefix` may be nullptr.
void host_log_va(LogLevel level, const char* prefix, const char* format, va_list args);
