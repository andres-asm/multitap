#pragma once

// host-libretro: our libretro implementation.
//
// A LibretroHost owns one loaded core and everything the core was told or
// asked for. Lifecycle, in this order:
//
//   libretro_host_load_core()     dlopen, symbol check, set_environment, get_system_info, retro_init
//   libretro_host_load_game()     retro_load_game, retro_get_system_av_info
//   libretro_host_set_input()     (each frame) what the frontend's controls say
//   libretro_host_run_frame()     (each frame) retro_run; the frame ends up in host->video,
//                                 its audio in host->audio
//   libretro_host_unload_game()   retro_unload_game
//   libretro_host_unload_core()   retro_deinit, dlclose
//
// Multi-instancing: libretro callbacks carry no userdata pointer, so the host
// keeps ONE thread_local "current instance" pointer. It is set right before
// every call into the core and cleared right after (see libretro_host.cpp).
// That is the only hidden global state in the host, by design.

#include <cstddef>
#include <cstdint>

#include "libretro.h"
#include "libretro_audio.h"
#include "libretro_core.h"
#include "libretro_options.h"
#include "libretro_video.h"

constexpr size_t LIBRETRO_PATH_MAX = 4096;

constexpr unsigned LIBRETRO_MAX_PORTS = 4;

// Savestate slots 0..LIBRETRO_STATE_SLOTS-1, one file each.
constexpr uint32_t LIBRETRO_STATE_SLOTS = 10;

// RETRO_DEVICE_KEYBOARD key states are indexed by RETROK_* code.
constexpr unsigned LIBRETRO_KEY_COUNT = RETROK_LAST;

// Input as the frontend sees it this frame, in RetroPad terms. The frontend
// fills this (keyboard, gamepads, mouse... mapped however it likes) and hands it
// over with libretro_host_set_input() before libretro_host_run_frame().
struct LibretroInputState
{
    uint16_t joypad_buttons[LIBRETRO_MAX_PORTS] = {};   // bit N = RETRO_DEVICE_ID_JOYPAD_N
    int16_t  analog[LIBRETRO_MAX_PORTS][2][2]   = {};   // [port][left/right stick][x/y], -0x8000..0x7fff

    // Pointer (port 0 only): -0x7fff..0x7fff across the image the core draws.
    bool     pointer_valid   = false;   // false = pointer outside the image / unknown
    int16_t  pointer_x       = 0;
    int16_t  pointer_y       = 0;
    bool     pointer_pressed = false;

    // Keyboard (RETRO_DEVICE_KEYBOARD, any port): 1 = key held, by RETROK_* code.
    // Cores that want typed text use the keyboard callback instead
    // (libretro_host_keyboard_event), which also carries the character.
    uint8_t  keys[LIBRETRO_KEY_COUNT] = {};
};

// What a core says about itself before it is started (libretro_host_peek_core).
// The canonical source: the core's own answers, not an info file.
constexpr size_t LIBRETRO_INFO_TEXT_MAX = 256;
constexpr size_t LIBRETRO_EXTENSIONS_MAX = 1024;

struct LibretroCoreInfo
{
    bool     usable = false;                          // false: see `error`
    char     error[LIBRETRO_INFO_TEXT_MAX] = {};
    bool     already_loaded = false;                  // refused: the library is loaded in this process (running?)

    char     name[LIBRETRO_INFO_TEXT_MAX]    = {};    // retro_system_info.library_name
    char     version[LIBRETRO_INFO_TEXT_MAX] = {};    // library_version
    char     extensions[LIBRETRO_EXTENSIONS_MAX] = {}; // valid_extensions as reported ("sfc|smc|..."), may be empty
    bool     need_fullpath    = false;
    bool     block_extract    = false;
    bool     supports_no_game = false;                // SET_SUPPORT_NO_GAME during retro_set_environment
    uint32_t option_count     = 0;                    // core options declared during retro_set_environment
    uint32_t options_api      = 0;                    // 2, 1, 0 (SET_VARIABLES), if option_count > 0
    unsigned api_version      = 0;                    // retro_api_version()
};

// Counters for debug UIs and tests.
struct LibretroHostStats
{
    uint64_t frames_run          = 0;   // retro_run calls
    uint64_t video_frames        = 0;   // video_refresh with a new image
    uint64_t video_duplicates    = 0;   // video_refresh with NULL
    uint64_t video_errors        = 0;   // frames we couldn't store (bad format, HW frame, no memory)
    uint64_t audio_frames        = 0;   // stereo frames received (both audio callbacks)
    uint64_t audio_errors        = 0;   // audio callbacks whose samples we couldn't store (no memory)
    uint64_t input_polls         = 0;
};

// Directories we hand to the core. Empty string = "not defined".
struct LibretroHostConfig
{
    const char* system_directory = "";
    const char* save_directory   = "";

    // Where the HOST keeps per-game files: SRAM/RTC in <saves>/<core>/<game>.srm
    // (.rtc) and savestates in <states>/<core>/<game>.state<N>. Empty = not kept.
    // (save_directory above is what the core is told for its OWN files.)
    const char* saves_directory  = "";
    const char* states_directory = "";

    // Highest core options version we tell the core about (GET_CORE_OPTIONS_VERSION):
    // 2 = v2, 1 = v1, 0 = don't answer at all, like a frontend from before
    // options existed. It only changes what the core THINKS we support (so its
    // v1/v0 fallbacks can be tested); every format is accepted regardless.
    uint32_t core_options_version = 2;

    // What GET_LANGUAGE answers: a RETRO_LANGUAGE_* value (the frontend's
    // language, so cores can show their own text in it).
    uint32_t language = 0;   // RETRO_LANGUAGE_ENGLISH
};

constexpr uint32_t LIBRETRO_CORE_OPTIONS_VERSION_MAX = 2;

struct LibretroHost
{
    LibretroCore core = {};

    // Our own copies: the core may keep the pointers we give it, so these
    // must live as long as the core is loaded.
    char core_path[LIBRETRO_PATH_MAX]        = {};
    char system_directory[LIBRETRO_PATH_MAX] = {};
    char save_directory[LIBRETRO_PATH_MAX]   = {};
    char saves_directory[LIBRETRO_PATH_MAX]  = {};
    char states_directory[LIBRETRO_PATH_MAX] = {};
    char content_path[LIBRETRO_PATH_MAX]     = {};

    // Content loaded into memory (when the core doesn't need a path).
    // Kept until retro_unload_game(): the core may keep pointing into it.
    uint8_t* content_data = nullptr;
    size_t   content_size = 0;

    bool core_initialized = false;   // retro_init() ran
    bool game_loaded      = false;   // retro_load_game() succeeded

    // Filled from the core.
    retro_system_info    system_info = {};
    retro_system_av_info av_info     = {};

    // Video, audio and input exchanged with the core (see libretro_video.h, libretro_audio.h).
    LibretroVideoFrame  video = {};
    LibretroAudioBuffer audio = {};   // this frame's audio only: cleared by every run_frame
    LibretroInputState  input = {};
    LibretroHostStats  stats = {};

    // Core options (libretro_options.h). Declared by the core in retro_set_environment,
    // kept until the core is unloaded. Frontends read this list and change values
    // with libretro_host_set_option().
    LibretroOptions options              = {};
    uint32_t        core_options_version = 2;   // copied from the config at load
    uint32_t        language = 0;               // copied from the config at load (GET_LANGUAGE)

    // Set by the core with SET_KEYBOARD_CALLBACK; nullptr = it doesn't want key events.
    retro_keyboard_event_t keyboard_callback = nullptr;

    // What the core asked for through the environment callback.
    retro_pixel_format pixel_format       = RETRO_PIXEL_FORMAT_0RGB1555;   // libretro's default
    bool               supports_no_game   = false;
    bool               shutdown_requested = false;
    bool               peeking            = false;   // libretro_host_peek_core: environment calls logged at debug level only
    unsigned           performance_level  = 0;

    // Environment statistics (for logging and the debug UI later).
    // Indexed by the command number without the EXPERIMENTAL/PRIVATE bits.
    uint32_t environment_call_counts[256] = {};
};

bool libretro_host_load_core(LibretroHost* host, const char* core_path, const LibretroHostConfig* config);

// content_path == nullptr starts the core without content (needs SET_SUPPORT_NO_GAME).
bool libretro_host_load_game(LibretroHost* host, const char* content_path);

// Replaces the input the core will see on its next input_state calls.
void libretro_host_set_input(LibretroHost* host, const LibretroInputState* input);

// Writes the core's save RAM (and RTC, if any) to the data directory now.
// Also done automatically when the game is unloaded; call this too every so
// often so a crash loses little. Returns false if anything failed (logged).
bool libretro_host_save_memory(LibretroHost* host);

// Savestates: retro_serialize into <data>/states/<core>/<game>.state<slot>,
// and back with retro_unserialize. Call between frames. Return false if the
// core can't (serialize size 0), the slot is out of range, there's no data
// directory, or a file operation failed; the reason is logged.
bool libretro_host_save_state(LibretroHost* host, uint32_t slot);
bool libretro_host_load_state(LibretroHost* host, uint32_t slot);

// The file a slot uses (whether or not it exists yet). False if there's no data directory.
bool libretro_host_state_path(const LibretroHost* host, uint32_t slot, char* out, size_t out_size);

// Forwards one key event to the core's keyboard callback, if it registered
// one (SET_KEYBOARD_CALLBACK). keycode = RETROK_*, character = the UTF-32
// character typed (0 if none), modifiers = RETROKMOD_* bits. Typed text can
// come as key-down events with keycode RETROK_UNKNOWN and only a character.
// Call outside retro_run (between frames). Returns false if the core has no callback.
bool libretro_host_keyboard_event(LibretroHost* host, bool down, unsigned keycode, uint32_t character, uint16_t modifiers);

// Changes a core option. The core sees it on its next GET_VARIABLE_UPDATE /
// GET_VARIABLE (usually during the next retro_run). Fails (logged) for an
// unknown key or a value that isn't one of the option's values.
bool libretro_host_set_option(LibretroHost* host, const char* key, const char* value);

// Runs one frame (retro_run). Needs a loaded game. Returns false if nothing
// could run (no game, or the core asked to shut down). Afterwards host->audio
// holds exactly the audio this frame produced; play or drop it before the next call.
bool libretro_host_run_frame(LibretroHost* host);

// retro_reset: the core's "reset button". Does nothing if no game is loaded.
void libretro_host_reset(LibretroHost* host);

// Both are safe to call when nothing is loaded.
void libretro_host_unload_game(LibretroHost* host);
void libretro_host_unload_core(LibretroHost* host);

// Loads the core only far enough to ask it about itself: retro_set_environment
// (catches SET_SUPPORT_NO_GAME, core options, ...) and retro_get_system_info,
// never retro_init, then unloads it. Same approach as RetroArch's system info
// probe. Returns false if the file isn't a usable core (info->error says why).
// Uses its own temporary host, so other cores can keep running. A core whose
// library is already loaded in this process (running in another host, however
// its path is spelled) is refused (info->already_loaded): peeking would run
// retro_set_environment a second time on the live core.
bool libretro_host_peek_core(const char* core_path, LibretroCoreInfo* info);

// A core's short name, as save files and settings use it: "snes9x" from
// ".../snes9x_libretro.so". False if it doesn't fit in `out`.
bool libretro_core_short_name(const char* core_path, char* out, size_t out_size);

// True if a core with this info can start `content_path`: its extension is in
// the core's list (case-insensitive; an empty list accepts anything), or, for
// no content (nullptr or ""), if the core supports running without content.
bool libretro_core_info_accepts(const LibretroCoreInfo* info, const char* content_path);

// The instance the core is currently running in, or nullptr outside core calls.
LibretroHost* libretro_host_current();
