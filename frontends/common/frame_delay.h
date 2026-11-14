#pragma once

// Automatic frame delay (decided by Andrés 2026-11-14): finds the longest
// frame delay (frontend setting: wait this long after presenting before
// running the core, so input is read later) that doesn't make the loop miss
// refreshes, up to the delay the user set as the maximum.
//
// It only looks at missed refreshes (frame_timing.h: a loop of 1.5 periods
// or more), which every machine can measure, rather than at when frames
// reach the screen, which needs present wait (not on every GPU/surface):
//   - no miss for FRAME_DELAY_RAISE_AFTER_SECONDS: 1 ms up (to the maximum);
//   - a miss: FRAME_DELAY_LOWER_MS down (to 0), and no raising for a while,
//     so it doesn't climb straight back into it. The wait starts at
//     FRAME_DELAY_HOLD_SECONDS and doubles each time a raise caused the miss
//     (a miss within FRAME_DELAY_RAISE_BLAME_SECONDS of it), up to
//     FRAME_DELAY_MAX_HOLD_SECONDS: once the limit is found it's probed
//     rarely, not every few seconds (each probe is one missed frame).
// A miss the delay didn't cause (the system stalled) lowers it too: that
// costs a little latency for a while, never a missed frame.
//
// Pure math: the frontend feeds it once per loop where the delay applies.

#include <cstdint>

constexpr double   FRAME_DELAY_RAISE_AFTER_SECONDS = 2.0;
constexpr uint32_t FRAME_DELAY_LOWER_MS            = 2;
constexpr double   FRAME_DELAY_HOLD_SECONDS        = 10.0;
constexpr double   FRAME_DELAY_MAX_HOLD_SECONDS    = 320.0;
constexpr double   FRAME_DELAY_RAISE_BLAME_SECONDS = 5.0;

struct AutoFrameDelay
{
    uint32_t delay_ms      = 0;     // the delay to use now
    double   quiet_seconds = 0.0;   // since the last miss or raise
    double   hold_seconds  = 0.0;   // no raising until this runs out
    double   next_hold     = FRAME_DELAY_HOLD_SECONDS;   // the wait after the next miss
    double   since_raise   = 1e9;   // seconds since the last raise
    uint32_t raises        = 0;     // statistics
    uint32_t lowers        = 0;
};

// Starts again from 0 ms (e.g. a new game).
void auto_frame_delay_reset(AutoFrameDelay* delay);

// Once per loop where the delay applies. missed: this loop missed a refresh;
// elapsed_seconds: since the previous call; max_ms: the user's maximum.
// Returns the delay to use (ms).
uint32_t auto_frame_delay_update(AutoFrameDelay* delay, bool missed, double elapsed_seconds, uint32_t max_ms);
