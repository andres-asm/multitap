#include "window_size.h"

#include <cmath>

bool window_size_fixed(double aspect, uint32_t height, uint32_t usable_width, uint32_t usable_height,
                       uint32_t* width, uint32_t* out_height)
{
    *width = 0;
    *out_height = 0;
    if (aspect <= 0.0 || height == 0 || usable_width == 0 || usable_height == 0)
    {
        return false;
    }
    double wanted_height = static_cast<double>(height);
    double wanted_width = wanted_height * aspect;
    // Shrink to fit, keeping the aspect: the limiting axis decides.
    double shrink = std::fmin(static_cast<double>(usable_width) / wanted_width,
                              static_cast<double>(usable_height) / wanted_height);
    if (shrink < 1.0)
    {
        wanted_width *= shrink;
        wanted_height *= shrink;
    }
    // Round down (never past the usable area), with a little slack so 719.9999
    // from a division still counts as 720.
    long long rounded_width = std::llround(std::floor(wanted_width + 1e-6));
    long long rounded_height = std::llround(std::floor(wanted_height + 1e-6));
    *width = rounded_width < 1 ? 1u : static_cast<uint32_t>(rounded_width);
    *out_height = rounded_height < 1 ? 1u : static_cast<uint32_t>(rounded_height);
    return true;
}

bool window_size_dynamic(const ViewportInput* picture, uint32_t scale, uint32_t usable_width, uint32_t usable_height,
                         uint32_t* width, uint32_t* height, uint32_t* used_scale)
{
    *width = 0;
    *height = 0;
    *used_scale = 0;
    if (scale == 0)
    {
        return false;
    }
    for (uint32_t factor = scale; factor >= 1; factor--)
    {
        if (!viewport_picture_size(picture, factor, width, height))
        {
            return false;
        }
        if ((*width <= usable_width && *height <= usable_height) || factor == 1)
        {
            *used_scale = factor;
            return true;
        }
    }
    return false;
}
