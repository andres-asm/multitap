#include "file_picker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "imgui.h"

#include "retro_ui.h"

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

static bool join_path(const char* directory, const char* name, char* out, size_t out_size)
{
    size_t length = std::strlen(directory);
    bool has_separator = length > 0 && (directory[length - 1] == '/' || directory[length - 1] == '\\');
    int written = std::snprintf(out, out_size, "%s%s%s", directory, has_separator ? "" : "/", name);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

static bool is_separator(char c)
{
    return c == '/' || c == '\\';
}

// "/a/b/c" -> "/a/b", "/a" -> "/". Stays "/" at the root.
static void go_to_parent(char* directory)
{
    size_t length = std::strlen(directory);
    // Ignore trailing separators ("/a/b/" is "/a/b").
    while (length > 1 && is_separator(directory[length - 1]))
    {
        length--;
    }
    // Find the separator before the last component.
    size_t last = length;
    while (last > 0 && !is_separator(directory[last - 1]))
    {
        last--;
    }
    if (last <= 1)
    {
        std::snprintf(directory, 2, "/");   // the parent is the root
        return;
    }
    directory[last - 1] = '\0';
}

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

static int compare_names(const void* a, const void* b)
{
    return SDL_strcasecmp(static_cast<const char*>(a), static_cast<const char*>(b));
}

static const LibretroCoreInfo* selected_core(const FilePicker* picker, const CoreCatalog* catalog)
{
    if (picker->core_index < 0 || picker->core_index >= catalog->count)
    {
        return nullptr;
    }
    return &catalog->entries[picker->core_index].info;
}

static void list_folder(FilePicker* picker, const CoreCatalog* catalog)
{
    picker->folder_count = 0;
    picker->file_count = 0;
    picker->selected_file = -1;
    picker->listing_stale = false;

    const LibretroCoreInfo* core = selected_core(picker, catalog);
    int count = 0;
    char** names = SDL_GlobDirectory(picker->directory, "*", 0, &count);
    for (int i = 0; names != nullptr && i < count; i++)
    {
        const char* name = names[i];
        if (name[0] == '.' || std::strlen(name) >= FILE_PICKER_NAME_MAX)
        {
            continue;   // hidden files and folders, and names we can't hold
        }
        char path[LIBRETRO_PATH_MAX] = {};
        SDL_PathInfo info = {};
        if (!join_path(picker->directory, name, path, sizeof(path)) || !SDL_GetPathInfo(path, &info))
        {
            continue;
        }
        if (info.type == SDL_PATHTYPE_DIRECTORY && picker->folder_count < FILE_PICKER_MAX_ENTRIES)
        {
            std::snprintf(picker->folders[picker->folder_count], FILE_PICKER_NAME_MAX, "%s", name);
            picker->folder_count++;
        }
        else if (info.type == SDL_PATHTYPE_FILE && picker->file_count < FILE_PICKER_MAX_ENTRIES)
        {
            // Only what the chosen core says it loads (its own extension list).
            bool wanted = picker->show_all || core == nullptr || libretro_core_info_accepts(core, name);
            if (wanted)
            {
                std::snprintf(picker->files[picker->file_count], FILE_PICKER_NAME_MAX, "%s", name);
                picker->file_count++;
            }
        }
    }
    if (names != nullptr)
    {
        SDL_free(names);
    }
    std::qsort(picker->folders, static_cast<size_t>(picker->folder_count), FILE_PICKER_NAME_MAX, compare_names);
    std::qsort(picker->files, static_cast<size_t>(picker->file_count), FILE_PICKER_NAME_MAX, compare_names);
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

void file_picker_open(FilePicker* picker, const CoreCatalog* catalog, const char* directory, const char* core_path)
{
    picker->open = true;
    picker->listing_stale = true;

    SDL_PathInfo info = {};
    if (directory != nullptr && directory[0] != '\0' && SDL_GetPathInfo(directory, &info) &&
        info.type == SDL_PATHTYPE_DIRECTORY)
    {
        std::snprintf(picker->directory, sizeof(picker->directory), "%s", directory);
    }
    else
    {
        const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
        std::snprintf(picker->directory, sizeof(picker->directory), "%s", home != nullptr ? home : "/");
    }

    // The given core if it's in the catalog, else the first usable one that
    // needs content (the catalog lists usable cores first).
    picker->core_index = -1;
    for (int32_t i = 0; i < catalog->count; i++)
    {
        char path[LIBRETRO_PATH_MAX] = {};
        if (core_path != nullptr && core_catalog_path(catalog, &catalog->entries[i], path, sizeof(path)) &&
            std::strcmp(path, core_path) == 0 && catalog->entries[i].info.usable)
        {
            picker->core_index = i;
            return;
        }
    }
    for (int32_t i = 0; i < catalog->count && picker->core_index < 0; i++)
    {
        if (catalog->entries[i].info.usable)
        {
            picker->core_index = i;
        }
    }
}

// Fills `choice` with the selected core and `file_name` (nullptr = no content).
static bool choose(FilePicker* picker, const CoreCatalog* catalog, const char* file_name, FilePickerChoice* choice)
{
    *choice = FilePickerChoice{};
    if (picker->core_index < 0 ||
        !core_catalog_path(catalog, &catalog->entries[picker->core_index], choice->core_path, sizeof(choice->core_path)))
    {
        return false;
    }
    if (file_name != nullptr && !join_path(picker->directory, file_name, choice->content_path, sizeof(choice->content_path)))
    {
        return false;
    }
    picker->open = false;
    return true;
}

bool file_picker_draw(FilePicker* picker, const CoreCatalog* catalog, ImFont* pixel_font, int32_t scale,
                      FilePickerChoice* choice)
{
    if (!picker->open)
    {
        return false;
    }
    if (picker->listing_stale)
    {
        list_folder(picker, catalog);
    }

    bool picked = false;
    float block = static_cast<float>(scale);
    const ImGuiIO& io = ImGui::GetIO();

    retro_ui_push_theme(pixel_font, scale);
    // 400 x 250 retro pixels, but never bigger than the window.
    ImVec2 size(400.0f * block, 250.0f * block);
    size.x = size.x < io.DisplaySize.x - 8.0f * block ? size.x : io.DisplaySize.x - 8.0f * block;
    size.y = size.y < io.DisplaySize.y - 24.0f * block ? size.y : io.DisplaySize.y - 24.0f * block;
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool keep_open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("LOAD GAME", &keep_open, flags))
    {
        retro_ui_begin_window(scale);

        // Core first: it decides which files are shown.
        const LibretroCoreInfo* core = selected_core(picker, catalog);
        char preview[LIBRETRO_INFO_TEXT_MAX * 2 + 8] = {};
        std::snprintf(preview, sizeof(preview), "%s %s", core != nullptr ? core->name : "no cores found",
                      core != nullptr ? core->version : "");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("CORE");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##core", preview))
        {
            for (int32_t i = 0; i < catalog->count; i++)
            {
                const CoreCatalogEntry* entry = &catalog->entries[i];
                if (!entry->info.usable)
                {
                    continue;
                }
                char label[LIBRETRO_INFO_TEXT_MAX * 2 + 280] = {};
                std::snprintf(label, sizeof(label), "%s %s##%s", entry->info.name, entry->info.version, entry->file_name);
                if (ImGui::Selectable(label, i == picker->core_index))
                {
                    picker->core_index = i;
                    picker->listing_stale = true;
                }
            }
            ImGui::EndCombo();
        }

        // Files (left) and folders (right), leaving room for two rows below.
        float row = ImGui::GetFrameHeightWithSpacing();
        float list_height = ImGui::GetContentRegionAvail().y - row * 2.0f;
        float files_width = ImGui::GetContentRegionAvail().x * 0.62f;

        ImGui::BeginChild("files", ImVec2(files_width, list_height), ImGuiChildFlags_Borders);
        for (int32_t i = 0; i < picker->file_count; i++)
        {
            ImGui::PushID(i);
            if (ImGui::Selectable(picker->files[i], i == picker->selected_file, ImGuiSelectableFlags_AllowDoubleClick))
            {
                picker->selected_file = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    picked = choose(picker, catalog, picker->files[i], choice);
                }
            }
            ImGui::PopID();
        }
        if (picker->file_count == 0)
        {
            ImGui::TextDisabled("NOTHING THIS CORE LOADS HERE");
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("folders", ImVec2(0.0f, list_height), ImGuiChildFlags_Borders);
        const char* open_folder = nullptr;
        if (ImGui::Selectable(".."))
        {
            go_to_parent(picker->directory);
            picker->listing_stale = true;
        }
        for (int32_t i = 0; i < picker->folder_count; i++)
        {
            ImGui::PushID(i);
            if (ImGui::Selectable(picker->folders[i]))
            {
                open_folder = picker->folders[i];
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (open_folder != nullptr)
        {
            char next[LIBRETRO_PATH_MAX] = {};
            if (join_path(picker->directory, open_folder, next, sizeof(next)))
            {
                std::snprintf(picker->directory, sizeof(picker->directory), "%s", next);
                picker->listing_stale = true;
            }
        }

        // Where we are.
        ImGui::TextDisabled("%s", picker->directory);

        // Bottom row.
        if (ImGui::Checkbox("SHOW ALL FILES", &picker->show_all))
        {
            picker->listing_stale = true;
        }
        bool can_run_alone = core != nullptr && core->supports_no_game;
        float load_width = ImGui::CalcTextSize("LOAD").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float alone_width = ImGui::CalcTextSize("START WITHOUT GAME").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float right = load_width + (can_run_alone ? alone_width + ImGui::GetStyle().ItemSpacing.x : 0.0f);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - right);
        if (can_run_alone && ImGui::Button("START WITHOUT GAME"))
        {
            picked = choose(picker, catalog, nullptr, choice);
        }
        if (can_run_alone)
        {
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(picker->selected_file < 0);
        if (ImGui::Button("LOAD") && picker->selected_file >= 0)
        {
            picked = choose(picker, catalog, picker->files[picker->selected_file], choice);
        }
        ImGui::EndDisabled();

        retro_ui_end_window();
    }
    ImGui::End();
    retro_ui_pop_theme();

    if (!keep_open)
    {
        picker->open = false;
    }
    return picked;
}
