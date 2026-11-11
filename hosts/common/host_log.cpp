#include "host_log.h"

#include <cstdio>
#include <cstring>

// The only global in this file. Written once at startup, read afterwards.
static LogLevel g_min_level = LogLevel::info;

void host_log_set_min_level(LogLevel level)
{
    g_min_level = level;
}

static const char* level_name(LogLevel level)
{
    switch (level)
    {
        case LogLevel::debug:   return "debug";
        case LogLevel::info:    return "info";
        case LogLevel::warning: return "warn";
        case LogLevel::error:   return "error";
    }
    return "?";
}

void host_log_va(LogLevel level, const char* prefix, const char* format, va_list args)
{
    if (static_cast<int>(level) < static_cast<int>(g_min_level))
    {
        return;
    }

    // Format into one buffer first so the line is written with a single call
    // and doesn't get interleaved with output from other threads.
    char message[2048] = {};
    vsnprintf(message, sizeof(message), format, args);

    size_t length = std::strlen(message);
    bool has_newline = length > 0 && message[length - 1] == '\n';

    if (prefix != nullptr)
    {
        std::fprintf(stderr, "[%s] %s%s%s", level_name(level), prefix, message, has_newline ? "" : "\n");
    }
    else
    {
        std::fprintf(stderr, "[%s] %s%s", level_name(level), message, has_newline ? "" : "\n");
    }
}

void host_log(LogLevel level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    host_log_va(level, nullptr, format, args);
    va_end(args);
}
