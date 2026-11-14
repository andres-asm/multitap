#include "frame_delay.h"

void auto_frame_delay_reset(AutoFrameDelay* delay)
{
    *delay = AutoFrameDelay{};
}

uint32_t auto_frame_delay_update(AutoFrameDelay* delay, bool missed, double elapsed_seconds, uint32_t max_ms)
{
    elapsed_seconds = elapsed_seconds < 0.0 ? 0.0 : (elapsed_seconds > 1.0 ? 1.0 : elapsed_seconds);
    delay->since_raise += elapsed_seconds;
    if (missed)
    {
        // Right after a raise: the raise found the limit, wait longer before
        // probing it again. Otherwise (a stall) start the waits over.
        if (delay->since_raise < FRAME_DELAY_RAISE_BLAME_SECONDS)
        {
            delay->hold_seconds = delay->next_hold;
            delay->next_hold = delay->next_hold * 2.0 < FRAME_DELAY_MAX_HOLD_SECONDS ? delay->next_hold * 2.0
                                                                                     : FRAME_DELAY_MAX_HOLD_SECONDS;
        }
        else
        {
            delay->hold_seconds = FRAME_DELAY_HOLD_SECONDS;
            delay->next_hold = FRAME_DELAY_HOLD_SECONDS;
        }
        uint32_t lowered = delay->delay_ms > FRAME_DELAY_LOWER_MS ? delay->delay_ms - FRAME_DELAY_LOWER_MS : 0;
        if (lowered != delay->delay_ms)
        {
            delay->lowers++;
        }
        delay->delay_ms = lowered;
        delay->quiet_seconds = 0.0;
    }
    else
    {
        delay->quiet_seconds += elapsed_seconds;
        delay->hold_seconds = delay->hold_seconds > elapsed_seconds ? delay->hold_seconds - elapsed_seconds : 0.0;
        if (delay->hold_seconds <= 0.0 && delay->quiet_seconds >= FRAME_DELAY_RAISE_AFTER_SECONDS &&
            delay->delay_ms < max_ms)
        {
            delay->delay_ms++;
            delay->raises++;
            delay->quiet_seconds = 0.0;
            delay->since_raise = 0.0;
        }
    }
    if (delay->delay_ms > max_ms)
    {
        delay->delay_ms = max_ms;   // the maximum was lowered
    }
    return delay->delay_ms;
}
