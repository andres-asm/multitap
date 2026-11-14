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

// A machine that misses a refresh whenever the delay is above `budget_ms`:
// the delay comes down from the top to the budget and locks after a minute.
static void test_auto_delay_comes_down_and_locks()
{
    check(auto_frame_delay_start_ms(60.0) == 12, "60 Hz starts at 12 ms");
    check(auto_frame_delay_start_ms(120.0) == 6, "120 Hz starts at 6 ms");
    check(auto_frame_delay_start_ms(0.0) == 0, "unknown refresh rate starts at 0");

    AutoFrameDelay delay = {};
    constexpr uint32_t BUDGET_MS = 7;
    uint32_t misses = 0;
    uint32_t misses_after_lock = 0;
    double locked_at = -1.0;
    for (int frame = 0; frame < 60 * 300; frame++)   // 5 minutes
    {
        bool missed = delay.started && delay.delay_ms > BUDGET_MS;
        misses += missed ? 1u : 0u;
        if (missed && delay.locked)
        {
            misses_after_lock++;
        }
        auto_frame_delay_update(&delay, missed, frame == 0 ? 0.0 : PERIOD, 12);
        if (delay.locked && locked_at < 0.0)
        {
            locked_at = frame * PERIOD;
        }
    }
    std::printf("auto delay, budget 7 ms from 12: locked at %u ms after %.1f s, %u misses\n", delay.delay_ms,
                locked_at, misses);
    check(delay.locked && delay.delay_ms == BUDGET_MS, "it locks exactly at the budget");
    check(delay.lowers == 12 - BUDGET_MS, "one lowering per ms above the budget (warm-up misses ignored)");
    check(misses_after_lock == 0, "no misses after locking");
    check(locked_at > 64.0 && locked_at < 67.0, "locks after the warm-up and a minute without misses");
}

static void test_auto_delay_warmup_and_restore()
{
    // Stalls in the first seconds (a game starting) don't lower the delay.
    AutoFrameDelay delay = {};
    auto_frame_delay_update(&delay, false, 0.0, 12);
    for (int frame = 0; frame < 60 * 4; frame++)
    {
        auto_frame_delay_update(&delay, frame % 20 == 0, PERIOD, 12);
    }
    check(delay.delay_ms == 12, "misses during the warm-up are ignored");
    for (int frame = 0; frame < 60 * 2; frame++)
    {
        auto_frame_delay_update(&delay, false, PERIOD, 12);
    }
    auto_frame_delay_update(&delay, true, PERIOD, 12);
    check(delay.delay_ms == 11, "after the warm-up a miss lowers it");

    auto_frame_delay_restore(&delay, 9);
    check(delay.started && delay.locked && delay.delay_ms == 9, "a restored value is locked at once");
    auto_frame_delay_update(&delay, true, PERIOD, 12);
    check(delay.delay_ms == 9, "and stays");
}

static void test_auto_delay_minute_must_be_uninterrupted()
{
    AutoFrameDelay delay = {};
    auto_frame_delay_update(&delay, false, 0.0, 12);
    for (int round = 0; round < 5; round++)
    {
        for (int frame = 0; frame < 60 * 50; frame++)   // 50 s of play...
        {
            auto_frame_delay_update(&delay, false, PERIOD, 12);
        }
        auto_frame_delay_interrupt(&delay);             // ...then the menu opens
    }
    check(!delay.locked && delay.delay_ms == 12, "interrupted minutes never lock, the delay stays");

    for (int frame = 0; frame < 60 * 61; frame++)
    {
        auto_frame_delay_update(&delay, false, PERIOD, 12);
    }
    check(delay.locked, "a full minute locks");
    auto_frame_delay_update(&delay, true, PERIOD, 12);
    auto_frame_delay_interrupt(&delay);
    check(delay.locked && delay.delay_ms == 12, "locked: misses and interruptions change nothing");
    check(auto_frame_delay_seconds_to_lock(&delay) == 0.0, "nothing left to lock");

    auto_frame_delay_reset(&delay);
    auto_frame_delay_update(&delay, true, 0.0, 12);
    check(!delay.locked && delay.delay_ms == 12, "a new game starts from the top (a miss before it counts for nothing)");
    for (int frame = 0; frame < 60 * 6; frame++)   // past the warm-up
    {
        auto_frame_delay_update(&delay, false, PERIOD, 12);
    }
    for (int i = 0; i < 20; i++)
    {
        auto_frame_delay_update(&delay, true, PERIOD, 12);
    }
    check(delay.delay_ms == 0, "never below 0");
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

    // A refresh lost spread over loops (1.0, 1.4, 1.0, 1.4...: no loop reaches
    // 1.5 periods, but the core frames fall 0.2 periods behind per loop):
    // counted as missed, and caught up.
    uint64_t missed_before = timing.missed_total;
    double spread[100];
    for (int i = 0; i < 100; i++)
    {
        spread[i] = (i % 2 == 0) ? 1.0 : 1.4;
    }
    extra = run_loops(&timing, spread, 100, &now);
    uint64_t spread_missed = timing.missed_total - missed_before;
    std::printf("catch-up: loops of 1.0 / 1.4 periods for 100 loops -> %llu missed, %u extra frames\n",
                static_cast<unsigned long long>(spread_missed), extra);
    check(spread_missed >= 15 && spread_missed <= 21, "a refresh lost over several loops is counted as missed");
    check(extra == spread_missed, "and each one is caught up");

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
    test_auto_delay_comes_down_and_locks();
    test_auto_delay_minute_must_be_uninterrupted();
    test_auto_delay_warmup_and_restore();
    test_catch_up();
    if (g_failures == 0)
    {
        std::printf("frame delay: all checks passed\n");
        return 0;
    }
    std::printf("frame delay: %d check(s) failed\n", g_failures);
    return 1;
}
