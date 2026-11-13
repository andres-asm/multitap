#include "rate_control.h"

// PI gains, in units of max_deviation. Proportional 1: a queue twice the
// target asks for the whole max_deviation, as before. Integral: corrects a
// steady error of the whole target in about 20 s, slow enough not to chase
// a single stall.
constexpr double PROPORTIONAL_GAIN = 1.0;
constexpr double INTEGRAL_GAIN     = 0.05;   // per second

static double clamp(double value, double low, double high)
{
    return value < low ? low : (value > high ? high : value);
}

void rate_control_init(RateControl* control, uint32_t target_ms, bool automatic, uint32_t floor_ms)
{
    double max_deviation = control->max_deviation;
    *control = RateControl{};
    control->max_deviation = max_deviation;
    control->automatic = automatic;
    control->floor_ms = floor_ms;
    double limit = static_cast<double>(RATE_CONTROL_MAX_TARGET_MS);
    control->target_ms = clamp(static_cast<double>(target_ms), static_cast<double>(floor_ms), limit);
}

void rate_control_reset(RateControl* control)
{
    control->integral = 0.0;
}

// Automatic target: up after an underrun, down after a long quiet stretch
// with room to spare.
static void adjust_target(RateControl* control, double queued_ms, double elapsed_seconds, bool underrun)
{
    double floor = static_cast<double>(control->floor_ms);
    double ceiling = static_cast<double>(RATE_CONTROL_MAX_TARGET_MS);
    control->since_raise += elapsed_seconds;
    if (underrun)
    {
        // Several dry pushes in a row are one stall: raise once for it.
        if (control->since_raise >= RATE_CONTROL_AUTO_RAISE_HOLD_SECONDS)
        {
            double raised = clamp(control->target_ms + RATE_CONTROL_AUTO_RAISE_MS, floor, ceiling);
            if (raised != control->target_ms)
            {
                control->raises++;
            }
            control->target_ms = raised;
            control->since_raise = 0.0;
        }
        control->quiet_seconds = 0.0;
        control->low_water_ms = 1e9;
        return;
    }

    control->quiet_seconds += elapsed_seconds;
    control->low_water_ms = queued_ms < control->low_water_ms ? queued_ms : control->low_water_ms;
    if (control->quiet_seconds < RATE_CONTROL_AUTO_QUIET_SECONDS)
    {
        return;
    }
    if (control->low_water_ms > RATE_CONTROL_AUTO_MARGIN_MS)
    {
        double lowered = clamp(control->target_ms - RATE_CONTROL_AUTO_LOWER_MS, floor, ceiling);
        if (lowered != control->target_ms)
        {
            control->lowers++;
        }
        control->target_ms = lowered;
    }
    control->quiet_seconds = 0.0;
    control->low_water_ms = 1e9;
}

double rate_control_update(RateControl* control, double queued_ms, double elapsed_seconds, bool underrun)
{
    elapsed_seconds = clamp(elapsed_seconds, 0.0, 1.0);   // a long pause isn't a reason to swing
    if (control->automatic)
    {
        adjust_target(control, queued_ms, elapsed_seconds, underrun);
    }

    // Smoothed reading: the raw one jumps by a device buffer at every take.
    if (control->smoothed_ms < 0.0)
    {
        control->smoothed_ms = queued_ms;
    }
    double weight = elapsed_seconds / (RATE_CONTROL_SMOOTHING_SECONDS + elapsed_seconds);
    control->smoothed_ms += weight * (queued_ms - control->smoothed_ms);

    // Error as a fraction of the target: +1 = twice the target, -1 = empty.
    double error = clamp((control->smoothed_ms - control->target_ms) / control->target_ms, -1.0, 1.0);
    double proportional = PROPORTIONAL_GAIN * error;
    // Integral: not on an underrun (a stall, not a speed error), and not while
    // the correction is already at its limit in the same direction (anti-windup).
    bool saturated = (proportional + control->integral >= 1.0 && error > 0.0) ||
                     (proportional + control->integral <= -1.0 && error < 0.0);
    if (!underrun && !saturated)
    {
        control->integral = clamp(control->integral + INTEGRAL_GAIN * error * elapsed_seconds, -1.0, 1.0);
    }
    double correction = clamp(proportional + control->integral, -1.0, 1.0);
    return 1.0 + control->max_deviation * correction;
}
