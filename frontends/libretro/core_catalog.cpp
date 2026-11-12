#include "core_catalog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "host_fs.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool ends_with(const char* text, const char* suffix)
{
    size_t text_length = std::strlen(text);
    size_t suffix_length = std::strlen(suffix);
    return text_length >= suffix_length && std::strcmp(text + text_length - suffix_length, suffix) == 0;
}

static bool is_core_file(const char* name)
{
    return ends_with(name, ".so") || ends_with(name, ".dll") || ends_with(name, ".dylib");
}

static void copy_text(char* destination, size_t size, const char* source)
{
    std::snprintf(destination, size, "%s", source != nullptr ? source : "");
}

bool core_catalog_path(const CoreCatalog* catalog, const CoreCatalogEntry* entry, char* out, size_t out_size)
{
    int written = std::snprintf(out, out_size, "%s/%s", catalog->directory, entry->file_name);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

// Usable cores first, by name (ignoring case), then the rest by file name.
static int compare_entries(const void* a, const void* b)
{
    const CoreCatalogEntry* x = static_cast<const CoreCatalogEntry*>(a);
    const CoreCatalogEntry* y = static_cast<const CoreCatalogEntry*>(b);
    if (x->info.usable != y->info.usable)
    {
        return x->info.usable ? -1 : 1;
    }
    int by_name = x->info.usable ? SDL_strcasecmp(x->info.name, y->info.name) : 0;
    return by_name != 0 ? by_name : std::strcmp(x->file_name, y->file_name);
}

// ---------------------------------------------------------------------------
// Cache file
// ---------------------------------------------------------------------------

static bool is_key(const char* key, const char* wanted)
{
    return std::strcmp(key, wanted) == 0;
}

static bool is_one(const char* value)
{
    return std::strcmp(value, "1") == 0;
}

static void set_field(CoreCatalogEntry* entry, const char* key, const char* value)
{
    LibretroCoreInfo* info = &entry->info;
    if (is_key(key, "size"))
    {
        entry->size = std::strtoull(value, nullptr, 10);
    }
    else if (is_key(key, "mtime"))
    {
        entry->mtime = std::strtoll(value, nullptr, 10);
    }
    else if (is_key(key, "usable"))
    {
        info->usable = is_one(value);
    }
    else if (is_key(key, "error"))
    {
        copy_text(info->error, sizeof(info->error), value);
    }
    else if (is_key(key, "name"))
    {
        copy_text(info->name, sizeof(info->name), value);
    }
    else if (is_key(key, "version"))
    {
        copy_text(info->version, sizeof(info->version), value);
    }
    else if (is_key(key, "extensions"))
    {
        copy_text(info->extensions, sizeof(info->extensions), value);
    }
    else if (is_key(key, "need_fullpath"))
    {
        info->need_fullpath = is_one(value);
    }
    else if (is_key(key, "block_extract"))
    {
        info->block_extract = is_one(value);
    }
    else if (is_key(key, "supports_no_game"))
    {
        info->supports_no_game = is_one(value);
    }
    else if (is_key(key, "option_count"))
    {
        info->option_count = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    }
    else if (is_key(key, "options_api"))
    {
        info->options_api = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    }
    else if (is_key(key, "api_version"))
    {
        info->api_version = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
}

static void load_cache(CoreCatalog* catalog)
{
    catalog->count = 0;
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!host_fs_exists(catalog->cache_path) || !host_fs_read_file(catalog->cache_path, &data, &size))
    {
        return;
    }

    // Line by line, copying each line into a small buffer so it can be split.
    const char* text = static_cast<const char*>(static_cast<const void*>(data));
    CoreCatalogEntry* entry = nullptr;
    size_t start = 0;
    while (start < size)
    {
        size_t end = start;
        while (end < size && text[end] != '\n')
        {
            end++;
        }
        char line[LIBRETRO_EXTENSIONS_MAX + 64] = {};
        size_t length = end - start < sizeof(line) - 1 ? end - start : sizeof(line) - 1;
        std::memcpy(line, text + start, length);
        start = end + 1;

        if (line[0] == '[' && length > 2 && line[length - 1] == ']')
        {
            entry = nullptr;
            if (catalog->count < CORE_CATALOG_MAX)
            {
                entry = &catalog->entries[catalog->count];
                *entry = CoreCatalogEntry{};
                line[length - 1] = '\0';
                copy_text(entry->file_name, sizeof(entry->file_name), line + 1);
                catalog->count++;
            }
            continue;
        }
        char* equals = std::strchr(line, '=');
        if (entry != nullptr && equals != nullptr)
        {
            *equals = '\0';
            set_field(entry, line, equals + 1);
        }
    }
    delete[] data;
}

static void save_cache(const CoreCatalog* catalog)
{
    // Generous: every entry's fields fit in 2 KB plus its extension list.
    size_t capacity = static_cast<size_t>(catalog->count) * (2048 + LIBRETRO_EXTENSIONS_MAX) + 1;
    char* text = new char[capacity];
    size_t used = 0;
    for (int32_t i = 0; i < catalog->count; i++)
    {
        const CoreCatalogEntry* entry = &catalog->entries[i];
        const LibretroCoreInfo* info = &entry->info;
        int written = std::snprintf(text + used, capacity - used,
                                    "[%s]\nsize=%llu\nmtime=%lld\nusable=%d\nerror=%s\nname=%s\nversion=%s\n"
                                    "extensions=%s\nneed_fullpath=%d\nblock_extract=%d\nsupports_no_game=%d\n"
                                    "option_count=%u\noptions_api=%u\napi_version=%u\n\n",
                                    entry->file_name, static_cast<unsigned long long>(entry->size),
                                    static_cast<long long>(entry->mtime), info->usable ? 1 : 0, info->error,
                                    info->name, info->version, info->extensions, info->need_fullpath ? 1 : 0,
                                    info->block_extract ? 1 : 0, info->supports_no_game ? 1 : 0, info->option_count,
                                    info->options_api, info->api_version);
        if (written < 0 || static_cast<size_t>(written) >= capacity - used)
        {
            break;
        }
        used += static_cast<size_t>(written);
    }
    host_fs_write_file(catalog->cache_path, text, used);
    delete[] text;
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

void core_catalog_init(CoreCatalog* catalog, const char* directory, const char* cache_path)
{
    copy_text(catalog->directory, sizeof(catalog->directory), directory);
    copy_text(catalog->cache_path, sizeof(catalog->cache_path), cache_path);
    load_cache(catalog);
}

// The running core's info, from its host (it can't be peeked: same library).
static void info_from_running_host(const LibretroHost* host, LibretroCoreInfo* info)
{
    *info = LibretroCoreInfo{};
    info->usable = true;
    copy_text(info->name, sizeof(info->name), host->system_info.library_name);
    copy_text(info->version, sizeof(info->version), host->system_info.library_version);
    copy_text(info->extensions, sizeof(info->extensions), host->system_info.valid_extensions);
    info->need_fullpath = host->system_info.need_fullpath;
    info->block_extract = host->system_info.block_extract;
    info->supports_no_game = host->supports_no_game;
    info->option_count = host->options.count;
    info->options_api = host->options.api_used;
    info->api_version = RETRO_API_VERSION;
}

void core_catalog_scan(CoreCatalog* catalog, const LibretroHost* running)
{
    catalog->peeked_last_scan = 0;
    int file_count = 0;
    char** names = SDL_GlobDirectory(catalog->directory, "*", 0, &file_count);

    CoreCatalogEntry* found = new CoreCatalogEntry[CORE_CATALOG_MAX];
    int32_t found_count = 0;
    bool changed = false;

    for (int i = 0; names != nullptr && i < file_count && found_count < CORE_CATALOG_MAX; i++)
    {
        CoreCatalogEntry* entry = &found[found_count];
        if (!is_core_file(names[i]) || std::strlen(names[i]) >= sizeof(entry->file_name))
        {
            continue;
        }
        *entry = CoreCatalogEntry{};
        copy_text(entry->file_name, sizeof(entry->file_name), names[i]);

        char path[LIBRETRO_PATH_MAX] = {};
        SDL_PathInfo file = {};
        if (!core_catalog_path(catalog, entry, path, sizeof(path)) || !SDL_GetPathInfo(path, &file))
        {
            continue;
        }
        entry->size = file.size;
        entry->mtime = file.modify_time;
        found_count++;

        // Cached and unchanged: nothing to load.
        bool cached = false;
        for (int32_t j = 0; j < catalog->count; j++)
        {
            const CoreCatalogEntry* old = &catalog->entries[j];
            if (std::strcmp(old->file_name, entry->file_name) == 0 && old->size == entry->size && old->mtime == entry->mtime)
            {
                entry->info = old->info;
                cached = true;
                break;
            }
        }
        if (cached)
        {
            continue;
        }

        changed = true;
        bool is_running = running != nullptr && running->core.library.handle != nullptr &&
                          std::strcmp(running->core_path, path) == 0;
        if (is_running)
        {
            info_from_running_host(running, &entry->info);
        }
        else
        {
            libretro_host_peek_core(path, &entry->info);
            catalog->peeked_last_scan++;
        }
    }
    if (names != nullptr)
    {
        SDL_free(names);
    }

    // Cores removed from the folder also change the cache.
    changed = changed || found_count != catalog->count;

    std::qsort(found, static_cast<size_t>(found_count), sizeof(found[0]), compare_entries);
    for (int32_t i = 0; i < found_count; i++)
    {
        catalog->entries[i] = found[i];
    }
    catalog->count = found_count;
    delete[] found;

    if (changed)
    {
        save_cache(catalog);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Core catalog: %d cores, %d peeked, cache written to %s",
                    catalog->count, catalog->peeked_last_scan, catalog->cache_path);
    }
}

void core_catalog_refresh(CoreCatalog* catalog, const LibretroHost* running)
{
    catalog->count = 0;   // nothing cached: every core gets peeked
    core_catalog_scan(catalog, running);
}
