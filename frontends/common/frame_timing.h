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
// Missed refreshes and catch-up (Vsync; decided by Andrés 2026-11-14): a
// loop that takes 1.5 refresh periods or more missed a refresh (the system
// stalled: a slow frame, the CPU clock dropping). In Vsync each loop runs one
// core frame, so a missed refresh is a core frame lost for good, and its
// audio with it (the queue drains; DRC's 0.5% can't give it back quickly).
// frame_timing_measure() keeps how much real time the core frames are behind
// (each loop adds its length minus one refresh period) and, with catch_up on,
// asks for extra core frames to make it up: one per period behind, at most
// FRAME_TIMING_MAX_CATCH_UP per loop. Only the last one is shown. More behind
// than that is dropped (a long stall isn't replayed). The count slowly decays
// toward 0, so a display rate a little off from the measured one doesn't add
// up into extra frames. Without catch-up the measurement still feeds the
// statistics and the automatic frame delay (frame_delay.h).
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
constexpr uint32_t FRAME_TIMING_MAX_CATCH_UP = 2;         // extra core frames per loop, at most

struct FrameTiming
{
    TimingMode mode      = TimingMode::vsync;   // what the user picked
    TimingMode effective = TimingMode::vsync;   // what's used now (vsync may fall back)
    bool       fell_back = false;               // vsync picked, but exact in use

    double core_fps   = 0.0;
    double display_hz = 0.0;

    // Exact picked, but the driver couldn't turn vsync off (FIFO only): the
    // timer still paces the core, which keeps cores slower than the display
    // right, but presents wait for vsync, so cores faster than the display run
    // at the display's speed. A driver limit; UIs should say so.
    bool fifo_forced = false;
    bool fifo_warned = false;   // logged once per core start

    uint64_t next_frame_ns = 0;   // exact mode: when the next core frame is due (SDL_GetTicksNS)

    // Vsync: missed refreshes and catch-up (frame_timing_measure).
    bool     catch_up        = true;   // run the core frames a stall lost (frontend setting)
    uint64_t last_loop_ns    = 0;      // 0 = not measuring (no frames ran last loop)
    double   behind_ns       = 0.0;    // real time the core frames haven't matched yet
    bool     missed          = false;  // this loop came a refresh late or more
    uint32_t catch_up_frames = 0;      // extra core frames to run this loop
    uint64_t missed_total    = 0;      // statistics, for debug UIs
    uint64_t caught_up_total = 0;
};

// Once per loop: works out the effective mode from the user's choice, the
// core's fps and the display's refresh rate. With no core running it's vsync
// (nothing to pace). vsync_forced: app_shell_vsync_forced() (sets fifo_forced
// while in Exact).
void frame_timing_update(FrameTiming* timing, bool core_running, double core_fps, double display_hz, bool vsync_forced);

// Once per loop, before the core frame: measures the loop in Vsync timing
// (missed, catch_up_frames). running_frames: core frames run this loop (a
// core runs, not paused, not minimized); otherwise the measurement restarts.
// Uses SDL_GetTicksNS(); frame_timing_measure_at() takes the time (tests).
void frame_timing_measure(FrameTiming* timing, bool running_frames);
void frame_timing_measure_at(FrameTiming* timing, bool running_frames, uint64_t now_ns);

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
