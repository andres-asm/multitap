#include "frame_delay.h"

void auto_frame_delay_reset(AutoFrameDelay* delay)
{
    *delay = AutoFrameDelay{};
}

void auto_frame_delay_restore(AutoFrameDelay* delay, uint32_t delay_ms)
{
    *delay = AutoFrameDelay{};
    delay->started = true;
    delay->locked = true;
    delay->delay_ms = delay_ms;
}

uint32_t auto_frame_delay_start_ms(double display_hz)
{
    if (display_hz <= 0.0)
    {
        return 0;
    }
    return static_cast<uint32_t>(1000.0 / display_hz * FRAME_DELAY_START_FRACTION);
}

void auto_frame_delay_interrupt(AutoFrameDelay* delay)
{
    if (!delay->locked)
    {
        delay->stable_seconds = 0.0;
    }
}

uint32_t auto_frame_delay_update(AutoFrameDelay* delay, bool missed, double elapsed_seconds, uint32_t start_ms)
{
    if (!delay->started)
    {
        delay->started = true;
        delay->delay_ms = start_ms;
        delay->stable_seconds = 0.0;
        return delay->delay_ms;   // this loop's miss (if any) came before the delay applied
    }
    if (delay->locked)
    {
        return delay->delay_ms;
    }
    elapsed_seconds = elapsed_seconds < 0.0 ? 0.0 : (elapsed_seconds > 1.0 ? 1.0 : elapsed_seconds);
    if (delay->warmup_seconds < FRAME_DELAY_WARMUP_SECONDS)
    {
        delay->warmup_seconds += elapsed_seconds;   // the game's own start-up stalls: not the delay's
        return delay->delay_ms;
    }
    if (missed)
    {
        if (delay->delay_ms >= FRAME_DELAY_LOWER_MS)
        {
            delay->delay_ms -= FRAME_DELAY_LOWER_MS;
            delay->lowers++;
        }
        delay->stable_seconds = 0.0;
        return delay->delay_ms;
    }
    delay->stable_seconds += elapsed_seconds;
    if (delay->stable_seconds >= FRAME_DELAY_LOCK_SECONDS)
    {
        delay->locked = true;
    }
    return delay->delay_ms;
}

double auto_frame_delay_seconds_to_lock(const AutoFrameDelay* delay)
{
    if (delay->locked)
    {
        return 0.0;
    }
    double left = FRAME_DELAY_LOCK_SECONDS - delay->stable_seconds;
    return left > 0.0 ? left : 0.0;
}
