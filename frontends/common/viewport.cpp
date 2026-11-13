#include "viewport.h"

#include <cmath>

// Scale factors come from divisions: 1080 / 216 may give 4.9999999. This
// much slack makes "fits exactly at n" count as exact.
constexpr double VIEWPORT_EPSILON = 1e-9;

static uint32_t round_to_pixels(double value)
{
    long long rounded = std::llround(value);
    return rounded < 1 ? 1u : static_cast<uint32_t>(rounded);
}

// Where along the free space the picture goes: 0 = start, 0.5 = middle, 1 = end.
static void anchor_factors(ViewportAnchor anchor, double* horizontal, double* vertical)
{
    switch (anchor)
    {
    case ViewportAnchor::top_left:
        *horizontal = 0.0;
        *vertical = 0.0;
        return;
    case ViewportAnchor::top:
        *horizontal = 0.5;
        *vertical = 0.0;
        return;
    case ViewportAnchor::top_right:
        *horizontal = 1.0;
        *vertical = 0.0;
        return;
    case ViewportAnchor::left:
        *horizontal = 0.0;
        *vertical = 0.5;
        return;
    case ViewportAnchor::center:
        *horizontal = 0.5;
        *vertical = 0.5;
        return;
    case ViewportAnchor::right:
        *horizontal = 1.0;
        *vertical = 0.5;
        return;
    case ViewportAnchor::bottom_left:
        *horizontal = 0.0;
        *vertical = 1.0;
        return;
    case ViewportAnchor::bottom:
        *horizontal = 0.5;
        *vertical = 1.0;
        return;
    case ViewportAnchor::bottom_right:
        *horizontal = 1.0;
        *vertical = 1.0;
        return;
    }
    *horizontal = 0.5;
    *vertical = 0.5;
}

// The whole factor for the integer scaling modes. `fit` = the largest factor
// at which the picture fits.
static uint32_t integer_factor(const ViewportInput* input, double fit, double width_at_1, double height_at_1)
{
    double window_width = static_cast<double>(input->window_width);
    double window_height = static_cast<double>(input->window_height);
    double below = std::floor(fit + VIEWPORT_EPSILON);
    bool exact = fit - below < VIEWPORT_EPSILON;

    double factor = below;
    switch (input->scaling)
    {
    case ViewportScaling::full:
    case ViewportScaling::integer_under:
        break;
    case ViewportScaling::integer_over:
        factor = exact ? below : below + 1.0;
        break;
    case ViewportScaling::automatic:
        if (!exact && below >= 1.0)
        {
            // How far the next size up goes past the window, on the worse axis.
            double next = below + 1.0;
            double overflow = std::fmax(next * width_at_1 / window_width, next * height_at_1 / window_height) - 1.0;
            if (overflow * 100.0 <= input->max_offscreen_percent + VIEWPORT_EPSILON)
            {
                factor = next;
            }
        }
        break;
    }
    return factor < 1.0 ? 1u : static_cast<uint32_t>(factor);
}

// The shown part of the frame (crop applied, in result->source_*) and its
// displayed aspect. `window_aspect` is used for stretch (<= 0: no window,
// stretch counts as the core's aspect).
static double source_and_aspect(const ViewportInput* input, double window_aspect, ViewportResult* result)
{
    // Crop first (in 64 bits: two huge crops must not wrap around).
    uint64_t crop_horizontal = static_cast<uint64_t>(input->crop.left) + input->crop.right;
    uint64_t crop_vertical = static_cast<uint64_t>(input->crop.top) + input->crop.bottom;
    bool crop_x = crop_horizontal < input->frame_width;
    bool crop_y = crop_vertical < input->frame_height;
    result->source_x = crop_x ? input->crop.left : 0;
    result->source_y = crop_y ? input->crop.top : 0;
    result->source_width = crop_x ? input->frame_width - static_cast<uint32_t>(crop_horizontal) : input->frame_width;
    result->source_height = crop_y ? input->frame_height - static_cast<uint32_t>(crop_vertical) : input->frame_height;

    if (input->aspect == ViewportAspect::stretch && window_aspect > 0.0)
    {
        return window_aspect;
    }
    // The aspect setting describes the whole frame; the pixel aspect ratio
    // carries it over to the cropped part.
    double frame_width = static_cast<double>(input->frame_width);
    double frame_height = static_cast<double>(input->frame_height);
    double frame_aspect = frame_width / frame_height;
    bool use_core = input->aspect == ViewportAspect::core || input->aspect == ViewportAspect::stretch;
    if (use_core && input->core_aspect > 0.0)
    {
        frame_aspect = input->core_aspect;
    }
    else if (input->aspect == ViewportAspect::fixed && input->fixed_aspect > 0.0)
    {
        frame_aspect = input->fixed_aspect;
    }
    double pixel_aspect = frame_aspect * frame_height / frame_width;
    return pixel_aspect * static_cast<double>(result->source_width) / static_cast<double>(result->source_height);
}

bool viewport_compute(const ViewportInput* input, ViewportResult* result)
{
    *result = ViewportResult{};
    if (input->frame_width == 0 || input->frame_height == 0 || input->window_width == 0 || input->window_height == 0)
    {
        return false;
    }

    double window_width = static_cast<double>(input->window_width);
    double window_height = static_cast<double>(input->window_height);
    double aspect = source_and_aspect(input, window_width / window_height, result);
    double source_width = static_cast<double>(result->source_width);
    double source_height = static_cast<double>(result->source_height);

    // Size at factor 1 (height = the source's lines) and the factor that fits.
    double width_at_1 = source_height * aspect;
    double height_at_1 = source_height;
    double fit = std::fmin(window_width / width_at_1, window_height / height_at_1);

    if (input->scaling == ViewportScaling::full)
    {
        // Fill the limiting axis exactly; the other one follows the aspect.
        if (window_width / width_at_1 < window_height / height_at_1)
        {
            result->width = input->window_width;
            result->height = round_to_pixels(window_width / aspect);
        }
        else
        {
            result->height = input->window_height;
            result->width = round_to_pixels(window_height * aspect);
        }
    }
    else
    {
        uint32_t factor = integer_factor(input, fit, width_at_1, height_at_1);
        result->integer_scale = factor;
        result->height = factor * result->source_height;
        result->width = round_to_pixels(static_cast<double>(result->height) * aspect);
    }

    double horizontal = 0.5;
    double vertical = 0.5;
    anchor_factors(input->anchor, &horizontal, &vertical);
    double free_width = window_width - static_cast<double>(result->width);
    double free_height = window_height - static_cast<double>(result->height);
    result->x = static_cast<int32_t>(std::floor(free_width * horizontal));
    result->y = static_cast<int32_t>(std::floor(free_height * vertical));

    result->filter = input->filter;
    double prescale_x = std::floor(static_cast<double>(result->width) / source_width + VIEWPORT_EPSILON);
    double prescale_y = std::floor(static_cast<double>(result->height) / source_height + VIEWPORT_EPSILON);
    result->prescale_x = prescale_x < 1.0 ? 1u : static_cast<uint32_t>(prescale_x);
    result->prescale_y = prescale_y < 1.0 ? 1u : static_cast<uint32_t>(prescale_y);
    return true;
}

bool viewport_picture_size(const ViewportInput* input, uint32_t factor, uint32_t* width, uint32_t* height)
{
    *width = 0;
    *height = 0;
    if (input->frame_width == 0 || input->frame_height == 0 || factor == 0)
    {
        return false;
    }
    ViewportResult source = {};
    double aspect = source_and_aspect(input, 0.0, &source);
    *height = factor * source.source_height;
    *width = round_to_pixels(static_cast<double>(*height) * aspect);
    return true;
}
