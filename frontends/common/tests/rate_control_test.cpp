// Checks rate_control (DRC + latency target) on a simulated audio path that
// behaves like the real one (audio_output):
//   - a frame loop pushes 800 frames per frame (a 60 fps core at 48 kHz),
//     resampled by the speed rate control asks for, at a rate that may be a
//     little off from what the base speed assumes, with occasional stalls;
//   - our queue (what audio_output measures) feeds a device that takes whole
//     buffers of DEVICE_BUFFER frames from it whenever its own buffer runs
//     out, and plays 48000 frames a second;
//   - an AUDIBLE gap is the device needing a buffer and our queue being
//     empty; our queue reading 0 at a push (what rate control is told) can
//     happen without one, right after the device took a buffer.
// Only the very first push starts from 50 ms of silence, like audio_output.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>

#include "rate_control.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

constexpr double DEVICE_RATE = 48000.0;
constexpr double DEVICE_BUFFER = 1024.0;   // 21 ms, like PipeWire here
constexpr double FRAMES_PER_PUSH = 800.0;
constexpr uint32_t FLOOR_MS = 52;          // audio_output: 2 x 21 ms + 10

struct Simulation
{
    double frame_seconds = 1.0 / 59.95;   // how long a loop frame really takes
    double base_speed = 59.95 / 60.0;     // what the frontend believes (display Hz / core fps)
    double stall_seconds = 0.0;           // extra time of a stalled frame
    double stall_every_seconds = 0.0;     // 0 = no stalls

    double queued = DEVICE_RATE * 0.05;   // our queue, frames (starts from the 50 ms cushion)
    double device = 0.0;                  // what the device holds, frames
    bool   in_gap = false;
    double time = 0.0;
    double next_stall = 0.0;
    uint32_t gaps = 0;                    // audible
    uint32_t gaps_after_settling = 0;
    uint32_t dry_readings = 0;            // our queue read 0 at a push
    double sum_queued_ms = 0.0;
    uint32_t samples = 0;
};

// The device plays `seconds`, taking a whole buffer from our queue each time
// its own runs out.
static void play(Simulation* sim, double seconds, double settle_seconds)
{
    constexpr double STEP = 0.0005;
    for (double t = 0.0; t < seconds; t += STEP)
    {
        double step = seconds - t < STEP ? seconds - t : STEP;
        sim->device -= DEVICE_RATE * step;
        if (sim->device > 0.0)
        {
            sim->in_gap = false;
            continue;
        }
        double take = sim->queued < DEVICE_BUFFER ? sim->queued : DEVICE_BUFFER;
        sim->queued -= take;
        sim->device += take;
        if (sim->device <= 0.0)
        {
            sim->device = 0.0;
            if (!sim->in_gap)
            {
                sim->gaps++;
                if (sim->time + t > settle_seconds)
                {
                    sim->gaps_after_settling++;
                }
            }
            sim->in_gap = true;
        }
        else
        {
            sim->in_gap = false;
        }
    }
}

static void run(Simulation* sim, RateControl* control, double seconds, double settle_seconds)
{
    if (sim->stall_every_seconds > 0.0 && sim->next_stall == 0.0)
    {
        sim->next_stall = sim->stall_every_seconds;
    }
    double end = sim->time + seconds;
    while (sim->time < end)
    {
        double elapsed = sim->frame_seconds;
        if (sim->stall_every_seconds > 0.0 && sim->time >= sim->next_stall)
        {
            elapsed += sim->stall_seconds;
            sim->next_stall += sim->stall_every_seconds;
        }
        play(sim, elapsed, settle_seconds);
        sim->time += elapsed;

        bool dry = sim->queued < 1.0;
        if (dry)
        {
            sim->dry_readings++;
        }
        double queued_ms = sim->queued * 1000.0 / DEVICE_RATE;
        if (sim->time > settle_seconds)
        {
            sim->sum_queued_ms += queued_ms;
            sim->samples++;
        }
        double correction = rate_control_update(control, queued_ms, elapsed, dry);
        // Resampled to the device: fewer frames when playing faster.
        sim->queued += FRAMES_PER_PUSH / (sim->base_speed * correction);
    }
}

static double average_ms(const Simulation* sim)
{
    return sim->samples > 0 ? sim->sum_queued_ms / sim->samples : 0.0;
}

static void test_pi_reaches_the_target()
{
    // Frames come 0.2% slower than the base speed assumes. Proportional
    // control alone would settle well below the target; the integral fixes it.
    RateControl control = {};
    rate_control_init(&control, 80, false, FLOOR_MS);
    Simulation sim = {};
    sim.frame_seconds = 1.0 / (59.95 * 0.998);
    run(&sim, &control, 240.0, 180.0);
    double average = average_ms(&sim);
    std::printf("fixed 80 ms, 0.2%% off: average %.1f ms, %u audible gaps\n", average, sim.gaps);
    check(average > 75.0 && average < 85.0, "the queue sits at the target despite a steady 0.2% error");
    check(sim.gaps == 0, "no audible gaps without stalls");
    check(control.target_ms == 80.0, "a fixed target doesn't move");

    rate_control_init(&control, 80, false, FLOOR_MS);
    Simulation faster = {};
    faster.frame_seconds = 1.0 / (59.95 * 1.002);
    run(&faster, &control, 240.0, 180.0);
    average = average_ms(&faster);
    std::printf("fixed 80 ms, 0.2%% the other way: average %.1f ms\n", average);
    check(average > 75.0 && average < 85.0, "the queue sits at the target with frames 0.2% fast too");
}

static void test_auto_without_stalls()
{
    // Starting at the default (clamped to the floor): no gaps at all, and the
    // target stays near the floor.
    RateControl control = {};
    rate_control_init(&control, RATE_CONTROL_AUTO_START_MS, true, FLOOR_MS);
    Simulation sim = {};
    run(&sim, &control, 600.0, 0.0);
    std::printf("auto from the floor, no stalls: target %.0f ms (%u raises), %u audible gaps, %u dry readings\n",
                control.target_ms, control.raises, sim.gaps, sim.dry_readings);
    check(sim.gaps == 0, "no audible gaps at the floor without stalls");
    check(control.target_ms <= 70.0, "the target stays near the floor without stalls");
}

static void test_auto_rises_until_stalls_stop_hurting()
{
    // A 50 ms stall every 20 s (about what was measured on Andrés's ARM board
    // in a Debug build). In Vsync timing a stall loses a frame of audio for
    // good, which only DRC's 0.5% gives back, so stalls must be occasional
    // for any target to help (here they cost 0.25% of the time).
    RateControl control = {};
    rate_control_init(&control, RATE_CONTROL_AUTO_START_MS, true, FLOOR_MS);
    Simulation sim = {};
    sim.stall_seconds = 0.050;
    sim.stall_every_seconds = 20.0;
    run(&sim, &control, 1500.0, 900.0);
    std::printf("auto, 50 ms stalls every 20 s: target %.0f ms after %u raises / %u lowers, %u audible gaps "
                "(%u in the last 600 s)\n",
                control.target_ms, control.raises, control.lowers, sim.gaps, sim.gaps_after_settling);
    check(control.target_ms >= FLOOR_MS && control.target_ms <= 120.0, "the target settles at a modest value");
    check(sim.gaps_after_settling == 0, "no audible gaps once it has settled");
}

static void test_auto_comes_down_without_stalls()
{
    RateControl control = {};
    rate_control_init(&control, 120, true, FLOOR_MS);
    Simulation sim = {};
    run(&sim, &control, 1200.0, 0.0);
    std::printf("auto from 120 ms, no stalls: target %.0f ms after %u lowers, %u audible gaps\n", control.target_ms,
                control.lowers, sim.gaps);
    check(control.target_ms <= 80.0, "without stalls the target comes down");
    check(control.target_ms >= FLOOR_MS, "but never below the floor");
    check(sim.gaps == 0, "coming down causes no audible gaps");
}

static void test_limits()
{
    RateControl control = {};
    rate_control_init(&control, 500, true, FLOOR_MS);
    check(control.target_ms == RATE_CONTROL_MAX_TARGET_MS, "a target above the maximum is clamped");
    rate_control_init(&control, 5, true, FLOOR_MS);
    check(control.target_ms == FLOOR_MS, "a target below the floor is raised to it");

    // The correction never exceeds max_deviation, whatever the queue.
    rate_control_init(&control, 60, false, FLOOR_MS);
    double fast = rate_control_update(&control, 10000.0, 0.016, false);
    rate_control_init(&control, 60, false, FLOOR_MS);
    double slow = rate_control_update(&control, 0.0, 0.016, false);
    check(fast <= 1.0 + RATE_CONTROL_DEFAULT_MAX_DEVIATION + 1e-12, "never faster than max_deviation");
    check(slow >= 1.0 - RATE_CONTROL_DEFAULT_MAX_DEVIATION - 1e-12, "never slower than max_deviation");

    // Anti-windup: far above the target for a long time, the integral doesn't
    // pile up while the correction is already at its limit.
    rate_control_init(&control, 60, false, FLOOR_MS);
    for (int i = 0; i < 6000; i++)
    {
        rate_control_update(&control, 200.0, 0.016, false);
    }
    std::printf("anti-windup: integral %.3f after 96 s far above the target\n", control.integral);
    check(control.integral < 0.05, "the integral doesn't wind up while saturated");
    rate_control_reset(&control);
    check(control.integral == 0.0, "reset forgets the learned correction");
}

int main()
{
    test_pi_reaches_the_target();
    test_auto_without_stalls();
    test_auto_rises_until_stalls_stop_hurting();
    test_auto_comes_down_without_stalls();
    test_limits();
    if (g_failures == 0)
    {
        std::printf("rate control: all checks passed\n");
        return 0;
    }
    std::printf("rate control: %d check(s) failed\n", g_failures);
    return 1;
}
