// Checks the automatic frame delay (frame_delay.h) and the Vsync missed-
// refresh measurement with catch-up (frame_timing_measure_at), on simulated
// loop times. Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>

#include "frame_delay.h"
#include "frame_timing.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

constexpr double PERIOD = 1.0 / 60.0;

// A machine whose loop misses a refresh whenever the delay is above
// `budget_ms`: the delay must settle at or just under it.
static void test_auto_delay_finds_the_budget()
{
    AutoFrameDelay delay = {};
    uint32_t misses = 0;
    uint32_t misses_late = 0;
    constexpr uint32_t BUDGET_MS = 7;
    for (int frame = 0; frame < 60 * 600; frame++)   // 10 minutes
    {
        bool missed = delay.delay_ms > BUDGET_MS;
        misses += missed ? 1u : 0u;
        if (frame > 60 * 300 && missed)
        {
            misses_late++;
        }
        auto_frame_delay_update(&delay, missed, PERIOD, 12);
    }
    std::printf("auto delay, budget 7 ms, max 12: %u ms after 10 min, %u misses (%u in the last 5 min)\n",
                delay.delay_ms, misses, misses_late);
    check(delay.delay_ms >= BUDGET_MS - FRAME_DELAY_LOWER_MS && delay.delay_ms <= BUDGET_MS,
          "the delay settles at the budget");
    // Probes of the limit back off: at most a couple in the last 5 minutes.
    check(misses_late <= 2, "misses are rare once settled");
}

static void test_auto_delay_limits()
{
    AutoFrameDelay delay = {};
    for (int frame = 0; frame < 60 * 120; frame++)
    {
        auto_frame_delay_update(&delay, false, PERIOD, 4);
    }
    check(delay.delay_ms == 4, "without misses it climbs to the maximum, not past it");
    auto_frame_delay_update(&delay, false, PERIOD, 2);
    check(delay.delay_ms == 2, "a lower maximum applies at once");
    auto_frame_delay_update(&delay, true, PERIOD, 12);
    auto_frame_delay_update(&delay, true, PERIOD, 12);
    check(delay.delay_ms == 0, "misses never go below 0");
    auto_frame_delay_reset(&delay);
    check(delay.delay_ms == 0 && delay.raises == 0, "reset starts from 0");
}

// Feeds loop intervals (in periods) to frame_timing_measure_at and returns
// the extra frames asked for in total.
static uint32_t run_loops(FrameTiming* timing, const double* intervals, int count, uint64_t* now)
{
    uint32_t extra = 0;
    for (int i = 0; i < count; i++)
    {
        *now += static_cast<uint64_t>(intervals[i] * PERIOD * 1e9);
        frame_timing_measure_at(timing, true, *now);
        extra += timing->catch_up_frames;
    }
    return extra;
}

static FrameTiming vsync_timing(bool catch_up)
{
    FrameTiming timing = {};
    timing.effective = TimingMode::vsync;
    timing.display_hz = 60.0;
    timing.catch_up = catch_up;
    return timing;
}

static void test_catch_up()
{
    uint64_t now = 1000000000ull;
    FrameTiming timing = vsync_timing(true);
    double steady[600];
    for (int i = 0; i < 600; i++)
    {
        steady[i] = (i % 2 == 0) ? 0.6 : 1.4;   // jitter, steady on average
    }
    uint32_t extra = run_loops(&timing, steady, 600, &now);
    check(extra == 0, "jitter alone asks for no extra frames");
    check(timing.missed_total == 0, "jitter alone misses no refresh");

    double stall[] = {1.0, 3.0, 1.0, 1.0};   // one loop of 3 periods: 2 refreshes lost
    extra = run_loops(&timing, stall, 4, &now);
    std::printf("catch-up: a 3-period loop -> %u extra frames, missed %llu\n", extra,
                static_cast<unsigned long long>(timing.missed_total));
    check(extra == 2, "a loop of 3 periods asks for the 2 frames it lost");
    check(timing.missed_total == 1, "and counts one missed loop");

    double long_stall[] = {10.0, 1.0, 1.0, 1.0, 1.0};
    extra = run_loops(&timing, long_stall, 5, &now);
    check(extra <= FRAME_TIMING_MAX_CATCH_UP + 1, "a long stall isn't replayed");

    // A display a little faster than measured (0.1%) for 10 minutes: no
    // extra frames from the error adding up.
    static double drift[36000];
    for (int i = 0; i < 36000; i++)
    {
        drift[i] = 1.001;
    }
    extra = run_loops(&timing, drift, 36000, &now);
    check(extra == 0, "a small steady error doesn't add up into extra frames");

    FrameTiming off = vsync_timing(false);
    extra = run_loops(&off, stall, 4, &now);
    check(extra == 0 && off.missed_total == 1, "catch-up off: misses counted, no extra frames");

    frame_timing_measure_at(&timing, false, now + 1000000000ull);
    check(timing.last_loop_ns == 0 && timing.behind_ns == 0.0, "no frames running restarts the measurement");
}

int main()
{
    test_auto_delay_finds_the_budget();
    test_auto_delay_limits();
    test_catch_up();
    if (g_failures == 0)
    {
        std::printf("frame delay: all checks passed\n");
        return 0;
    }
    std::printf("frame delay: %d check(s) failed\n", g_failures);
    return 1;
}
