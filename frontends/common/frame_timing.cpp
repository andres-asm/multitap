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
