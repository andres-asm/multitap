#pragma once

// When to run the core's next frame. Two timing modes, picked by the user
// (decided by Andrés, 2026-11-12):
//
//   Vsync  one retro_run per display refresh (vsync on). The core then runs at
//          the display rate, so audio plays display Hz / core fps faster or
//          slower, with dynamic rate control on top (audio_output).
//          Only possible when the core's fps is within VSYNC_TOLERANCE of the
//          display rate; otherwise it falls back to Exact on its own
//          (e.g. a 50 fps core on a 60 Hz display).
//   Exact  vsync off; frames are paced by a timer at exactly the core's fps.
//          Audio plays at its own rate, with DRC on top to absorb the drift
//          between the timer's clock and the audio device's. Later the
//          natural mode for FreeSync/G-Sync displays.
//
// Shared by every frontend (frontend_common). It may move into the host later,
// with the rest of the frame loop.

#include <cstdint>

enum class TimingMode
{
    vsync,
    exact,
};

constexpr double FRAME_TIMING_VSYNC_TOLERANCE = 0.01;   // 1%: 59.94 / 60 / 60.10 all use vsync on 60 Hz

struct FrameTiming
{
    TimingMode mode      = TimingMode::vsync;   // what the user picked
    TimingMode effective = TimingMode::vsync;   // what's used now (vsync may fall back)
    bool       fell_back = false;               // vsync picked, but exact in use

    double core_fps   = 0.0;
    double display_hz = 0.0;

    uint64_t next_frame_ns = 0;   // exact mode: when the next core frame is due (SDL_GetTicksNS)
};

// Once per loop: works out the effective mode from the user's choice, the
// core's fps and the display's refresh rate. With no core running it's vsync
// (nothing to pace).
void frame_timing_update(FrameTiming* timing, bool core_running, double core_fps, double display_hz);

// True if a core frame should run now. Vsync: always (vsync paces the loop).
// Exact: when the timer says so (and advances it).
bool frame_timing_frame_due(FrameTiming* timing);

// Exact mode: sleeps until the next frame is due (vsync off, so nothing else
// would stop the loop from spinning). Does nothing in vsync mode.
void frame_timing_wait(const FrameTiming* timing);

// The audio speed for audio_output_set_speed(): display Hz / core fps in
// vsync mode, 1 in exact mode.
double frame_timing_audio_ratio(const FrameTiming* timing);

const char* frame_timing_mode_name(TimingMode mode);
