#pragma once

// Dynamic rate control (DRC) and the audio latency target, as pure math so it
// can be tested without an audio device (audio_output.h uses it).
//
// DRC keeps the audio queue at a target by playing a little faster or slower:
// at most max_deviation (0.5%: a pitch change nobody hears). It's a PI
// controller: the proportional part reacts to the queue now, the integral
// part learns a steady mismatch (the core's frames coming slightly faster or
// slower than the base speed assumes) so the queue sits AT the target instead
// of settling above or below it.
//
// Latency target, fixed or automatic (decided by Andrés 2026-11-13):
//   - fixed: the target stays where it was set;
//   - automatic: starts low and finds the smallest target this machine
//     survives. An underrun (the queue ran dry: a stall longer than the
//     queue) raises it RATE_CONTROL_AUTO_RAISE_MS. After
//     RATE_CONTROL_AUTO_QUIET_SECONDS without underruns, if the queue never
//     dropped below RATE_CONTROL_AUTO_MARGIN_MS in that time, it comes down
//     RATE_CONTROL_AUTO_LOWER_MS. Never below the floor (two device buffers
//     + a little, see audio_output) nor above RATE_CONTROL_MAX_TARGET_MS.
//
// The queue reading jumps by a whole device buffer each time the device takes
// one, so the controller works on a smoothed reading (RATE_CONTROL_SMOOTHING_SECONDS).
// The integral part only accumulates while the correction isn't already at
// its limit in that direction (anti-windup): otherwise a long way to the
// target (e.g. right after starting) would wind it up and it would overshoot.

#include <cstdint>

constexpr double   RATE_CONTROL_DEFAULT_MAX_DEVIATION = 0.005;
constexpr uint32_t RATE_CONTROL_MAX_TARGET_MS         = 200;
constexpr uint32_t RATE_CONTROL_AUTO_START_MS         = 40;
constexpr uint32_t RATE_CONTROL_AUTO_RAISE_MS         = 10;
constexpr uint32_t RATE_CONTROL_AUTO_LOWER_MS         = 5;
constexpr double   RATE_CONTROL_AUTO_QUIET_SECONDS    = 30.0;
constexpr double   RATE_CONTROL_AUTO_MARGIN_MS        = 15.0;
constexpr double   RATE_CONTROL_SMOOTHING_SECONDS     = 0.25;
// One stall can empty the queue on several pushes in a row: after a raise,
// underruns within this time don't raise again.
constexpr double   RATE_CONTROL_AUTO_RAISE_HOLD_SECONDS = 1.0;

struct RateControl
{
    // Settings
    bool     automatic     = true;
    double   max_deviation = RATE_CONTROL_DEFAULT_MAX_DEVIATION;
    uint32_t floor_ms      = 10;   // the lowest target allowed

    // State
    double   target_ms     = RATE_CONTROL_AUTO_START_MS;
    double   integral      = 0.0;   // the learned steady correction, -1..1 (x max_deviation)
    double   smoothed_ms   = -1.0;  // the smoothed queue (-1 = no reading yet)
    double   quiet_seconds = 0.0;   // since the last underrun or change of target
    double   since_raise   = 1e9;   // seconds since the last automatic raise
    double   low_water_ms  = 1e9;   // the lowest queue seen in that time
    uint32_t raises        = 0;     // statistics: automatic target changes
    uint32_t lowers        = 0;
};

// target_ms: where to start (automatic) or stay (fixed). floor_ms: the
// lowest target that makes sense for the device (see audio_output).
void rate_control_init(RateControl* control, uint32_t target_ms, bool automatic, uint32_t floor_ms);

// Called once per audio push. queued_ms: what's queued now; elapsed_seconds:
// since the previous call; underrun: this push found the queue dry (not
// during a warm-up). Returns the speed factor to apply on top of the base
// speed: 1 - max_deviation .. 1 + max_deviation (> 1 = play faster).
double rate_control_update(RateControl* control, double queued_ms, double elapsed_seconds, bool underrun);

// Forgets the learned correction (e.g. the base speed changed: the old one
// doesn't apply). The target stays.
void rate_control_reset(RateControl* control);
