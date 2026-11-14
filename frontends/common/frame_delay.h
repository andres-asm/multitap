#pragma once

// Automatic frame delay (flow decided by Andrés 2026-11-14). Frame delay =
// wait this long after presenting before running the core, so input is read
// later; too long and the loop misses refreshes. AUTO finds, per game, the
// longest delay this machine sustains, then locks it:
//
//   1. a game starts at the highest delay the display allows
//      (FRAME_DELAY_START_FRACTION of a refresh: 12 ms at 60 Hz), whatever
//      the manual FRAME DELAY says;
//   2. the first FRAME_DELAY_WARMUP_SECONDS of play don't count: a game's
//      first seconds stall on their own (the core starting, textures,
//      GameMode switching the governor), which says nothing about the delay;
//   3. each missed refresh (frame_timing.h: a loop of 1.5 periods or more)
//      lowers it FRAME_DELAY_LOWER_MS;
//   4. FRAME_DELAY_LOCK_SECONDS of play in a row without a miss locks it for
//      the rest of the game. Uninterrupted: a miss, a pause, the menu or
//      another window having focus start the count over
//      (auto_frame_delay_interrupt).
//
// Only missed refreshes are looked at, which every machine can measure, not
// when frames reach the screen (needs present wait, not on every GPU or
// surface). The first seconds of a game may miss a few frames while it comes
// down (with catch-up the game doesn't fall behind; a picture repeats).
// Once locked nothing changes: a game that later gets more demanding (a
// heavier scene) may miss frames with that delay. Each game searches again.
//
// The frontend keeps the locked value per game and starts there next time
// (auto_frame_delay_restore), with a way to search again.
//
// Pure math: the frontend feeds it once per loop where the delay applies.

#include <cstdint>

constexpr double   FRAME_DELAY_START_FRACTION = 0.75;
constexpr uint32_t FRAME_DELAY_LOWER_MS       = 1;
constexpr double   FRAME_DELAY_LOCK_SECONDS   = 60.0;
constexpr double   FRAME_DELAY_WARMUP_SECONDS = 5.0;

struct AutoFrameDelay
{
    bool     started        = false;   // the start value was set (first update of a game)
    uint32_t delay_ms       = 0;       // the delay to use now
    double   warmup_seconds = 0.0;     // play since the start (misses ignored until FRAME_DELAY_WARMUP_SECONDS)
    double   stable_seconds = 0.0;     // uninterrupted play without a miss
    bool     locked         = false;   // found: no more changes this game
    uint32_t lowers         = 0;       // statistics
};

// A new game: the next update starts from the top again.
void auto_frame_delay_reset(AutoFrameDelay* delay);

// A value found before (kept by the frontend for this game): locked at once.
void auto_frame_delay_restore(AutoFrameDelay* delay, uint32_t delay_ms);

// The display's refresh rate -> where a game starts (ms, rounded down).
uint32_t auto_frame_delay_start_ms(double display_hz);

// Paused, menu open, window without focus, timing not Vsync...: the minute
// starts over (the delay stays). Nothing once locked.
void auto_frame_delay_interrupt(AutoFrameDelay* delay);

// Once per loop where the delay applies. missed: this loop missed a refresh;
// elapsed_seconds: since the previous call (0 on the first); start_ms: from
// auto_frame_delay_start_ms. Returns the delay to use (ms).
uint32_t auto_frame_delay_update(AutoFrameDelay* delay, bool missed, double elapsed_seconds, uint32_t start_ms);

// Seconds of uninterrupted play left until it locks (0 when locked).
double auto_frame_delay_seconds_to_lock(const AutoFrameDelay* delay);
