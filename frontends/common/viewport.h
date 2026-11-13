#pragma once

// Where the core's picture goes in the window: pure math, no SDL or Vulkan.
// Takes the core's frame size and aspect, the window size and the Picture
// settings (aspect, scaling, position, crop, filter) and gives the
// destination rectangle, the part of the frame to show, and what the
// renderer needs for the filter. Called every frame: no allocation.
//
// Rules:
//   - Crop is in core pixels and is applied first. The aspect describes the
//     WHOLE frame, so cropping removes picture without distorting it (the
//     pixel aspect ratio stays the same). A crop that would leave no pixels
//     on an axis is ignored on that axis.
//   - Aspect: core = the core's aspect (or frame w/h when it gives none);
//     square_pixels = frame w/h (1:1 pixels); fixed = `fixed_aspect`
//     (4:3, 16:9, 8:7...); stretch = the window's aspect.
//   - Scaling: s = the largest factor at which the picture fits the window.
//       full           s as is (fills one axis).
//       integer_under  n = floor(s): the largest whole multiple that fits.
//       integer_over   n = ceil(s): the smallest one that covers the window
//                      on the limiting axis (overflows the other way).
//       automatic      n = floor(s), or n + 1 if the picture at n + 1 goes
//                      past the window by at most `max_offscreen_percent`
//                      (on the worse axis). A picture that fits exactly at
//                      n stays at n.
//     Integer factors are always at least 1 and apply to the HEIGHT; the
//     width follows the aspect (so it isn't a whole multiple when the
//     pixels aren't square).
//   - Position: one of 9 anchors. A picture bigger than the window goes past
//     its edges (the renderer clips).
//   - Sizes are rounded to whole pixels; centered positions round down.

#include <cstdint>

enum class ViewportAspect
{
    core,
    square_pixels,
    fixed,
    stretch,
};

enum class ViewportScaling
{
    full,
    integer_under,
    integer_over,
    automatic,
};

enum class ViewportAnchor
{
    top_left,
    top,
    top_right,
    left,
    center,
    right,
    bottom_left,
    bottom,
    bottom_right,
};

enum class ViewportFilter
{
    nearest,
    bilinear,
    sharp_bilinear,   // nearest prescale to the integer part, then bilinear
};

struct ViewportCrop
{
    uint32_t top = 0;
    uint32_t bottom = 0;
    uint32_t left = 0;
    uint32_t right = 0;
};

struct ViewportInput
{
    uint32_t frame_width = 0;     // the core's frame
    uint32_t frame_height = 0;
    double   core_aspect = 0.0;   // what the core says (<= 0: frame w/h, as libretro defines)
    uint32_t window_width = 0;    // drawable area, in pixels
    uint32_t window_height = 0;

    ViewportAspect  aspect = ViewportAspect::core;
    double          fixed_aspect = 4.0 / 3.0;   // for ViewportAspect::fixed
    ViewportScaling scaling = ViewportScaling::automatic;
    double          max_offscreen_percent = 5.0;   // for ViewportScaling::automatic
    ViewportAnchor  anchor = ViewportAnchor::center;
    ViewportCrop    crop = {};
    ViewportFilter  filter = ViewportFilter::nearest;
};

struct ViewportResult
{
    // Destination in window pixels. x/y may be negative and the rectangle may
    // go past the window when the picture is bigger.
    int32_t  x = 0;
    int32_t  y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    // The part of the frame shown (after crop), in core pixels.
    uint32_t source_x = 0;
    uint32_t source_y = 0;
    uint32_t source_width = 0;
    uint32_t source_height = 0;

    uint32_t integer_scale = 0;   // the whole factor used; 0 for full scaling

    // For the renderer. Sharp bilinear: scale the source up by prescale_x /
    // prescale_y with nearest (the whole part of the scale, at least 1), then
    // bilinear to the destination.
    ViewportFilter filter = ViewportFilter::nearest;
    uint32_t prescale_x = 1;
    uint32_t prescale_y = 1;
};

// False (result zeroed) if the frame or the window has no size.
bool viewport_compute(const ViewportInput* input, ViewportResult* result);

// The picture's own size at whole factor `factor` (height = factor x the
// cropped frame height, width from the aspect), with no window involved: for
// a window sized to the picture. The window fields are not used; `stretch`
// has no window to follow here, so it counts as the core's aspect.
// False if the frame has no size or factor is 0.
bool viewport_picture_size(const ViewportInput* input, uint32_t factor, uint32_t* width, uint32_t* height);
