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
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"

#include "app_shell.h"
#include "audio_output.h"
#include "debug_ui.h"
#include "fixed_input.h"
#include "keyboard_input.h"
#include "libretro_host.h"
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
};

struct App
{
    AppShell shell = {};
    bool     running = false;

    LibretroHost* host = nullptr;
    char core_path[LIBRETRO_PATH_MAX]    = {};
    char content_path[LIBRETRO_PATH_MAX] = {};
    char status[512]                     = {};
    bool paused    = false;
    bool step_once = false;

    StreamingTexture core_texture    = {};
    uint64_t         core_texture_id = 0;   // ImGui texture id, 0 = none
    uint64_t         uploaded_serial = 0;   // host->video.serial of the frame in the texture

    ImageArea    image_area = {};
    SDL_Gamepad* gamepad = nullptr;
    FixedInput   fixed_input = {};
    KeyboardInput keyboard   = {};   // libretro keyboard + game focus (Scroll Lock / F12)

    // Where the host keeps SRAM and savestates ("~/.host-libretro", resolved at startup).
    char data_directory[LIBRETRO_PATH_MAX] = {};

    AudioOutput audio = {};   // open while a core runs; stream == nullptr = no sound

    // Core options version we report to the next core we load (2, 1 or 0).
    // Lower values make cores take their older options code paths.
    uint32_t options_version = LIBRETRO_CORE_OPTIONS_VERSION_MAX;

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

static bool core_running(const App* app)
{
    return app->host->game_loaded;
}

// ---------------------------------------------------------------------------
// Core video texture
// ---------------------------------------------------------------------------

static void destroy_core_texture(App* app)
{
    vkDeviceWaitIdle(app->shell.vulkan.device);
    debug_ui_remove_texture(app->core_texture_id);
    app->core_texture_id = 0;
    streaming_texture_destroy(&app->core_texture, &app->shell.vulkan);
    app->uploaded_serial = 0;
}

// Makes sure the texture can hold width x height. Recreating it waits for the
// GPU, so it's sized to the core's maximum geometry up front.
static bool ensure_core_texture(App* app, uint32_t width, uint32_t height)
{
    if (app->core_texture.image != VK_NULL_HANDLE &&
        width <= app->core_texture.capacity_width && height <= app->core_texture.capacity_height)
    {
        return true;
    }

    uint32_t new_width = width > app->core_texture.capacity_width ? width : app->core_texture.capacity_width;
    uint32_t new_height = height > app->core_texture.capacity_height ? height : app->core_texture.capacity_height;

    destroy_core_texture(app);
    if (!streaming_texture_create(&app->core_texture, &app->shell.vulkan, new_width, new_height))
    {
        set_status(app, "Could not create a %ux%u texture for the core's video", new_width, new_height);
        return false;
    }
    app->core_texture_id = debug_ui_add_texture(app->core_texture.view);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Core video texture: %ux%u", new_width, new_height);
    return true;
}

static void upload_core_video(App* app)
{
    const LibretroVideoFrame* video = &app->host->video;
    if (!core_running(app) || video->pixels == nullptr || video->serial == app->uploaded_serial)
    {
        return;
    }
    if (!app_shell_acquire(&app->shell))
    {
        return;   // no frame this time (e.g. swapchain being recreated); try next frame
    }
    if (streaming_texture_upload(&app->core_texture, renderer_command_buffer(&app->shell.renderer),
                                 renderer_frame_index(&app->shell.renderer), video->pixels, video->width, video->height))
    {
        app->uploaded_serial = video->serial;
    }
}

// ---------------------------------------------------------------------------
// Core lifecycle
// ---------------------------------------------------------------------------

static void core_stop(App* app)
{
    if (app->core_texture.image != VK_NULL_HANDLE)
    {
        destroy_core_texture(app);
    }
    audio_output_destroy(&app->audio);
    libretro_host_unload_core(app->host);
    app->paused = false;
    app->image_area = ImageArea{};
    app->video_window_geometry = retro_game_geometry{};   // the next core sizes the window again
}

static void core_start(App* app)
{
    core_stop(app);

    if (app->core_path[0] == '\0')
    {
        set_status(app, "Choose a core first");
        return;
    }

    // The fields keep what the user typed; these are what we actually load.
    char core_path[LIBRETRO_PATH_MAX] = {};
    char content_path[LIBRETRO_PATH_MAX] = {};
    if (!resolve_user_path(app->core_path, core_path, sizeof(core_path)) ||
        !resolve_user_path(app->content_path, content_path, sizeof(content_path)))
    {
        set_status(app, "Path too long");
        return;
    }

    LibretroHostConfig config = {};
    config.core_options_version = app->options_version;
    config.data_directory = app->data_directory;
    if (!libretro_host_load_core(app->host, core_path, &config))
    {
        set_status(app, "Could not load the core %s (see the log)", core_path);
        return;
    }

    const char* content = content_path[0] != '\0' ? content_path : nullptr;
    if (!libretro_host_load_game(app->host, content))
    {
        set_status(app, "The core could not start%s (see the log)", content != nullptr ? " this content" : " without content");
        libretro_host_unload_core(app->host);
        return;
    }

    const retro_game_geometry* geometry = &app->host->av_info.geometry;
    uint32_t width = geometry->max_width > 0 ? geometry->max_width : geometry->base_width;
    uint32_t height = geometry->max_height > 0 ? geometry->max_height : geometry->base_height;
    if (width == 0 || height == 0)
    {
        width = 640;
        height = 480;
    }
    if (!ensure_core_texture(app, width, height))
    {
        core_stop(app);
        return;
    }

    // No sound is not fatal for a test frontend: the core still runs (the reason is logged).
    audio_output_create(&app->audio, app->host->av_info.timing.sample_rate);

    set_status(app, "Running %s", app->host->system_info.library_name != nullptr ? app->host->system_info.library_name : "core");
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

    input->pointer_valid = true;
    input->pointer_x = static_cast<int16_t>(normalized_x * 65534.0f - 32767.0f);
    input->pointer_y = static_cast<int16_t>(normalized_y * 65534.0f - 32767.0f);
    input->pointer_pressed = area->held;
}

static void run_core_frame(App* app)
{
    if (!core_running(app) || (app->paused && !app->step_once))
    {
        return;
    }
    // Minimized: nothing is presented, so there's no vsync to pace the core.
    // Running anyway would spin it at ~100 fps (app_shell sleeps 10 ms per loop).
    if (SDL_GetWindowFlags(app->shell.window) & SDL_WINDOW_MINIMIZED)
    {
        // Nothing gets pushed while minimized, so the queue runs dry on purpose:
        // clear it so restoring the window doesn't count as an underrun.
        audio_output_clear(&app->audio);
        return;
    }
    app->step_once = false;

    LibretroInputState input = {};
    // In game focus the keyboard is the core's keyboard, not the fixed joypad keys.
    bool keyboard_enabled = !ImGui::GetIO().WantTextInput && !app->keyboard.game_focus;
    fixed_input_read(&app->fixed_input, &input, keyboard_enabled, app->gamepad);
    keyboard_input_read(&app->keyboard, &input);
    read_pointer(app, &input);

    libretro_host_set_input(app->host, &input);
    libretro_host_run_frame(app->host);

    if (app->host->shutdown_requested)
    {
        set_status(app, "The core asked to shut down (RETRO_ENVIRONMENT_SHUTDOWN)");
        core_stop(app);
        return;
    }

    // host->audio holds exactly this frame's audio. A core may have changed its
    // sample rate this frame (SET_SYSTEM_AV_INFO), so follow it first.
    // Single-stepping (paused) stays silent: one frame of audio is a ~16 ms blip
    // after a gap, which would only count as an underrun every step.
    audio_output_set_sample_rate(&app->audio, app->host->av_info.timing.sample_rate);
    if (!app->paused)
    {
        audio_output_push(&app->audio, app->host->audio.samples, app->host->audio.frames);
    }

    // A core may grow its image at runtime (SET_SYSTEM_AV_INFO); recreate the texture before drawing.
    const LibretroVideoFrame* video = &app->host->video;
    if (video->width > 0 && video->height > 0)
    {
        ensure_core_texture(app, video->width, video->height);
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

static void draw_controls_window(App* app)
{
    const ImGuiIO& io = ImGui::GetIO();
    LibretroHost* host = app->host;

    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("host-libretro");

    ImGui::InputText("Core", app->core_path, sizeof(app->core_path));
    ImGui::InputTextWithHint("Content", "empty = start without content", app->content_path, sizeof(app->content_path));

    // Cores read GET_CORE_OPTIONS_VERSION once, while loading, so a change only
    // means something for the next load: unload now rather than show a mismatch.
    static const char* const OPTIONS_API_NAMES[] = {
        "v0 (SET_VARIABLES; version not answered)",
        "v1 (SET_CORE_OPTIONS)",
        "v2 (SET_CORE_OPTIONS_V2)",
    };
    int options_api = static_cast<int>(app->options_version);
    if (ImGui::Combo("Options API", &options_api, OPTIONS_API_NAMES, 3))
    {
        uint32_t new_version = static_cast<uint32_t>(options_api);
        if (new_version != app->options_version)
        {
            app->options_version = new_version;
            if (core_running(app) || app->host->core.library.handle != nullptr)
            {
                core_stop(app);
                set_status(app, "Options API changed to v%u: core unloaded, press Start", new_version);
            }
        }
    }
    ImGui::SetItemTooltip("Which core options version we report to the core. Lower versions make it use its "
                          "older options code. Changing it unloads the core.");

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
    if (ImGui::Button(app->paused ? "Resume" : "Pause"))
    {
        app->paused = !app->paused;
        // Don't leave the last ~100 ms queued: it would play now (after pausing)
        // or, worse, resurface after resuming.
        audio_output_clear(&app->audio);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!app->paused);
    if (ImGui::Button("Step frame"))
    {
        app->step_once = true;
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
        if (app->audio.stream != nullptr)
        {
            ImGui::Text("Audio out:     %d Hz, %u ms queued", app->audio.sample_rate, audio_output_queued_ms(&app->audio));
            ImGui::Text("Underruns:     %llu   Dropped: %llu frames", static_cast<unsigned long long>(app->audio.underruns),
                        static_cast<unsigned long long>(app->audio.dropped_frames));
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
    ImGui::Text("Gamepad: %s", app->gamepad != nullptr ? SDL_GetGamepadName(app->gamepad) : "none");
    bool game_focus = app->keyboard.game_focus;
    if (ImGui::Checkbox("Game focus (Scroll Lock / F12)", &game_focus))
    {
        keyboard_input_set_game_focus(&app->keyboard, game_focus, app->host, app->shell.window);
    }
    ImGui::SetItemTooltip("While on, the keyboard is the core's keyboard (typing, computers) and the fixed joypad "
                          "keys are off. The gamepad works either way.");
    if (game_focus)
    {
        ImGui::SameLine();
        ImGui::TextUnformatted(app->host->keyboard_callback != nullptr ? "(core has a keyboard callback)"
                                                                       : "(core only polls keys)");
    }
    ImGui::Text("Saves and states: %s", app->data_directory[0] != '\0' ? app->data_directory : "(none)");

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
                libretro_host_set_option(app->host, option->key, option->values[i]);
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

static void draw_options_window(App* app)
{
    const LibretroOptions* options = &app->host->options;

    ImGui::SetNextWindowPos(ImVec2(20.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Core options");

    if (app->host->core.library.handle == nullptr)
    {
        ImGui::TextUnformatted("No core loaded.");
        ImGui::End();
        return;
    }
    if (options->count == 0)
    {
        ImGui::TextUnformatted("The core declared no options.");
        ImGui::End();
        return;
    }

    uint32_t hidden = 0;
    for (uint32_t i = 0; i < options->count; i++)
    {
        hidden += options->options[i].visible ? 0u : 1u;
    }
    ImGui::Text("%u options, declared with API v%u (we report v%u)%s", options->count, options->api_used,
                app->host->core_options_version, hidden > 0 ? "," : "");
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
            libretro_host_set_option(app->host, option->key, option->values[option->default_index]);
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

    ImGui::End();
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
    const retro_game_geometry* geometry = &app->host->av_info.geometry;
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
    const StreamingTexture* texture = &app->core_texture;

    if (!core_running(app) || !texture->has_content || app->core_texture_id == 0)
    {
        ImGui::TextUnformatted(core_running(app) ? "Waiting for the first frame..." : "No core running.");
        ImGui::End();
        return;
    }

    // Fit the image into the window, keeping the core's aspect ratio.
    float content_width = static_cast<float>(texture->content_width);
    float content_height = static_cast<float>(texture->content_height);
    float aspect = app->host->av_info.geometry.aspect_ratio;
    if (aspect <= 0.0f)
    {
        aspect = content_width / content_height;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    float width = available.x;
    float height = width / aspect;
    if (height > available.y)
    {
        height = available.y;
        width = height * aspect;
    }
    if (width < 1.0f || height < 1.0f)
    {
        ImGui::End();
        return;
    }

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    cursor.x += (available.x - width) * 0.5f;
    ImGui::SetCursorScreenPos(cursor);

    // An invisible button over the image takes the mouse, so clicking the image
    // goes to the core as a pointer instead of dragging the ImGui window.
    ImGui::InvisibleButton("core_video_input", ImVec2(width, height));
    app->image_area.hovered = ImGui::IsItemHovered();
    app->image_area.held = ImGui::IsItemActive();
    app->image_area.visible = true;
    app->image_area.x = cursor.x;
    app->image_area.y = cursor.y;
    app->image_area.width = width;
    app->image_area.height = height;

    ImGui::SetCursorScreenPos(cursor);
    float u = 0.0f;
    float v = 0.0f;
    streaming_texture_uv_max(texture, &u, &v);
    debug_ui_image_nearest(app->core_texture_id, width, height, u, v);

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
        if (keyboard_input_handle_event(&app->keyboard, &event, app->host, app->shell.window))
        {
            continue;
        }
        app_shell_process_event(&app->shell, &event);
        fixed_input_handle_event(&app->fixed_input, &event, !ImGui::GetIO().WantTextInput && !app->keyboard.game_focus,
                                 app->gamepad);

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

            case SDL_EVENT_GAMEPAD_ADDED:
                if (app->gamepad == nullptr)
                {
                    app->gamepad = SDL_OpenGamepad(event.gdevice.which);
                }
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                if (app->gamepad != nullptr && SDL_GetGamepadID(app->gamepad) == event.gdevice.which)
                {
                    SDL_CloseGamepad(app->gamepad);
                    app->gamepad = nullptr;
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
    app->host = new LibretroHost{};

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
        // Own layout file, so it doesn't fight with piccolo's imgui.ini in the same folder.
        io.IniFilename = "libretro_test_frontend.ini";

        // Decided for now: per-game host data (SRAM, savestates) under ~/.host-libretro.
        resolve_user_path("~/.host-libretro", app->data_directory, sizeof(app->data_directory));

        set_status(app, "Choose a core (path, command line, or drag and drop)");
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
            keyboard_input_update(&app->keyboard, app->shell.window);
            run_core_frame(app);

            if (app_shell_begin_frame(&app->shell))
            {
                // Order matters: the controls window's Start/Stop buttons may recreate or
                // destroy the core texture, so the video window (which puts that texture
                // into this frame's draw list) must be built AFTER it.
                draw_controls_window(app);
                draw_options_window(app);
                draw_video_window(app);
                upload_core_video(app);
                if (!app_shell_present(&app->shell, CLEAR_COLOR))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal rendering error, exiting");
                    exit_code = 1;
                    break;
                }
            }
        }

        core_stop(app);
        if (app->gamepad != nullptr)
        {
            SDL_CloseGamepad(app->gamepad);
        }
    }

    app_shell_destroy(&app->shell);
    delete app->host;
    delete app;
    return exit_code;
}
