// libretro_test_frontend: the test frontend bundled with host-libretro.
//
// Its job is checking that host-libretro implements libretro correctly, not
// playing games (that's piccolo). Plain Dear ImGui UI: load a core and content,
// run it, show its video, play its audio (basic: audio_output.h), fixed controls
// (fixed_input.h).
//
// Usage: libretro_test_frontend [core] [content]
// Files can also be dragged onto the window (cores by extension, anything else is content).

#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"

#include "app_paths.h"
#include "app_shell.h"
#include "audio_output.h"
#include "debug_ui.h"
#include "core_catalog.h"
#include "core_session.h"
#include "host_fs.h"
#include "libretro_host.h"
#include "picture.h"
#include "settings_file.h"
#include "vk_texture.h"

static const ClearColor CLEAR_COLOR = { 0.06f, 0.06f, 0.08f, 1.0f };

// Where the core's image was drawn last frame (ImGui coordinates), for the pointer.
struct ImageArea
{
    bool  visible = false;
    float x       = 0.0f;
    float y       = 0.0f;
    float width   = 0.0f;
    float height  = 0.0f;
    bool  hovered = false;
    bool  held    = false;   // left mouse button held down on the image

    // The part of the core's frame shown there (crop), as fractions of it.
    float frame_u0 = 0.0f;
    float frame_v0 = 0.0f;
    float frame_u1 = 1.0f;
    float frame_v1 = 1.0f;
};

// The Video window's picture options (viewport.h / picture.h), to try them
// all here before piccolo gets real settings. Indexes into the lists in
// draw_picture_controls().
struct PictureControls
{
    int   aspect        = 0;   // core
    int   scaling       = 0;   // full
    int   filter        = 0;   // nearest
    int   anchor        = 4;   // center
    int   crop[4]       = {};  // top, bottom, left, right
    float max_offscreen = 5.0f;
};

struct App
{
    AppShell shell = {};
    bool     running = false;

    // The running core: host, video texture, audio, timing, input, states.
    CoreSession session = {};

    char core_path[LIBRETRO_PATH_MAX]    = {};
    char content_path[LIBRETRO_PATH_MAX] = {};
    char status[512]                     = {};

    ImageArea       image_area = {};
    PictureControls picture_controls = {};
    Picture         picture = {};

    // The program folder (app_paths.h: ~/.local/share/host-libretro on Linux)
    // and its subfolders. The host keeps SRAM and savestates in it.
    AppPaths    paths = {};
    std::string imgui_ini;   // <settings>/imgui.ini; ImGui keeps a pointer to it

    // Cores offered in the Core list: every core in <root>/cores, with what
    // each says about itself (peeked once, cached in <root>/core_info_cache.cfg).
    CoreCatalog catalog = {};


    // The core geometry the Video window was last sized for. When the core's
    // base size or aspect changes (load, SET_GEOMETRY, SET_SYSTEM_AV_INFO), the
    // window is resized once; the user may resize it freely in between.
    retro_game_geometry video_window_geometry = {};
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#if defined(__GNUC__)
static void set_status(App* app, const char* format, ...) __attribute__((format(printf, 2, 3)));
#endif
static void set_status(App* app, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(app->status, sizeof(app->status), format, args);
    va_end(args);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", app->status);
}

static void copy_path(char* destination, size_t size, const char* source)
{
    std::snprintf(destination, size, "%s", source);
}

// What the user typed into a path field, made loadable:
//   - surrounding spaces and one pair of matching quotes removed (paths copied
//     from a file manager or a shell often come quoted);
//   - a leading "~" or "~/" replaced by the home folder. Only shells expand "~";
//     dlopen/fopen would look for a folder literally named "~".
// Returns false (and leaves `out` empty) if the result doesn't fit.
static bool resolve_user_path(const char* typed, char* out, size_t out_size)
{
    out[0] = '\0';
    size_t start = 0;
    size_t end = std::strlen(typed);
    while (start < end && (typed[start] == ' ' || typed[start] == '\t'))
    {
        start++;
    }
    while (end > start && (typed[end - 1] == ' ' || typed[end - 1] == '\t' || typed[end - 1] == '\n' || typed[end - 1] == '\r'))
    {
        end--;
    }
    if (end - start >= 2 && (typed[start] == '"' || typed[start] == '\'') && typed[end - 1] == typed[start])
    {
        start++;
        end--;
    }

    const char* home = "";
    if (end > start && typed[start] == '~' && (end - start == 1 || typed[start + 1] == '/'))
    {
        // SDL_GetUserFolder works on every platform (HOME isn't set on Windows)
        // and returns the folder WITH a trailing separator.
        const char* folder = SDL_GetUserFolder(SDL_FOLDER_HOME);
        if (folder != nullptr)
        {
            home = folder;
            start += end - start == 1 ? 1 : 2;   // drop "~" or "~/"
        }
    }

    int written = std::snprintf(out, out_size, "%s%.*s", home, static_cast<int>(end - start), typed + start);
    if (written < 0 || static_cast<size_t>(written) >= out_size)
    {
        out[0] = '\0';
        return false;
    }
    return true;
}

static bool ends_with(const char* text, const char* suffix)
{
    size_t text_length = std::strlen(text);
    size_t suffix_length = std::strlen(suffix);
    return text_length >= suffix_length && std::strcmp(text + text_length - suffix_length, suffix) == 0;
}

static bool looks_like_core(const char* path)
{
    return ends_with(path, ".so") || ends_with(path, ".dll") || ends_with(path, ".dylib");
}

// The file name part of a path, for showing a core in the list.
static const char* file_name_of(const char* path)
{
    const char* name = path;
    for (const char* cursor = path; *cursor != '\0'; cursor++)
    {
        if (*cursor == '/' || *cursor == '\\')
        {
            name = cursor + 1;
        }
    }
    return name;
}

// The catalog entry for the core in the Core field, or nullptr.
static const CoreCatalogEntry* selected_catalog_entry(const App* app)
{
    const char* file = file_name_of(app->core_path);
    for (int32_t i = 0; i < app->catalog.count; i++)
    {
        if (std::strcmp(app->catalog.entries[i].file_name, file) == 0)
        {
            return &app->catalog.entries[i];
        }
    }
    return nullptr;
}

static void catalog_entry_tooltip(const CoreCatalogEntry* entry)
{
    const LibretroCoreInfo* info = &entry->info;
    if (!info->usable)
    {
        ImGui::SetItemTooltip("%s\nNot usable: %s", entry->file_name, info->error);
        return;
    }
    ImGui::SetItemTooltip("%s\nExtensions: %s\nRuns without content: %s\nNeeds a path (need_fullpath): %s\n"
                          "Core options: %u (API v%u)",
                          entry->file_name, info->extensions[0] != '\0' ? info->extensions : "(any)",
                          info->supports_no_game ? "yes" : "no", info->need_fullpath ? "yes" : "no",
                          info->option_count, info->options_api);
}

static void draw_core_list(App* app)
{
    const CoreCatalogEntry* selected = selected_catalog_entry(app);
    char preview[LIBRETRO_INFO_TEXT_MAX * 2 + 8] = {};
    if (selected != nullptr && selected->info.usable)
    {
        std::snprintf(preview, sizeof(preview), "%s %s", selected->info.name, selected->info.version);
    }
    else
    {
        std::snprintf(preview, sizeof(preview), "%s", app->core_path[0] != '\0' ? file_name_of(app->core_path) : "choose a core");
    }

    if (ImGui::BeginCombo("Core", preview))
    {
        // Rescan on every open (cheap: cached cores are only stat'ed), so cores
        // copied into the folder show up. The running core is never peeked.
        if (ImGui::IsWindowAppearing())
        {
            core_catalog_scan(&app->catalog, app->session.host->core.library.handle != nullptr ? app->session.host : nullptr);
        }
        for (int32_t i = 0; i < app->catalog.count; i++)
        {
            const CoreCatalogEntry* entry = &app->catalog.entries[i];
            char label[LIBRETRO_INFO_TEXT_MAX * 2 + 280] = {};
            if (entry->info.usable)
            {
                std::snprintf(label, sizeof(label), "%s %s##%s", entry->info.name, entry->info.version, entry->file_name);
            }
            else
            {
                std::snprintf(label, sizeof(label), "%s (not usable)##%s", entry->file_name, entry->file_name);
            }

            // Greyed out (still selectable) when it can't take what's in the Content field.
            bool fits = libretro_core_info_accepts(&entry->info, app->content_path);
            if (!fits)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            ImGuiSelectableFlags flags = entry->info.usable ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled;
            if (ImGui::Selectable(label, entry == selected, flags))
            {
                if (!core_catalog_path(&app->catalog, entry, app->core_path, sizeof(app->core_path)))
                {
                    app->core_path[0] = '\0';
                    set_status(app, "Core path too long");
                }
            }
            if (!fits)
            {
                ImGui::PopStyleColor();
            }
            catalog_entry_tooltip(entry);
        }
        if (app->catalog.count == 0)
        {
            ImGui::TextDisabled("No cores in %s", app->catalog.directory);
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Cores in %s, greyed out when they can't load what's in Content (or need content when it's "
                          "empty). Drop a core onto the window or pass it on the command line to use one from "
                          "elsewhere.\n%s",
                          app->catalog.directory, app->core_path[0] != '\0' ? app->core_path : "");
    // Full width, on its own line under the list.
    if (ImGui::Button("Refresh core info", ImVec2(-FLT_MIN, 0.0f)))
    {
        core_catalog_refresh(&app->catalog, app->session.host->core.library.handle != nullptr ? app->session.host : nullptr);
        set_status(app, "Core info refreshed: %d cores, %d peeked", app->catalog.count, app->catalog.peeked_last_scan);
    }
    ImGui::SetItemTooltip("Ask every core about itself again (ignores the cached info).");
}

// ---------------------------------------------------------------------------
// Last used core and content (<settings>/last_used.cfg)
// ---------------------------------------------------------------------------
//
// "core" and "content", written when a core starts, read at startup unless a
// core is given on the command line. The fields are filled in; nothing starts
// on its own. Paths are stored as typed (e.g. with "~").

static std::string last_used_file(const App* app)
{
    return app->paths.settings + "/last_used.cfg";
}

static void save_last_used(const App* app)
{
    if (app->paths.settings.empty())
    {
        return;
    }
    SettingsFile file = {};
    if (settings_file_set(&file, "", "core", app->core_path) && settings_file_set(&file, "", "content", app->content_path))
    {
        settings_file_save(&file, last_used_file(app).c_str(), "host-libretro test frontend: last used core and content");
    }
}

static void load_last_used(App* app)
{
    SettingsFile file = {};
    if (app->paths.settings.empty() || !settings_file_load(&file, last_used_file(app).c_str()))
    {
        return;
    }
    const char* core = settings_file_get(&file, "", "core");
    const char* content = settings_file_get(&file, "", "content");
    if (core != nullptr)
    {
        copy_path(app->core_path, sizeof(app->core_path), core);
    }
    if (content != nullptr)
    {
        copy_path(app->content_path, sizeof(app->content_path), content);
    }
}

static bool core_running(const App* app)
{
    return core_session_running(&app->session);
}

// ---------------------------------------------------------------------------
// Core video texture
// ---------------------------------------------------------------------------



static void upload_core_video(App* app)
{
    core_session_upload_video(&app->session, &app->shell);
    if (app_shell_acquire(&app->shell))
    {
        picture_record(&app->picture, renderer_command_buffer(&app->shell.renderer), &app->session.texture);
    }
}

// ---------------------------------------------------------------------------
// Core lifecycle
// ---------------------------------------------------------------------------

static void core_stop(App* app)
{
    core_session_stop(&app->session, &app->shell);
    app->image_area = ImageArea{};
    app->video_window_geometry = retro_game_geometry{};   // the next core sizes the window again
}

static void core_start(App* app)
{
    core_stop(app);

    // The fields keep what the user typed; these are what we actually load.
    char core_path[LIBRETRO_PATH_MAX] = {};
    char content_path[LIBRETRO_PATH_MAX] = {};
    if (!resolve_user_path(app->core_path, core_path, sizeof(core_path)) ||
        !resolve_user_path(app->content_path, content_path, sizeof(content_path)))
    {
        set_status(app, "Path too long");
        return;
    }

    bool started = core_session_start(&app->session, &app->shell, core_path, content_path);
    set_status(app, "%s", app->session.message);
    if (started)
    {
        save_last_used(app);   // so the next start has these filled in
    }
}

// Maps the mouse onto the core's image as a libretro pointer (-0x7fff..0x7fff).
static void read_pointer(const App* app, LibretroInputState* input)
{
    const ImageArea* area = &app->image_area;
    if (!area->visible || !area->hovered || area->width <= 0.0f || area->height <= 0.0f)
    {
        input->pointer_valid = false;
        return;
    }

    ImVec2 mouse = ImGui::GetIO().MousePos;
    float normalized_x = (mouse.x - area->x) / area->width;
    float normalized_y = (mouse.y - area->y) / area->height;
    normalized_x = normalized_x < 0.0f ? 0.0f : (normalized_x > 1.0f ? 1.0f : normalized_x);
    normalized_y = normalized_y < 0.0f ? 0.0f : (normalized_y > 1.0f ? 1.0f : normalized_y);
    // From the shown part to the whole frame (they differ when cropping).
    normalized_x = area->frame_u0 + normalized_x * (area->frame_u1 - area->frame_u0);
    normalized_y = area->frame_v0 + normalized_y * (area->frame_v1 - area->frame_v0);

    input->pointer_valid = true;
    input->pointer_x = static_cast<int16_t>(normalized_x * 65534.0f - 32767.0f);
    input->pointer_y = static_cast<int16_t>(normalized_y * 65534.0f - 32767.0f);
    input->pointer_pressed = area->held;
}

static void run_core_frame(App* app)
{
    LibretroInputState pointer = {};
    read_pointer(app, &pointer);
    // ImGui text fields (core path, content) keep the keyboard from the joypad.
    CoreSessionFrame result = core_session_run_frame(&app->session, &app->shell, &pointer, !ImGui::GetIO().WantTextInput);
    if (result == CoreSessionFrame::shut_down)
    {
        set_status(app, "%s", app->session.message);
        core_stop(app);
    }
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

static const char* pixel_format_name(retro_pixel_format format)
{
    switch (format)
    {
        case RETRO_PIXEL_FORMAT_0RGB1555: return "0RGB1555";
        case RETRO_PIXEL_FORMAT_XRGB8888: return "XRGB8888";
        case RETRO_PIXEL_FORMAT_RGB565:   return "RGB565";
        default:                          return "invalid";
    }
}

static void draw_options_section(App* app);

// ---------------------------------------------------------------------------
// Savestates
// ---------------------------------------------------------------------------

static void save_state(App* app)
{
    if (core_session_save_state(&app->session) || core_running(app))
    {
        set_status(app, "%s", app->session.message);
    }
}

static void load_state(App* app)
{
    if (core_session_load_state(&app->session) || core_running(app))
    {
        set_status(app, "%s", app->session.message);
    }
}

static void change_state_slot(App* app, int32_t delta)
{
    core_session_change_slot(&app->session, delta);
}

static void draw_savestate_section(App* app)
{
    if (!ImGui::CollapsingHeader("Savestates", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    ImGui::BeginDisabled(!core_running(app));

    int slot = static_cast<int>(app->session.state_slot);
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::InputInt("Slot (F6/F7)", &slot))
    {
        change_state_slot(app, static_cast<int32_t>(slot) - static_cast<int32_t>(app->session.state_slot));
    }
    ImGui::SameLine();
    if (ImGui::Button("Save (F2)"))
    {
        save_state(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load (F4)"))
    {
        load_state(app);
    }

    char path[LIBRETRO_PATH_MAX] = {};
    if (core_running(app) && libretro_host_state_path(app->session.host, app->session.state_slot, path, sizeof(path)))
    {
        ImGui::TextDisabled("Slot %u: %s", app->session.state_slot, host_fs_exists(path) ? "has a state" : "empty");
        ImGui::SetItemTooltip("%s", path);
    }
    ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// Once per loop: effective timing mode from the user's choice, the core's fps
// and the display; vsync on/off and the audio speed follow it.
static void update_timing(App* app)
{
    core_session_update(&app->session, &app->shell);
}

static void draw_timing_section(App* app)
{
    static const char* const MODE_NAMES[] = { "Vsync", "Exact" };
    int mode = app->session.timing.mode == TimingMode::vsync ? 0 : 1;
    if (ImGui::Combo("Timing", &mode, MODE_NAMES, 2))
    {
        app->session.timing.mode = mode == 0 ? TimingMode::vsync : TimingMode::exact;
    }
    ImGui::SetItemTooltip("Vsync: one core frame per display refresh, audio speed matched + DRC (falls back to "
                          "Exact when the core's fps is more than 1%% from the display's).\n"
                          "Exact: vsync off, frames paced by a timer at exactly the core's fps.");

    if (!core_running(app))
    {
        return;
    }
    ImGui::Text("In use: %s%s  (core %.3f fps, display %.2f Hz, %s)", frame_timing_mode_name(app->session.timing.effective),
                app->session.timing.fell_back ? " (Vsync not possible)" : "", app->session.timing.core_fps, app->session.timing.display_hz,
                vk_present_mode_to_string(app->shell.swapchain.present_mode));
    if (app->session.timing.fifo_forced)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "Exact not available on this driver: presents wait for vsync (FIFO only)");
    }
    if (app->session.audio.stream != nullptr)
    {
        ImGui::Text("Audio speed: x%.5f%s", app->session.audio.current_ratio,
                    !app->session.audio.rate_control                     ? ""
                    : app->session.timing.effective == TimingMode::vsync ? " (matched to the display + DRC)"
                                                                         : " (DRC)");
    }
}

static const char* const ASPECT_NAMES[] = {"Core provided", "Square pixels", "4:3", "16:9", "8:7", "Stretch"};
static const char* const SCALING_NAMES[] = {"Full", "Integer (under)", "Integer (over)", "Auto"};
static const char* const FILTER_NAMES[] = {"Nearest", "Bilinear", "Sharp bilinear"};
static const char* const ANCHOR_NAMES[] = {"Top left", "Top",         "Top right", "Left",        "Center",
                                           "Right",    "Bottom left", "Bottom",    "Bottom right"};

static void draw_picture_controls(PictureControls* controls)
{
    if (!ImGui::CollapsingHeader("Picture (Video window)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    ImGui::PushItemWidth(160.0f);
    ImGui::Combo("Aspect", &controls->aspect, ASPECT_NAMES, IM_ARRAYSIZE(ASPECT_NAMES));
    ImGui::SameLine();
    ImGui::Combo("Scaling", &controls->scaling, SCALING_NAMES, IM_ARRAYSIZE(SCALING_NAMES));
    ImGui::Combo("Filter", &controls->filter, FILTER_NAMES, IM_ARRAYSIZE(FILTER_NAMES));
    ImGui::SameLine();
    ImGui::Combo("Position", &controls->anchor, ANCHOR_NAMES, IM_ARRAYSIZE(ANCHOR_NAMES));
    ImGui::SliderFloat("Auto: max offscreen %", &controls->max_offscreen, 0.0f, 50.0f, "%.1f");
    ImGui::SliderInt4("Crop T/B/L/R", controls->crop, 0, 64);
    ImGui::PopItemWidth();
}

// The controls as viewport settings.
static void picture_input_from_controls(const PictureControls* controls, ViewportInput* input)
{
    static const ViewportAspect ASPECTS[] = {ViewportAspect::core,  ViewportAspect::square_pixels, ViewportAspect::fixed,
                                             ViewportAspect::fixed, ViewportAspect::fixed,         ViewportAspect::stretch};
    static const double FIXED_ASPECTS[] = {0.0, 0.0, 4.0 / 3.0, 16.0 / 9.0, 8.0 / 7.0, 0.0};
    static const ViewportScaling SCALINGS[] = {ViewportScaling::full, ViewportScaling::integer_under,
                                               ViewportScaling::integer_over, ViewportScaling::automatic};
    static const ViewportFilter FILTERS[] = {ViewportFilter::nearest, ViewportFilter::bilinear,
                                             ViewportFilter::sharp_bilinear};
    static const ViewportAnchor ANCHORS[] = {ViewportAnchor::top_left,    ViewportAnchor::top,
                                             ViewportAnchor::top_right,   ViewportAnchor::left,
                                             ViewportAnchor::center,      ViewportAnchor::right,
                                             ViewportAnchor::bottom_left, ViewportAnchor::bottom,
                                             ViewportAnchor::bottom_right};
    size_t aspect = static_cast<size_t>(controls->aspect);
    input->aspect = ASPECTS[aspect];
    input->fixed_aspect = FIXED_ASPECTS[aspect];
    input->scaling = SCALINGS[static_cast<size_t>(controls->scaling)];
    input->filter = FILTERS[static_cast<size_t>(controls->filter)];
    input->anchor = ANCHORS[static_cast<size_t>(controls->anchor)];
    input->max_offscreen_percent = static_cast<double>(controls->max_offscreen);
    input->crop.top = static_cast<uint32_t>(controls->crop[0]);
    input->crop.bottom = static_cast<uint32_t>(controls->crop[1]);
    input->crop.left = static_cast<uint32_t>(controls->crop[2]);
    input->crop.right = static_cast<uint32_t>(controls->crop[3]);
}

static void draw_controls_window(App* app)
{
    const ImGuiIO& io = ImGui::GetIO();
    LibretroHost* host = app->session.host;

    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 760.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("host-libretro");

    draw_core_list(app);
    ImGui::InputTextWithHint("Content", "empty = start without content", app->content_path, sizeof(app->content_path));

    // Cores read GET_CORE_OPTIONS_VERSION once, while loading, so a change only
    // means something for the next load: unload now rather than show a mismatch.
    static const char* const OPTIONS_API_NAMES[] = {
        "v0 (SET_VARIABLES; version not answered)",
        "v1 (SET_CORE_OPTIONS)",
        "v2 (SET_CORE_OPTIONS_V2)",
    };
    int options_api = static_cast<int>(app->session.options_version);
    if (ImGui::Combo("Options API", &options_api, OPTIONS_API_NAMES, 3))
    {
        uint32_t new_version = static_cast<uint32_t>(options_api);
        if (new_version != app->session.options_version)
        {
            app->session.options_version = new_version;
            if (core_running(app) || app->session.host->core.library.handle != nullptr)
            {
                core_stop(app);
                set_status(app, "Options API changed to v%u: core unloaded, press Start", new_version);
            }
        }
    }
    ImGui::SetItemTooltip("Which core options version we report to the core. Lower versions make it use its "
                          "older options code. Changing it unloads the core.");
    draw_timing_section(app);

    if (ImGui::Button("Start"))
    {
        core_start(app);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!core_running(app));
    if (ImGui::Button("Stop"))
    {
        core_stop(app);
        set_status(app, "Stopped");
    }
    ImGui::SameLine();
    if (ImGui::Button(app->session.paused ? "Resume" : "Pause"))
    {
        app->session.paused = !app->session.paused;
        // Don't leave the last ~100 ms queued: it would play now (after pausing)
        // or, worse, resurface after resuming.
        audio_output_clear(&app->session.audio);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!app->session.paused);
    if (ImGui::Button("Step frame"))
    {
        app->session.step_once = true;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::TextWrapped("%s", app->status);
    ImGui::Separator();

    if (core_running(app))
    {
        const retro_system_info* system = &host->system_info;
        const retro_game_geometry* geometry = &host->av_info.geometry;
        const LibretroHostStats* stats = &host->stats;

        ImGui::Text("Core: %s %s", system->library_name != nullptr ? system->library_name : "?",
                    system->library_version != nullptr ? system->library_version : "");
        ImGui::Text("Pixel format: %s", pixel_format_name(host->pixel_format));
        ImGui::Text("Geometry: base %ux%u, max %ux%u, aspect %.3f", geometry->base_width, geometry->base_height,
                    geometry->max_width, geometry->max_height, static_cast<double>(geometry->aspect_ratio));
        ImGui::Text("Timing: %.3f fps, %.0f Hz audio", host->av_info.timing.fps, host->av_info.timing.sample_rate);
        ImGui::Separator();
        ImGui::Text("Frames run:    %llu", static_cast<unsigned long long>(stats->frames_run));
        ImGui::Text("Video frames:  %llu (%ux%u)", static_cast<unsigned long long>(stats->video_frames),
                    host->video.width, host->video.height);
        ImGui::Text("Duplicates:    %llu", static_cast<unsigned long long>(stats->video_duplicates));
        ImGui::Text("Video errors:  %llu", static_cast<unsigned long long>(stats->video_errors));
        ImGui::Text("Audio frames:  %llu (%zu this frame)", static_cast<unsigned long long>(stats->audio_frames),
                    host->audio.frames);
        if (app->session.audio.stream != nullptr)
        {
            ImGui::Text("Audio out:     %d Hz -> %d Hz, %u ms queued, target %.0f ms%s", app->session.audio.sample_rate,
                        app->session.audio.device_rate, audio_output_queued_ms(&app->session.audio),
                        app->session.audio.rate.target_ms, app->session.audio.rate.automatic ? " (auto)" : "");
            ImGui::Text("Underruns:     %llu   Dropped: %llu frames", static_cast<unsigned long long>(app->session.audio.underruns),
                        static_cast<unsigned long long>(app->session.audio.dropped_frames));
        }
        else
        {
            ImGui::TextUnformatted("Audio out:     none (see the log)");
        }
        if (stats->audio_errors > 0)
        {
            ImGui::Text("Audio errors:  %llu", static_cast<unsigned long long>(stats->audio_errors));
        }
        ImGui::Text("Joypad port 0: 0x%04x", host->input.joypad_buttons[0]);
    }

    ImGui::Separator();
    ImGui::Text("Frontend: %.1f FPS (%s)", static_cast<double>(io.Framerate),
                app->shell.vulkan.physical_device_properties.deviceName);
    ImGui::Text("Gamepad: %s", app->session.gamepad != nullptr ? SDL_GetGamepadName(app->session.gamepad) : "none");
    bool game_focus = app->session.keyboard.game_focus;
    if (ImGui::Checkbox("Game focus (Scroll Lock / F12)", &game_focus))
    {
        keyboard_input_set_game_focus(&app->session.keyboard, game_focus, app->session.host, app->shell.window);
    }
    ImGui::SetItemTooltip("While on, the keyboard is the core's keyboard (typing, computers) and the fixed joypad "
                          "keys are off. The gamepad works either way.");
    if (game_focus)
    {
        ImGui::SameLine();
        ImGui::TextUnformatted(app->session.host->keyboard_callback != nullptr ? "(core has a keyboard callback)"
                                                                       : "(core only polls keys)");
    }
    ImGui::Text("Saves and states: %s", app->paths.root.empty() ? "(none)" : app->paths.root.c_str());

    draw_savestate_section(app);
    draw_picture_controls(&app->picture_controls);
    // Inside this window (not a separate one) so everything scrolls together.
    if (ImGui::CollapsingHeader("Core options", ImGuiTreeNodeFlags_DefaultOpen))
    {
        draw_options_section(app);
    }

    if (ImGui::CollapsingHeader("Controls"))
    {
        ImGui::TextUnformatted("Keyboard: arrows = D-pad, X/Z = A/B, S/A = X/Y,\n"
                               "Q/W = L/R, E/R = L2/R2, Enter = Start,\n"
                               "Right Shift = Select. Mouse on the image = pointer.\n"
                               "Gamepad: by position (South = B, East = A).");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Core options window
// ---------------------------------------------------------------------------

static const char* option_value_text(const LibretroOption* option, uint32_t index)
{
    return option->value_labels[index] != nullptr ? option->value_labels[index] : option->values[index];
}

// One drop-down. Choosing a value hands it to the host; the core picks it up
// on its next GET_VARIABLE_UPDATE (normally during the next frame).
static void draw_option(App* app, const LibretroOption* option, bool inside_category)
{
    const char* label = inside_category && option->label_in_category != nullptr ? option->label_in_category : option->label;

    ImGui::PushID(option->key);
    if (ImGui::BeginCombo(label, option_value_text(option, option->current_index)))
    {
        for (uint32_t i = 0; i < option->value_count; i++)
        {
            bool selected = i == option->current_index;
            if (ImGui::Selectable(option_value_text(option, i), selected))
            {
                libretro_host_set_option(app->session.host, option->key, option->values[i]);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    // Key and info text on hover: the key is what shows up in logs and GET_VARIABLE.
    ImGui::SetItemTooltip("%s%s%s", option->key, option->info != nullptr ? "\n\n" : "",
                          option->info != nullptr ? option->info : "");
    ImGui::PopID();
}

static bool category_exists(const LibretroOptions* options, const char* key)
{
    for (uint32_t i = 0; key != nullptr && i < options->category_count; i++)
    {
        if (std::strcmp(options->categories[i].key, key) == 0)
        {
            return true;
        }
    }
    return false;
}

// Core options, inside the main window ("Core options" section).
static void draw_options_section(App* app)
{
    const LibretroOptions* options = &app->session.host->options;

    if (app->session.host->core.library.handle == nullptr)
    {
        ImGui::TextUnformatted("No core loaded.");
        return;
    }
    if (options->count == 0)
    {
        ImGui::TextUnformatted("The core declared no options.");
        return;
    }

    uint32_t hidden = 0;
    for (uint32_t i = 0; i < options->count; i++)
    {
        hidden += options->options[i].visible ? 0u : 1u;
    }
    ImGui::Text("%u options, declared with API v%u (we report v%u)%s", options->count, options->api_used,
                app->session.host->core_options_version, hidden > 0 ? "," : "");
    if (hidden > 0)
    {
        ImGui::SameLine();
        ImGui::Text("%u hidden by the core", hidden);
    }
    if (ImGui::Button("Reset to defaults"))
    {
        for (uint32_t i = 0; i < options->count; i++)
        {
            const LibretroOption* option = &options->options[i];
            libretro_host_set_option(app->session.host, option->key, option->values[option->default_index]);
        }
    }
    ImGui::Separator();

    // Options without a (known) category first, then one section per category.
    for (uint32_t i = 0; i < options->count; i++)
    {
        const LibretroOption* option = &options->options[i];
        if (option->visible && !category_exists(options, option->category_key))
        {
            draw_option(app, option, false);
        }
    }
    for (uint32_t c = 0; c < options->category_count; c++)
    {
        const LibretroOptionCategory* category = &options->categories[c];
        ImGui::PushID(category->key);
        bool open = ImGui::CollapsingHeader(category->label, ImGuiTreeNodeFlags_DefaultOpen);
        if (category->info != nullptr)
        {
            ImGui::SetItemTooltip("%s", category->info);
        }
        if (open)
        {
            for (uint32_t i = 0; i < options->count; i++)
            {
                const LibretroOption* option = &options->options[i];
                if (option->visible && option->category_key != nullptr && std::strcmp(option->category_key, category->key) == 0)
                {
                    draw_option(app, option, true);
                }
            }
        }
        ImGui::PopID();
    }

}

static bool same_geometry(const retro_game_geometry* a, const retro_game_geometry* b)
{
    return a->base_width == b->base_width && a->base_height == b->base_height && a->aspect_ratio == b->aspect_ratio;
}

// Like RetroArch's windowed mode: the window's image area is the core's base
// size times the largest whole scale that fits on screen. The picture inside is
// still drawn at the core's aspect ratio, so e.g. 720x480 at 4:3 gets side bars.
static void size_video_window_for_geometry(App* app)
{
    const retro_game_geometry* geometry = &app->session.host->av_info.geometry;
    if (!core_running(app) || geometry->base_width == 0 || geometry->base_height == 0 ||
        same_geometry(geometry, &app->video_window_geometry))
    {
        return;
    }
    app->video_window_geometry = *geometry;

    const ImGuiStyle& style = ImGui::GetStyle();
    float chrome_width = style.WindowPadding.x * 2.0f;
    float chrome_height = style.WindowPadding.y * 2.0f + ImGui::GetFrameHeight();   // + title bar

    // Room to the right of / below where the window starts, inside the SDL window.
    ImVec2 screen = ImGui::GetMainViewport()->WorkSize;
    float room_width = screen.x - 500.0f - 20.0f - chrome_width;
    float room_height = screen.y - 20.0f - 20.0f - chrome_height;

    float base_width = static_cast<float>(geometry->base_width);
    float base_height = static_cast<float>(geometry->base_height);
    uint32_t scale = 1;
    while (base_width * static_cast<float>(scale + 1) <= room_width &&
           base_height * static_cast<float>(scale + 1) <= room_height)
    {
        scale++;
    }

    float scale_factor = static_cast<float>(scale);
    ImGui::SetNextWindowSize(ImVec2(base_width * scale_factor + chrome_width, base_height * scale_factor + chrome_height),
                             ImGuiCond_Always);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Video window: %ux%u x%u (core geometry changed)", geometry->base_width,
                geometry->base_height, scale);
}

static void draw_video_window(App* app)
{
    ImGui::SetNextWindowPos(ImVec2(500.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(700.0f, 560.0f), ImGuiCond_FirstUseEver);
    size_video_window_for_geometry(app);   // overrides the size above when the geometry changed
    ImGui::Begin("Video");

    app->image_area.visible = false;
    app->picture.ready = false;
    const StreamingTexture* texture = &app->session.texture;

    if (!core_running(app) || !texture->has_content || app->session.texture_id == 0)
    {
        ImGui::TextUnformatted(core_running(app) ? "Waiting for the first frame..." : "No core running.");
        ImGui::End();
        return;
    }

    // The rest of the window is the "screen" the picture is placed on.
    ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    if (available.x < 1.0f || available.y < 1.0f)
    {
        ImGui::End();
        return;
    }
    ViewportInput input = {};
    input.frame_width = texture->content_width;
    input.frame_height = texture->content_height;
    input.core_aspect = static_cast<double>(app->session.host->av_info.geometry.aspect_ratio);
    input.window_width = static_cast<uint32_t>(available.x);
    input.window_height = static_cast<uint32_t>(available.y);
    picture_input_from_controls(&app->picture_controls, &input);

    ViewportResult viewport = {};
    if (!viewport_compute(&input, &viewport) ||
        !picture_prepare(&app->picture, &app->shell.vulkan, texture, &viewport))
    {
        ImGui::End();
        return;
    }

    // An invisible button over the whole area takes the mouse, so clicking
    // goes to the core as a pointer instead of dragging the ImGui window.
    ImGui::InvisibleButton("core_video_input", available);
    app->image_area.hovered = ImGui::IsItemHovered();
    app->image_area.held = ImGui::IsItemActive();
    app->image_area.visible = true;
    app->image_area.x = origin.x + static_cast<float>(viewport.x);
    app->image_area.y = origin.y + static_cast<float>(viewport.y);
    app->image_area.width = static_cast<float>(viewport.width);
    app->image_area.height = static_cast<float>(viewport.height);
    float frame_width = static_cast<float>(texture->content_width);
    float frame_height = static_cast<float>(texture->content_height);
    app->image_area.frame_u0 = static_cast<float>(viewport.source_x) / frame_width;
    app->image_area.frame_v0 = static_cast<float>(viewport.source_y) / frame_height;
    app->image_area.frame_u1 = static_cast<float>(viewport.source_x + viewport.source_width) / frame_width;
    app->image_area.frame_v1 = static_cast<float>(viewport.source_y + viewport.source_height) / frame_height;

    // Clip to the area: integer over / auto may make the picture bigger.
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, ImVec2(origin.x + available.x, origin.y + available.y), true);
    picture_draw(&app->picture, draw_list, texture, app->session.texture_id, origin.x, origin.y);
    draw_list->PopClipRect();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

static void handle_dropped_file(App* app, const char* path)
{
    if (looks_like_core(path))
    {
        copy_path(app->core_path, sizeof(app->core_path), path);
        set_status(app, "Core set: %s", path);
    }
    else
    {
        copy_path(app->content_path, sizeof(app->content_path), path);
        if (app->core_path[0] != '\0')
        {
            core_start(app);
        }
        else
        {
            set_status(app, "Content set; now choose a core");
        }
    }
}

static void handle_events(App* app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        // In game focus, keys and typed text belong to the core alone: neither
        // ImGui nor the fixed joypad keys see them.
        if (core_session_handle_event(&app->session, &app->shell, &event, !ImGui::GetIO().WantTextInput))
        {
            continue;
        }
        app_shell_process_event(&app->shell, &event);

        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                app->running = false;
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(app->shell.window))
                {
                    app->running = false;
                }
                break;

            case SDL_EVENT_DROP_FILE:
                if (event.drop.data != nullptr)
                {
                    handle_dropped_file(app, event.drop.data);
                }
                break;

            // Frontend hotkeys (as in RetroArch). In game focus they never get
            // here: the keyboard belongs to the core then.
            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat && !ImGui::GetIO().WantTextInput)
                {
                    switch (event.key.key)
                    {
                        case SDLK_F2: save_state(app); break;
                        case SDLK_F4: load_state(app); break;
                        case SDLK_F6: change_state_slot(app, -1); break;
                        case SDLK_F7: change_state_slot(app, 1); break;
                        default: break;
                    }
                }
                break;

            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    // App holds a LibretroHost pointer and path buffers; keep it off the stack.
    App* app = new App{};
    if (!core_session_create(&app->session))
    {
        delete app;
        return 1;
    }

    AppShellConfig config = {};
    config.name          = "libretro_test_frontend";
    config.identifier    = "org.multitap.libretro_test_frontend";
    config.window_width  = 1280;
    config.window_height = 800;

    int exit_code = 0;
    if (!app_shell_create(&app->shell, &config))
    {
        exit_code = 1;
    }
    else
    {
        ImGuiIO& io = ImGui::GetIO();
        // Arrow keys and gamepad buttons belong to the core here, not to ImGui navigation.
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
        // Everything in the program folder (app_paths.h), migrated once from
        // ~/.host-libretro. Without it nothing is saved, but cores still run.
        if (app_paths_init(&app->paths, "host-libretro", "libretro_test_frontend.cfg"))
        {
            app->imgui_ini = app->paths.settings + "/imgui.ini";
            io.IniFilename = app->imgui_ini.c_str();
            copy_path(app->session.saves_directory, sizeof(app->session.saves_directory), app->paths.saves.c_str());
            copy_path(app->session.states_directory, sizeof(app->session.states_directory), app->paths.states.c_str());
            copy_path(app->session.system_directory, sizeof(app->session.system_directory), app->paths.system.c_str());
            std::string cache_path = app->paths.root + "/core_info_cache.cfg";
            core_catalog_init(&app->catalog, app->paths.cores.c_str(), cache_path.c_str());
            core_catalog_scan(&app->catalog, nullptr);
        }
        else
        {
            io.IniFilename = nullptr;
        }

        set_status(app, "Choose a core (list, command line, or drag and drop)");
        if (argc <= 1)
        {
            load_last_used(app);
            if (app->core_path[0] != '\0')
            {
                set_status(app, "Last used core and content filled in: press Start");
            }
        }
        if (argc > 1)
        {
            copy_path(app->core_path, sizeof(app->core_path), argv[1]);
        }
        if (argc > 2)
        {
            copy_path(app->content_path, sizeof(app->content_path), argv[2]);
        }
        if (argc > 1)
        {
            core_start(app);
        }

        app->running = true;
        while (app->running)
        {
            handle_events(app);
            update_timing(app);
            run_core_frame(app);   // only when a frame is due

            if (app_shell_begin_frame(&app->shell))
            {
                // Order matters: the controls window's Start/Stop buttons may recreate or
                // destroy the core texture, so the video window (which puts that texture
                // into this frame's draw list) must be built AFTER it.
                draw_controls_window(app);
                draw_video_window(app);
                upload_core_video(app);
                if (!app_shell_present(&app->shell, CLEAR_COLOR))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal rendering error, exiting");
                    exit_code = 1;
                    break;
                }
            }
            // Exact mode: vsync is off, so this is what keeps the loop at the core's fps.
            core_session_wait(&app->session);
        }

        core_stop(app);
    }

    core_session_destroy(&app->session, &app->shell);
    picture_destroy(&app->picture, &app->shell.vulkan);
    app_shell_destroy(&app->shell);
    delete app;
    return exit_code;
}
