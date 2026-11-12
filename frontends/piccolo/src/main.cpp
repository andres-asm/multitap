// piccolo: a libretro frontend for playing games, with a retro-style UI
// (retro_ui.h: themed Dear ImGui widgets with piccolo's own pixel font).
//
// The game fills the window. The menu bar (GAME / CONFIG / MISC) is shown
// when nothing runs; while a game runs, Esc shows and hides it, and the game
// pauses while it's shown (or while the LOAD GAME window is open).
//
// Running the core is frontend_libretro's core session, shared with the
// host-libretro test frontend. Cores, saves and states live in
// ~/.host-libretro (cores/, saves/, states/), shared with it too.

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"

#include "app_shell.h"
#include "core_catalog.h"
#include "core_session.h"
#include "file_picker.h"
#include "host_fs.h"
#include "pixel_font.h"
#include "pixel_font_imgui.h"
#include "retro_ui.h"
#include "vk_swapchain.h"

static const ClearColor CLEAR_COLOR = { 0.0f, 0.0f, 0.0f, 1.0f };   // around the game picture

struct App
{
    AppShell shell = {};
    bool     running = false;
    bool     show_debug_window = false;
    bool     show_imgui_demo = false;

    ImFont* pixel_font = nullptr;   // piccolo's pixel font as an ImGui font
    int32_t ui_scale   = 1;         // window pixels per retro pixel
    char    status[256] = "GAME > LOAD GAME TO START";

    CoreSession session = {};
    CoreCatalog catalog = {};
    FilePicker  picker  = {};
    bool        menu_visible = true;   // only hideable while a game runs

    // What the LOAD GAME window picked, started at the top of the NEXT loop:
    // starting stops the old core, which destroys the texture this frame's
    // picture may already have been queued with (a crash at render time).
    FilePickerChoice pending_start = {};
    bool             has_pending_start = false;

    // ~/.host-libretro, and what was used last (piccolo.cfg).
    char data_directory[LIBRETRO_PATH_MAX] = {};
    char last_core[LIBRETRO_PATH_MAX]      = {};
    char last_folder[LIBRETRO_PATH_MAX]    = {};
};

// ---------------------------------------------------------------------------
// Last used core and folder (<data>/piccolo.cfg)
// ---------------------------------------------------------------------------

static bool settings_file(const App* app, char* out, size_t out_size)
{
    int written = std::snprintf(out, out_size, "%s/piccolo.cfg", app->data_directory);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

static void save_settings(const App* app)
{
    char path[LIBRETRO_PATH_MAX] = {};
    char text[LIBRETRO_PATH_MAX * 2 + 32] = {};
    int length = std::snprintf(text, sizeof(text), "core=%s\nfolder=%s\n", app->last_core, app->last_folder);
    if (settings_file(app, path, sizeof(path)) && length > 0 && static_cast<size_t>(length) < sizeof(text))
    {
        host_fs_write_file(path, text, static_cast<size_t>(length));
    }
}

static void read_setting(const char* line, size_t length, const char* key, char* out, size_t out_size)
{
    size_t key_length = std::strlen(key);
    if (length >= key_length && std::strncmp(line, key, key_length) == 0 && length - key_length < out_size)
    {
        std::memcpy(out, line + key_length, length - key_length);
        out[length - key_length] = '\0';
    }
}

static void load_settings(App* app)
{
    char path[LIBRETRO_PATH_MAX] = {};
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!settings_file(app, path, sizeof(path)) || !host_fs_exists(path) || !host_fs_read_file(path, &data, &size))
    {
        return;
    }
    const char* text = static_cast<const char*>(static_cast<const void*>(data));
    size_t start = 0;
    while (start < size)
    {
        size_t end = start;
        while (end < size && text[end] != '\n')
        {
            end++;
        }
        read_setting(text + start, end - start, "core=", app->last_core, sizeof(app->last_core));
        read_setting(text + start, end - start, "folder=", app->last_folder, sizeof(app->last_folder));
        start = end + 1;
    }
    delete[] data;
}

// ---------------------------------------------------------------------------
// Game control
// ---------------------------------------------------------------------------

static bool game_running(const App* app)
{
    return core_session_running(&app->session);
}

static void set_status(App* app, const char* text)
{
    std::snprintf(app->status, sizeof(app->status), "%s", text);
}

// The game pauses while the menu or the LOAD GAME window is up, and its
// queued audio is dropped so resuming doesn't play stale sound.
static void update_pause(App* app)
{
    bool pause = game_running(app) && (app->menu_visible || app->picker.open);
    if (pause && !app->session.paused)
    {
        audio_output_clear(&app->session.audio);
    }
    app->session.paused = pause;
}

static void start_game(App* app, const FilePickerChoice* choice)
{
    if (core_session_start(&app->session, &app->shell, choice->core_path, choice->content_path))
    {
        app->menu_visible = false;   // straight into the game
        std::snprintf(app->last_core, sizeof(app->last_core), "%s", choice->core_path);
        if (choice->content_path[0] != '\0')
        {
            // The folder of the file, for the next LOAD GAME.
            std::snprintf(app->last_folder, sizeof(app->last_folder), "%s", choice->content_path);
            char* slash = std::strrchr(app->last_folder, '/');
            if (slash != nullptr && slash != app->last_folder)
            {
                *slash = '\0';
            }
        }
        save_settings(app);
    }
    set_status(app, app->session.message);
}

static void handle_menu_action(App* app, MenuAction action)
{
    switch (action)
    {
        case MenuAction::none:
            return;
        case MenuAction::load_game:
            core_catalog_scan(&app->catalog, game_running(app) ? app->session.host : nullptr);
            file_picker_open(&app->picker, &app->catalog, app->last_folder, app->last_core);
            break;
        case MenuAction::reset:
            if (game_running(app))
            {
                libretro_host_reset(app->session.host);
                set_status(app, "RESET");
                app->menu_visible = false;
            }
            break;
        case MenuAction::save_state:
            if (core_session_save_state(&app->session))
            {
                app->menu_visible = false;
            }
            set_status(app, app->session.message);
            break;
        case MenuAction::load_state:
            if (core_session_load_state(&app->session))
            {
                app->menu_visible = false;
            }
            set_status(app, app->session.message);
            break;
        case MenuAction::quit:
            app->running = false;
            break;
        case MenuAction::debug_window:
            app->show_debug_window = !app->show_debug_window;
            break;
        default:
            set_status(app, "NOT THERE YET");
            break;
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// Keyboard as the game's joypad only while the game has the screen.
static bool game_has_keyboard(const App* app)
{
    return game_running(app) && !app->menu_visible && !app->picker.open;
}

static void app_handle_events(App* app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (core_session_handle_event(&app->session, &app->shell, &event, game_has_keyboard(app)))
        {
            continue;   // the core's keyboard (game focus)
        }
        app_shell_process_event(&app->shell, &event);

        if (event.type == SDL_EVENT_QUIT)
        {
            app->running = false;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(app->shell.window))
        {
            app->running = false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
                case SDLK_ESCAPE:
                    // Close the LOAD GAME window first; otherwise show/hide the
                    // menu (it stays up when nothing runs).
                    if (app->picker.open)
                    {
                        app->picker.open = false;
                    }
                    else if (game_running(app))
                    {
                        app->menu_visible = !app->menu_visible;
                    }
                    break;
                case SDLK_F2:
                    handle_menu_action(app, MenuAction::save_state);
                    break;
                case SDLK_F4:
                    handle_menu_action(app, MenuAction::load_state);
                    break;
                default:
                    break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void draw_game(App* app)
{
    const CoreSession* session = &app->session;
    if (!game_running(app) || !session->texture.has_content || session->texture_id == 0)
    {
        return;
    }
    float u = 0.0f;
    float v = 0.0f;
    streaming_texture_uv_max(&session->texture, &u, &v);
    float aspect = session->host->av_info.geometry.aspect_ratio;
    if (aspect <= 0.0f && session->texture.content_height > 0)
    {
        aspect = static_cast<float>(session->texture.content_width) / static_cast<float>(session->texture.content_height);
    }
    retro_ui_game_picture(session->texture_id, u, v, aspect > 0.0f ? aspect : 4.0f / 3.0f);
}

static void app_draw_debug_ui(App* app)
{
    const ImGuiIO& io = ImGui::GetIO();
    const VkPhysicalDeviceProperties* gpu = &app->shell.vulkan.physical_device_properties;
    const VulkanSwapchain* swapchain = &app->shell.swapchain;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 80.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("piccolo", &app->show_debug_window);

    ImGui::Text("GPU: %s", gpu->deviceName);
    ImGui::Text("Vulkan %u.%u.%u", VK_API_VERSION_MAJOR(gpu->apiVersion), VK_API_VERSION_MINOR(gpu->apiVersion),
                VK_API_VERSION_PATCH(gpu->apiVersion));
    ImGui::Text("Swapchain: %ux%u, %u images, %s", swapchain->extent.width, swapchain->extent.height,
                swapchain->image_count, vk_present_mode_to_string(swapchain->present_mode));
    ImGui::Text("Validation: %s", app->shell.vulkan.validation_enabled ? "on" : "off");
    ImGui::Text("Retro UI scale: %dx", app->ui_scale);
    ImGui::Separator();
    ImGui::Text("%.3f ms/frame (%.1f FPS)", static_cast<double>(1000.0f / io.Framerate), static_cast<double>(io.Framerate));
    if (game_running(app))
    {
        const CoreSession* session = &app->session;
        ImGui::Text("Core: %s, %.3f fps, timing %s", session->host->system_info.library_name,
                    session->host->av_info.timing.fps, frame_timing_mode_name(session->timing.effective));
        ImGui::Text("Audio: x%.5f, %u ms queued, %llu underruns", session->audio.current_ratio,
                    audio_output_queued_ms(&session->audio), static_cast<unsigned long long>(session->audio.underruns));
    }
    ImGui::Separator();
    ImGui::Checkbox("Dear ImGui demo window", &app->show_imgui_demo);

    ImGui::End();

    if (app->show_imgui_demo)
    {
        ImGui::ShowDemoWindow(&app->show_imgui_demo);
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

static bool make_data_paths(App* app, char* cores, size_t cores_size, char* cache, size_t cache_size)
{
    const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);   // ends with a separator
    int data = std::snprintf(app->data_directory, sizeof(app->data_directory), "%s.host-libretro", home != nullptr ? home : "");
    int cores_written = std::snprintf(cores, cores_size, "%s/cores", app->data_directory);
    int cache_written = std::snprintf(cache, cache_size, "%s/core_info_cache.cfg", app->data_directory);
    return home != nullptr && data > 0 && static_cast<size_t>(data) < sizeof(app->data_directory) && cores_written > 0 &&
           static_cast<size_t>(cores_written) < cores_size && cache_written > 0 && static_cast<size_t>(cache_written) < cache_size;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    // The session, catalog and file picker are large (paths, file lists): off the stack.
    App* app = new App{};

    AppShellConfig config = {};
    config.name       = "piccolo";
    config.identifier = "org.multitap.piccolo";

    if (!pixel_font_init() || !core_session_create(&app->session))
    {
        delete app;
        return 1;
    }
    if (!app_shell_create(&app->shell, &config))
    {
        app_shell_destroy(&app->shell);
        core_session_destroy(&app->session, &app->shell);
        delete app;
        return 1;
    }
    app->pixel_font = pixel_font_imgui_add();
    if (app->pixel_font == nullptr)
    {
        core_session_destroy(&app->session, &app->shell);
        app_shell_destroy(&app->shell);
        delete app;
        return 1;
    }

    char cores_directory[LIBRETRO_PATH_MAX] = {};
    char cache_path[LIBRETRO_PATH_MAX] = {};
    if (make_data_paths(app, cores_directory, sizeof(cores_directory), cache_path, sizeof(cache_path)))
    {
        SDL_CreateDirectory(cores_directory);
        std::snprintf(app->session.data_directory, sizeof(app->session.data_directory), "%s", app->data_directory);
        core_catalog_init(&app->catalog, cores_directory, cache_path);
        core_catalog_scan(&app->catalog, nullptr);
        load_settings(app);
    }

    app->running = true;
    while (app->running)
    {
        app_handle_events(app);
        if (app->has_pending_start)
        {
            app->has_pending_start = false;
            start_game(app, &app->pending_start);   // outside any frame: nothing drawn with the old texture yet
        }
        update_pause(app);
        core_session_update(&app->session, &app->shell);
        if (core_session_run_frame(&app->session, &app->shell, nullptr, game_has_keyboard(app)) == CoreSessionFrame::shut_down)
        {
            set_status(app, app->session.message);
            app->menu_visible = true;
        }

        if (app_shell_begin_frame(&app->shell))
        {
            app->ui_scale = retro_ui_scale_for_width(ImGui::GetIO().DisplaySize.x);

            draw_game(app);   // background, behind everything
            bool menu_shown = app->menu_visible || !game_running(app);
            if (menu_shown)
            {
                // The status line's window goes first, so it stays behind the others.
                retro_ui_status_line(app->pixel_font, app->ui_scale, app->status);
                handle_menu_action(app, retro_ui_menu_bar(app->pixel_font, app->ui_scale));
            }

            if (file_picker_draw(&app->picker, &app->catalog, app->pixel_font, app->ui_scale, &app->pending_start))
            {
                app->has_pending_start = true;   // see pending_start
            }
            if (app->show_debug_window)
            {
                app_draw_debug_ui(app);
            }

            core_session_upload_video(&app->session, &app->shell);
            if (!app_shell_present(&app->shell, CLEAR_COLOR))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal rendering error, exiting");
                break;
            }
        }
        core_session_wait(&app->session);
    }

    core_session_destroy(&app->session, &app->shell);   // writes save RAM
    app_shell_destroy(&app->shell);
    delete app;
    return 0;
}
