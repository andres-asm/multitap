#pragma once

// A libretro core running in a frontend window: everything between "the user
// picked a core and content" and "the picture is on screen and the sound is
// playing". Shared by every libretro frontend (piccolo, the host-libretro test
// frontend), which only add their own UI around it.
//
// Per loop:
//   while (SDL_PollEvent(&event))
//       if (!core_session_handle_event(&session, &event, ...)) { ...frontend's own events... }
//   core_session_update(&session, &shell);                 timing mode -> vsync + audio speed
//   core_session_run_frame(&session, &shell, ...);         when a frame is due
//   app_shell_begin_frame(...); ...UI, showing session.texture...; core_session_upload_video(...);
//   app_shell_present(...);
//   core_session_wait(&session);                           Exact mode: sleep until the next frame

#include <cstdint>

#include "audio_output.h"
#include "fixed_input.h"
#include "frame_timing.h"
#include "keyboard_input.h"
#include "libretro_host.h"
#include "vk_texture.h"

struct AppShell;
struct SDL_Gamepad;
union SDL_Event;

enum class CoreSessionFrame
{
    skipped,     // nothing ran (no core, paused, minimized, or not due)
    ran,
    shut_down,   // the core asked to shut down; the session stopped it
};

struct CoreSession
{
    LibretroHost* host = nullptr;   // owned; allocated by core_session_create

    // Settings for the next start.
    char     saves_directory[LIBRETRO_PATH_MAX] = {};                     // SRAM ("" = not kept)
    char     states_directory[LIBRETRO_PATH_MAX] = {};                    // savestates ("" = none)
    char     system_directory[LIBRETRO_PATH_MAX] = {};                    // BIOS etc. ("" = none)
    uint32_t options_version = LIBRETRO_CORE_OPTIONS_VERSION_MAX;         // reported to the core
    uint32_t language = 0;   // RETRO_LANGUAGE_* for GET_LANGUAGE (0 = English)

    // The core's picture, uploaded once per new frame.
    StreamingTexture texture         = {};
    uint64_t         texture_id      = 0;   // ImGui texture id, 0 = none
    uint64_t         uploaded_serial = 0;   // host->video.serial in the texture

    AudioOutput   audio       = {};   // open while a core runs
    FrameTiming   timing      = {};   // Vsync / Exact
    FixedInput    fixed_input = {};
    KeyboardInput keyboard    = {};   // libretro keyboard + game focus
    SDL_Gamepad*  gamepad     = nullptr;

    bool     paused     = false;
    bool     step_once  = false;      // run exactly one frame while paused
    uint32_t state_slot = 0;

    char message[512] = {};           // what the last start/stop/state call did, for the UI
};

// Allocates the host. False only if out of memory.
bool core_session_create(CoreSession* session);
// Stops the core and frees everything (safe after a failed create).
void core_session_destroy(CoreSession* session, AppShell* shell);

// Loads the core, then the content (nullptr or "" = none). Paths are used as
// given (resolve "~" etc. before). On failure the core is unloaded again and
// `message` says why.
//
// Start and stop destroy `texture`. Never call them in a frame after the
// texture was handed to ImGui (image, draw list): that frame would render
// with a destroyed descriptor set. Call them before the UI draws the picture,
// or outside the frame (e.g. remember a UI choice and act on it next loop).
bool core_session_start(CoreSession* session, AppShell* shell, const char* core_path, const char* content_path);
void core_session_stop(CoreSession* session, AppShell* shell);
bool core_session_running(const CoreSession* session);

// Keyboard (game focus), gamepads coming and going, short taps on the fixed
// controls. Returns true if the event was the core's keyboard input and the
// frontend should ignore it. `keyboard_for_joypad`: false while the frontend's
// own UI wants the keyboard (a text field has focus).
bool core_session_handle_event(CoreSession* session, AppShell* shell, const SDL_Event* event, bool keyboard_for_joypad);

// Once per loop, before running frames: timing mode (from the user's choice,
// the core's fps and the display), vsync on/off and the audio speed.
void core_session_update(CoreSession* session, AppShell* shell);

// Runs one core frame if one is due: input (fixed controls, keyboard, and the
// pointer fields of `pointer` if not nullptr), retro_run, audio, texture size.
CoreSessionFrame core_session_run_frame(CoreSession* session, AppShell* shell, const LibretroInputState* pointer,
                                        bool keyboard_for_joypad);

// Uploads the newest core frame into `texture` (outside the rendering pass;
// call after building the UI, before app_shell_present).
void core_session_upload_video(CoreSession* session, AppShell* shell);

// Exact timing mode: sleeps until the next frame is due. Call at the end of the loop.
void core_session_wait(const CoreSession* session);

// Savestates in `state_slot`. False if nothing runs or it failed (`message` says why).
bool core_session_save_state(CoreSession* session);
bool core_session_load_state(CoreSession* session);
void core_session_change_slot(CoreSession* session, int32_t delta);
