#include "app_paths.h"

#include <cstring>

#include <SDL3/SDL.h>

#include "host_fs.h"
#include "settings_file.h"

static bool is_separator(char c)
{
    return c == '/' || c == '\\';
}

// base + "/" + name, without doubling a separator `base` already ends with.
static std::string join(const char* base, const char* name)
{
    std::string result = base;
    while (result.size() > 1 && is_separator(result.back()))
    {
        result.pop_back();
    }
    result += "/";
    result += name;
    return result;
}

bool app_paths_xdg_root(const char* xdg_data_home, const char* home, const char* program_name, std::string* root)
{
    // The XDG spec: a relative path in XDG_DATA_HOME is invalid and ignored.
    if (xdg_data_home != nullptr && xdg_data_home[0] == '/')
    {
        *root = join(xdg_data_home, program_name);
        return true;
    }
    if (home != nullptr && home[0] != '\0')
    {
        std::string data_home = join(home, ".local/share");
        *root = join(data_home.c_str(), program_name);
        return true;
    }
    return false;
}

void app_paths_fill(AppPaths* paths)
{
    const char* root = paths->root.c_str();
    paths->settings = join(root, "settings");
    paths->cores = join(root, "cores");
    paths->saves = join(root, "saves");
    paths->states = join(root, "states");
    paths->system = join(root, "system");
}

// `value` with a leading `prefix` (followed by a separator or the end)
// replaced by `replacement`. False if it doesn't start with it.
static bool replace_prefix(std::string* value, const char* prefix, const char* replacement)
{
    size_t length = std::strlen(prefix);
    if (length == 0 || value->compare(0, length, prefix) != 0)
    {
        return false;
    }
    if (value->size() > length && !is_separator((*value)[length]))
    {
        return false;   // "/home/u/.host-libretro-old/..." is not inside the old folder
    }
    value->replace(0, length, replacement);
    return true;
}

// Copies the old "last used" file (key=value lines) to the new one, pointing
// paths that were inside the old folder at the new folder.
static bool migrate_last_used(const char* from, const char* to, const char* old_root, const char* new_root)
{
    SettingsFile file = {};
    if (!settings_file_load(&file, from))
    {
        return false;
    }
    int32_t global = settings_file_find_section(&file, "");
    if (global >= 0)
    {
        SettingsFileSection* section = &file.sections[static_cast<size_t>(global)];
        for (size_t i = 0; i < section->entries.size(); i++)
        {
            std::string* value = &section->entries[i].value;
            if (!replace_prefix(value, old_root, new_root))
            {
                replace_prefix(value, "~/.host-libretro", new_root);
            }
        }
    }
    return settings_file_save(&file, to, nullptr);
}

bool app_paths_setup(const AppPaths* paths, const char* old_root, const char* old_last_used_name)
{
    bool ok = true;
    const char* root = paths->root.c_str();

    if (old_root != nullptr && old_root[0] != '\0' && host_fs_exists(old_root) &&
        !host_fs_exists(paths->settings.c_str()))
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "First run with %s: copying cores, saves and states from %s (the old folder is left as it is)",
                    root, old_root);
        static const char* const FOLDERS[] = {"cores", "saves", "states"};
        const std::string* destinations[] = {&paths->cores, &paths->saves, &paths->states};
        for (size_t i = 0; i < sizeof(FOLDERS) / sizeof(FOLDERS[0]); i++)
        {
            std::string from = join(old_root, FOLDERS[i]);
            if (host_fs_exists(from.c_str()) && !host_fs_copy(from.c_str(), destinations[i]->c_str()))
            {
                ok = false;
            }
        }
        if (old_last_used_name != nullptr)
        {
            std::string from = join(old_root, old_last_used_name);
            std::string to = join(paths->settings.c_str(), "last_used.cfg");
            if (host_fs_exists(from.c_str()) && !migrate_last_used(from.c_str(), to.c_str(), old_root, root))
            {
                ok = false;
            }
        }
        if (!ok)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Copying from %s was not complete (see above)", old_root);
        }
    }

    const std::string* folders[] = {&paths->root,  &paths->settings, &paths->cores,
                                    &paths->saves, &paths->states,   &paths->system};
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++)
    {
        if (!host_fs_make_directories(folders[i]->c_str()))
        {
            ok = false;
        }
    }
    return ok;
}

bool app_paths_init(AppPaths* paths, const char* program_name, const char* old_last_used_name)
{
    *paths = AppPaths{};
    const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);   // ends with a separator; SDL owns it

#if defined(_WIN32)
    // Portable: the folder the executable is in.
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Can't find the program's folder: %s", SDL_GetError());
        return false;
    }
    paths->root = base;
    while (paths->root.size() > 3 && is_separator(paths->root.back()))
    {
        paths->root.pop_back();
    }
#elif defined(__APPLE__)
    if (home == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Can't find the home folder: %s", SDL_GetError());
        return false;
    }
    std::string support = join(home, "Library/Application Support");
    paths->root = join(support.c_str(), program_name);
#else
    if (!app_paths_xdg_root(SDL_getenv("XDG_DATA_HOME"), SDL_getenv("HOME"), program_name, &paths->root))
    {
        // No HOME (some sandboxes, Android): the platform's own writable folder.
        char* preferences = SDL_GetPrefPath("multitap", program_name);
        if (preferences == nullptr)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Can't find a folder for %s: %s", program_name, SDL_GetError());
            return false;
        }
        paths->root = preferences;
        SDL_free(preferences);
        while (paths->root.size() > 1 && is_separator(paths->root.back()))
        {
            paths->root.pop_back();
        }
    }
#endif

    app_paths_fill(paths);
    std::string old_root = home != nullptr ? join(home, ".host-libretro") : std::string();
    bool ok = app_paths_setup(paths, old_root.c_str(), old_last_used_name);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s folder: %s", program_name, paths->root.c_str());
    return ok;
}
