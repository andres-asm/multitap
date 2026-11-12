#pragma once

// The cores in a folder and what each one says about itself.
//
// The information comes from the cores themselves (libretro_host_peek_core,
// in the host), not from .info files, which can be out of date. Peeking means
// loading the library, so the results are cached in a text file and a core
// is only peeked again when its file changes (size or modification time).
// Files that aren't usable cores are cached too, with the reason, so they
// aren't retried on every scan.
//
// Cache file: one block per core file,
//   [snes9x_libretro.so]
//   size=3015784
//   mtime=1762955160000000000
//   name=Snes9x
//   ...

#include <cstdint>

#include "libretro_host.h"

constexpr int32_t CORE_CATALOG_MAX = 128;

struct CoreCatalogEntry
{
    char             file_name[256] = {};
    uint64_t         size           = 0;
    int64_t          mtime          = 0;   // SDL_Time (nanoseconds)
    LibretroCoreInfo info           = {};
};

struct CoreCatalog
{
    char             directory[LIBRETRO_PATH_MAX]  = {};   // the folder with the cores
    char             cache_path[LIBRETRO_PATH_MAX] = {};   // the cache file
    CoreCatalogEntry entries[CORE_CATALOG_MAX]     = {};   // sorted by core name (unusable files last)
    int32_t          count                         = 0;
    int32_t          peeked_last_scan              = 0;    // how many cores the last scan had to load
};

// Remembers the folder and the cache file, and reads the cache (if any).
void core_catalog_init(CoreCatalog* catalog, const char* directory, const char* cache_path);

// Lists the folder; peeks new or changed cores; writes the cache if anything
// changed. `running` (may be nullptr) is a host with a core loaded: that core
// is never peeked (it's the same library), its info is taken from the host.
void core_catalog_scan(CoreCatalog* catalog, const LibretroHost* running);

// Forgets every cached result and scans again, peeking every core (except
// the running one). For when a core changed in a way size + mtime can't see,
// or to rule out a stale cache.
void core_catalog_refresh(CoreCatalog* catalog, const LibretroHost* running);

// "<directory>/<file_name>". False if it doesn't fit.
bool core_catalog_path(const CoreCatalog* catalog, const CoreCatalogEntry* entry, char* out, size_t out_size);
