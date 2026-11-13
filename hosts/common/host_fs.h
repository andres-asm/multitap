#pragma once

// Small file helpers for hosts (no SDL). Paths are UTF-8.
// Every failure is logged (host_log) with the path, so callers just return.

#include <cstddef>
#include <cstdint>

// True if `path` exists (file or directory).
bool host_fs_exists(const char* path);

// Creates `path` and any missing parent directories ("mkdir -p").
// Succeeds if the directory already exists.
bool host_fs_make_directories(const char* path);

// Reads a whole file into a new[] buffer the caller owns (delete[]).
// An empty file succeeds with *size == 0 (and a 1-byte buffer).
bool host_fs_read_file(const char* path, uint8_t** data, size_t* size);

// Writes `size` bytes so that `path` is never left half-written: the data
// goes to "<path>.tmp" first, which then replaces `path`. Creates missing
// parent directories.
bool host_fs_write_file(const char* path, const void* data, size_t size);

// Copies a file, or a directory with everything inside it, to `to`.
// Files that already exist at the destination are kept, not overwritten.
// Creates missing parent directories.
bool host_fs_copy(const char* from, const char* to);

// "dir/name.ext" -> "name" (no directory, no last extension). Returns false
// if it doesn't fit in `out`.
bool host_fs_stem(const char* path, char* out, size_t out_size);
