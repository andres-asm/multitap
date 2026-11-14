#include "core_session.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#include <SDL3/SDL.h>

#include "app_shell.h"
#include "debug_ui.h"
#include "host_fs.h"

#if defined(__GNUC__)
static void set_message(CoreSession* session, const char* format, ...) __attribute__((format(printf, 2, 3)));
#endif
static void set_message(CoreSession* session, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(session->message, sizeof(session->message), format, args);
    va_end(args);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", session->message);
}

// ---------------------------------------------------------------------------
// Video texture
// ---------------------------------------------------------------------------

static void destroy_texture(CoreSession* session, AppShell* shell)
{
    if (session->texture.image == VK_NULL_HANDLE)
    {
        return;
    }
    vkDeviceWaitIdle(shell->vulkan.device);   // the GPU may still be drawing it
    debug_ui_remove_texture(session->texture_id);
    session->texture_id = 0;
    streaming_texture_destroy(&session->texture, &shell->vulkan);
    session->uploaded_serial = 0;
}

// Makes sure the texture can hold width x height. Recreating it waits for the
// GPU, so it's sized to the core's maximum geometry up front.
static bool ensure_texture(CoreSession* session, AppShell* shell, uint32_t width, uint32_t height)
{
    StreamingTexture* texture = &session->texture;
    if (texture->image != VK_NULL_HANDLE && width <= texture->capacity_width && height <= texture->capacity_height)
    {
        return true;
    }
    uint32_t new_width = width > texture->capacity_width ? width : texture->capacity_width;
    uint32_t new_height = height > texture->capacity_height ? height : texture->capacity_height;

    destroy_texture(session, shell);
    if (!streaming_texture_create(texture, &shell->vulkan, new_width, new_height))
    {
        set_message(session, "Could not create a %ux%u texture for the core's video", new_width, new_height);
        return false;
    }
    session->texture_id = debug_ui_add_texture(texture->view);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Core video texture: %ux%u", new_width, new_height);
    return true;
}

void core_session_upload_video(CoreSession* session, AppShell* shell)
{
    const LibretroVideoFrame* video = &session->host->video;
    if (!core_session_running(session) || video->pixels == nullptr || video->serial == session->uploaded_serial)
    {
        return;
    }
    if (!app_shell_acquire(shell))
    {
        return;   // no frame this time (e.g. swapchain being recreated); try next frame
    }
    if (streaming_texture_upload(&session->texture, renderer_command_buffer(&shell->renderer),
                                 renderer_frame_index(&shell->renderer), video->pixels, video->width, video->height))
    {
        session->uploaded_serial = video->serial;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool core_session_create(CoreSession* session)
{
    *session = CoreSession{};
    // ~20 KB of path buffers: on the heap.
    session->host = new (std::nothrow) LibretroHost{};
    return session->host != nullptr;
}

void core_session_destroy(CoreSession* session, AppShell* shell)
{
    if (session->host != nullptr)
    {
        core_session_stop(session, shell);
        delete session->host;
    }
    if (session->gamepad != nullptr)
    {
        SDL_CloseGamepad(session->gamepad);
    }
    *session = CoreSession{};
}

bool core_session_running(const CoreSession* session)
{
    return session->host != nullptr && session->host->game_loaded;
}

void core_session_stop(CoreSession* session, AppShell* shell)
{
    if (core_session_running(session))
    {
        // How the session's pacing went: missed refreshes, catch-up frames,
        // audio underruns (what a log is read for after a stuttery run).
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Session: %llu missed refreshes, %llu frames caught up, %llu audio underruns",
                    static_cast<unsigned long long>(session->timing.missed_total),
                    static_cast<unsigned long long>(session->timing.caught_up_total),
                    static_cast<unsigned long long>(session->audio.underruns));
    }
    session->timing.missed_total = 0;
    session->timing.caught_up_total = 0;
    destroy_texture(session, shell);
    audio_output_destroy(&session->audio);
    libretro_host_unload_core(session->host);   // writes save RAM first
    session->paused = false;
    session->step_once = false;
}

// The content folder's name ("Super Nintendo" for ".../Super Nintendo/smw.sfc"),
// the core options' folder layer.
static void folder_name(const char* content_path, char* out, size_t out_size)
{
    out[0] = '\0';
    if (content_path == nullptr)
    {
        return;
    }
    const char* end = std::strrchr(content_path, '/');
    if (end == nullptr || end == content_path)
    {
        return;
    }
    const char* start = end - 1;
    while (start > content_path && *(start - 1) != '/')
    {
        start--;
    }
    size_t length = static_cast<size_t>(end - start);
    if (length >= out_size)
    {
        length = out_size - 1;
    }
    std::memcpy(out, start, length);
    out[length] = '\0';
}

// The running core declared (or re-declared) its options: cache them.
static void cache_options(CoreSession* session)
{
    if (session->option_cache == nullptr || session->host->options.serial == session->options_serial)
    {
        return;
    }
    session->options_serial = session->host->options.serial;
    char core[256] = {};
    if (libretro_core_short_name(session->host->core_path, core, sizeof(core)))
    {
        core_options_cache_update(session->option_cache, core, &session->host->options);
    }
}

bool core_session_set_option(CoreSession* session, CoreOptionLayer layer, const char* key, const char* value)
{
    const LibretroOption* option = libretro_options_find(&session->host->options, key);
    if (session->option_store == nullptr || option == nullptr)
    {
        return false;
    }
    if (!core_options_store_set(session->option_store, layer, key, value, option->values[option->default_index]))
    {
        return false;
    }
    const char* effective = core_options_store_get(session->option_store, key, nullptr);
    return libretro_host_set_option(session->host, key, effective != nullptr ? effective : value);
}

bool core_session_reset_option(CoreSession* session, CoreOptionLayer layer, const char* key)
{
    const LibretroOption* option = libretro_options_find(&session->host->options, key);
    if (session->option_store == nullptr || option == nullptr)
    {
        return false;
    }
    core_options_store_reset(session->option_store, layer, key);
    const char* effective = core_options_store_get(session->option_store, key, nullptr);
    return libretro_host_set_option(session->host, key,
                                    effective != nullptr ? effective : option->values[option->default_index]);
}

bool core_session_start(CoreSession* session, AppShell* shell, const char* core_path, const char* content_path)
{
    core_session_stop(session, shell);
    if (core_path == nullptr || core_path[0] == '\0')
    {
        set_message(session, "Choose a core first");
        return false;
    }

    // The stored option values for this core, folder and game: the host
    // applies them as the core declares its options.
    std::vector<LibretroOptionValue> saved;
    if (session->option_store != nullptr)
    {
        char core[256] = {};
        char folder[LIBRETRO_PATH_MAX] = {};
        char game[LIBRETRO_PATH_MAX] = {};
        libretro_core_short_name(core_path, core, sizeof(core));
        if (content_path != nullptr && content_path[0] != '\0')
        {
            folder_name(content_path, folder, sizeof(folder));
            host_fs_stem(content_path, game, sizeof(game));
        }
        core_options_store_set_context(session->option_store, core, folder, game);
        core_options_store_effective(session->option_store, &saved);
    }

    LibretroHostConfig config = {};
    config.core_options_version = session->options_version;
    config.language = session->language;
    config.saves_directory = session->saves_directory;
    config.states_directory = session->states_directory;
    config.system_directory = session->system_directory;
    config.saved_options = saved.data();
    config.saved_option_count = static_cast<uint32_t>(saved.size());
    if (!libretro_host_load_core(session->host, core_path, &config))
    {
        set_message(session, "Could not load the core %s (see the log)", core_path);
        return false;
    }
    if (!saved.empty())
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Core options: %u stored value(s) for this core / folder / game",
                    static_cast<unsigned>(saved.size()));
    }
    session->options_serial = 0;
    cache_options(session);   // declared in retro_set_environment

    const char* content = content_path != nullptr && content_path[0] != '\0' ? content_path : nullptr;
    if (!libretro_host_load_game(session->host, content))
    {
        set_message(session, "The core could not start%s (see the log)", content != nullptr ? " this content" : " without content");
        libretro_host_unload_core(session->host);
        return false;
    }

    // Texture at the core's maximum size, so it rarely needs recreating.
    const retro_game_geometry* geometry = &session->host->av_info.geometry;
    uint32_t width = geometry->max_width > 0 ? geometry->max_width : geometry->base_width;
    uint32_t height = geometry->max_height > 0 ? geometry->max_height : geometry->base_height;
    if (width == 0 || height == 0)
    {
        width = 640;
        height = 480;
    }
    if (!ensure_texture(session, shell, width, height))
    {
        libretro_host_unload_core(session->host);
        return false;
    }

    // No sound is not fatal: the core still runs (the reason is logged).
    audio_output_create(&session->audio, session->host->av_info.timing.sample_rate, &session->audio_config);

    const char* name = session->host->system_info.library_name;
    set_message(session, "Running %s", name != nullptr ? name : "core");
    return true;
}

// ---------------------------------------------------------------------------
// Per loop
// ---------------------------------------------------------------------------

bool core_session_handle_event(CoreSession* session, AppShell* shell, const SDL_Event* event, bool keyboard_for_joypad)
{
    // In game focus, keys and typed text belong to the core alone.
    if (keyboard_input_handle_event(&session->keyboard, event, session->host, shell->window))
    {
        return true;
    }
    fixed_input_handle_event(&session->fixed_input, event, keyboard_for_joypad && !session->keyboard.game_focus,
                             session->gamepad);

    if ((event->type == SDL_EVENT_WINDOW_FOCUS_LOST || event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) &&
        event->window.windowID == SDL_GetWindowID(shell->window))
    {
        audio_output_set_background(&session->audio, event->type == SDL_EVENT_WINDOW_FOCUS_LOST);
    }
    if (event->type == SDL_EVENT_AUDIO_DEVICE_ADDED || event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED)
    {
        if (!event->adevice.recording)
        {
            audio_output_devices_changed(&session->audio);
        }
    }
    if (event->type == SDL_EVENT_GAMEPAD_ADDED && session->gamepad == nullptr)
    {
        session->gamepad = SDL_OpenGamepad(event->gdevice.which);
    }
    if (event->type == SDL_EVENT_GAMEPAD_REMOVED && session->gamepad != nullptr &&
        SDL_GetGamepadID(session->gamepad) == event->gdevice.which)
    {
        SDL_CloseGamepad(session->gamepad);
        session->gamepad = nullptr;
    }
    return false;
}

void core_session_update(CoreSession* session, AppShell* shell)
{
    keyboard_input_update(&session->keyboard, shell->window);

    bool running = core_session_running(session);
    // vsync_forced comes from the swapchain as it is now (set_vsync below may
    // change it; the next loop sees the result).
    frame_timing_update(&session->timing, running, running ? session->host->av_info.timing.fps : 0.0,
                        app_shell_display_refresh_hz(shell), app_shell_vsync_forced(shell));
    // Missed refreshes (and the catch-up frames for them) only count while
    // core frames run: a paused or minimized game has nothing to make up.
    bool minimized = (SDL_GetWindowFlags(shell->window) & SDL_WINDOW_MINIMIZED) != 0;
    frame_timing_measure(&session->timing, running && !session->paused && !minimized);
    bool vsync = session->timing.effective == TimingMode::vsync;
    if (!app_shell_set_vsync(shell, vsync))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not switch vsync %s", vsync ? "on" : "off");
    }
    // DRC in both modes. Vsync: on top of display Hz / core fps. Exact: on top
    // of ratio 1, because the frame timer (SDL_GetTicksNS) and the audio device
    // clock differ by tens of ppm, enough to drain or fill the queue in minutes.
    // DRC can be turned off in the frontend's settings (then only the base speed applies).
    audio_output_configure(&session->audio, &session->audio_config);
    audio_output_set_speed(&session->audio, frame_timing_audio_ratio(&session->timing),
                           session->audio_config.rate_control);
}

CoreSessionFrame core_session_run_frame(CoreSession* session, AppShell* shell, const LibretroInputState* pointer,
                                        bool keyboard_for_joypad)
{
    // The due check is a loop tick, not "the core will run": it comes BEFORE
    // the paused/minimized checks on purpose, so the Exact timer keeps
    // advancing while paused. With vsync off, frame_timing_wait() is the only
    // thing throttling the loop; if the timer stood still, a paused session
    // would spin at 100% CPU. Don't move it below those checks.
    if (!frame_timing_frame_due(&session->timing))
    {
        return CoreSessionFrame::skipped;
    }
    if (!core_session_running(session) || (session->paused && !session->step_once))
    {
        return CoreSessionFrame::skipped;
    }
    // Minimized: nothing is presented, so vsync wouldn't pace the core.
    if (SDL_GetWindowFlags(shell->window) & SDL_WINDOW_MINIMIZED)
    {
        // The queue runs dry on purpose: clear it so restoring isn't an underrun.
        audio_output_clear(&session->audio);
        return CoreSessionFrame::skipped;
    }
    session->step_once = false;

    LibretroInputState input = {};
    fixed_input_read(&session->fixed_input, &input, keyboard_for_joypad && !session->keyboard.game_focus,
                     session->gamepad);
    keyboard_input_read(&session->keyboard, &input);
    if (pointer != nullptr)
    {
        input.pointer_valid = pointer->pointer_valid;
        input.pointer_x = pointer->pointer_x;
        input.pointer_y = pointer->pointer_y;
        input.pointer_pressed = pointer->pointer_pressed;
    }

    libretro_host_set_input(session->host, &input);

    // The frame, plus the ones a missed refresh lost (Vsync catch-up,
    // frame_timing.h). Each one's audio is queued; only the last picture is
    // uploaded and shown.
    uint32_t frames = 1 + (session->paused ? 0 : session->timing.catch_up_frames);
    for (uint32_t i = 0; i < frames; i++)
    {
        libretro_host_run_frame(session->host);

        if (session->host->shutdown_requested)
        {
            core_session_stop(session, shell);
            set_message(session, "The core asked to shut down (RETRO_ENVIRONMENT_SHUTDOWN)");
            return CoreSessionFrame::shut_down;
        }

        // A core may change its sample rate (SET_SYSTEM_AV_INFO): follow it first.
        // Single-stepping stays silent: one frame of audio after a gap is just a blip.
        audio_output_set_sample_rate(&session->audio, session->host->av_info.timing.sample_rate);
        if (!session->paused)
        {
            audio_output_push(&session->audio, session->host->audio.samples, session->host->audio.frames);
        }
    }

    cache_options(session);   // a core may declare its options again at runtime

    // A core may grow its image at runtime (SET_SYSTEM_AV_INFO).
    const LibretroVideoFrame* video = &session->host->video;
    if (video->width > 0 && video->height > 0)
    {
        ensure_texture(session, shell, video->width, video->height);
    }
    return CoreSessionFrame::ran;
}

void core_session_wait(const CoreSession* session)
{
    frame_timing_wait(&session->timing);
}

// ---------------------------------------------------------------------------
// Savestates
// ---------------------------------------------------------------------------

bool core_session_save_state(CoreSession* session)
{
    if (!core_session_running(session))
    {
        return false;
    }
    if (!libretro_host_save_state(session->host, session->state_slot))
    {
        set_message(session, "Could not save state slot %u (see the log)", session->state_slot);
        return false;
    }
    set_message(session, "Saved state slot %u", session->state_slot);
    return true;
}

bool core_session_load_state(CoreSession* session)
{
    if (!core_session_running(session))
    {
        return false;
    }
    if (!libretro_host_load_state(session->host, session->state_slot))
    {
        set_message(session, "Could not load state slot %u (see the log)", session->state_slot);
        return false;
    }
    // What was queued belongs to the time before the load.
    audio_output_clear(&session->audio);
    set_message(session, "Loaded state slot %u", session->state_slot);
    return true;
}

void core_session_change_slot(CoreSession* session, int32_t delta)
{
    int32_t slot = static_cast<int32_t>(session->state_slot) + delta;
    int32_t slots = static_cast<int32_t>(LIBRETRO_STATE_SLOTS);
    session->state_slot = static_cast<uint32_t>((slot % slots + slots) % slots);
}
