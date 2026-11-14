#include "frame_timing.h"

#include <SDL3/SDL.h>

constexpr uint64_t NANOSECONDS_PER_SECOND = 1000000000ull;

// More than this behind (a stall: window dragged, a breakpoint) and we
// restart the clock instead of running a burst of frames to catch up.
constexpr uint64_t EXACT_MAX_LATENESS_NS = 250000000ull;   // 250 ms

static uint64_t frame_period_ns(double fps)
{
    double clamped = fps > 1.0 ? fps : 1.0;
    return static_cast<uint64_t>(static_cast<double>(NANOSECONDS_PER_SECOND) / clamped);
}

void frame_timing_update(FrameTiming* timing, bool core_running, double core_fps, double display_hz, bool vsync_forced)
{
    timing->core_fps = core_running ? core_fps : 0.0;
    timing->display_hz = display_hz;

    TimingMode effective = timing->mode;
    timing->fell_back = false;
    if (!core_running || core_fps <= 0.0)
    {
        effective = TimingMode::vsync;
    }
    else if (timing->mode == TimingMode::vsync)
    {
        // Unknown display rate, or too far from the core's: vsync would run the
        // core at the wrong speed (DRC can only stretch audio by a little).
        double difference = display_hz > 0.0 ? (core_fps - display_hz) / display_hz : 1.0;
        difference = difference < 0.0 ? -difference : difference;
        if (difference > FRAME_TIMING_VSYNC_TOLERANCE)
        {
            effective = TimingMode::exact;
            timing->fell_back = true;
        }
    }

    if (effective == TimingMode::exact && timing->effective != TimingMode::exact)
    {
        timing->next_frame_ns = SDL_GetTicksNS();   // start pacing from now
    }
    timing->effective = effective;

    timing->fifo_forced = effective == TimingMode::exact && vsync_forced;
    if (!core_running)
    {
        timing->fifo_warned = false;   // warn again for the next core
    }
    else if (timing->fifo_forced && !timing->fifo_warned)
    {
        timing->fifo_warned = true;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Exact timing not available on this driver (FIFO only): presents wait for vsync, so a core "
                    "faster than the display (%.3f fps on %.2f Hz) runs at the display's speed",
                    core_fps, display_hz);
    }
}

// Each loop the count of real time behind moves toward 0 by this fraction:
// a steady error e per loop settles at e / LEAK (0.1% of a 60 Hz period:
// 1.7 ms, far below a period) instead of adding up.
constexpr double BEHIND_LEAK = 0.01;
// Behind by this much of a period runs an extra frame: a missed refresh adds
// a whole period, while ordinary jitter (loops of 0.6 then 1.4 periods)
// stays well below it.
constexpr double CATCH_UP_THRESHOLD = 0.9;

void frame_timing_measure(FrameTiming* timing, bool running_frames)
{
    frame_timing_measure_at(timing, running_frames, SDL_GetTicksNS());
}

void frame_timing_measure_at(FrameTiming* timing, bool running_frames, uint64_t now_ns)
{
    uint64_t last = timing->last_loop_ns;
    timing->missed = false;
    timing->catch_up_frames = 0;
    bool measuring = running_frames && timing->effective == TimingMode::vsync && timing->display_hz > 0.0;
    timing->last_loop_ns = measuring ? now_ns : 0;
    if (!measuring || last == 0 || now_ns <= last)
    {
        timing->behind_ns = 0.0;
        return;
    }

    double period = static_cast<double>(NANOSECONDS_PER_SECOND) / timing->display_hz;
    double interval = static_cast<double>(now_ns - last);

    // Lost refreshes: whole periods the core frames are behind. A refresh can
    // be lost in one long loop or spread over several slightly long ones
    // (1.0 + 1.4 + ...: seen with a frame delay just too long, where no single
    // loop reaches 1.5 periods), so the count of time behind decides, not
    // one loop's length. Counted once whether or not catch-up runs them.
    double behind = (timing->behind_ns + interval - period) * (1.0 - BEHIND_LEAK);
    double most = static_cast<double>(FRAME_TIMING_MAX_CATCH_UP + 1) * period;
    behind = behind < -2.0 * period ? -2.0 * period : (behind > most ? most : behind);
    uint32_t lost = 0;
    while (lost < FRAME_TIMING_MAX_CATCH_UP && behind >= CATCH_UP_THRESHOLD * period)
    {
        lost++;
        behind -= period;
    }
    timing->missed = lost > 0 || interval >= 1.5 * period;
    if (timing->missed)
    {
        timing->missed_total++;
    }
    timing->catch_up_frames = timing->catch_up ? lost : 0;
    if (behind > period)
    {
        behind = period;   // beyond what this loop can make up: dropped, not carried
    }
    timing->caught_up_total += timing->catch_up_frames;
    timing->behind_ns = behind;
}

bool frame_timing_frame_due(FrameTiming* timing)
{
    if (timing->effective == TimingMode::vsync)
    {
        return true;
    }
    uint64_t now = SDL_GetTicksNS();
    if (now < timing->next_frame_ns)
    {
        return false;
    }
    uint64_t period = frame_period_ns(timing->core_fps);
    // Advance from the deadline, not from "now", so small wake-up delays don't
    // add up: the average rate stays exactly the core's fps.
    timing->next_frame_ns += period;
    if (now > timing->next_frame_ns + EXACT_MAX_LATENESS_NS)
    {
        timing->next_frame_ns = now + period;
    }
    return true;
}

void frame_timing_wait(const FrameTiming* timing)
{
    if (timing->effective != TimingMode::exact)
    {
        return;
    }
    uint64_t now = SDL_GetTicksNS();
    if (timing->next_frame_ns > now)
    {
        // SDL_DelayPrecise spins for the last bit, so the wake-up is accurate.
        SDL_DelayPrecise(timing->next_frame_ns - now);
    }
}

double frame_timing_audio_ratio(const FrameTiming* timing)
{
    if (timing->effective != TimingMode::vsync || timing->core_fps <= 0.0 || timing->display_hz <= 0.0)
    {
        return 1.0;
    }
    return timing->display_hz / timing->core_fps;
}

const char* frame_timing_mode_name(TimingMode mode)
{
    switch (mode)
    {
        case TimingMode::vsync: return "Vsync";
        case TimingMode::exact: return "Exact";
        default:                return "?";
    }
}
