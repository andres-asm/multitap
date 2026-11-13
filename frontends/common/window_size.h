#pragma once

// Window sizes for the two window modes (plan-settings, Video: Display).
// Pure math: the caller gets the usable area of the window's display
// (SDL_GetDisplayUsableBounds, minus the window's own borders) and applies
// the result. Sizes are in pixels.
//
//   Fixed size  the window has its own aspect (16:9, 16:10, 4:3) and height
//               preset; width = height x aspect. If that doesn't fit the
//               usable area, it shrinks keeping its aspect.
//   Dynamic     the window is the picture: the core's frame (crop and aspect
//               from the Picture settings) at a whole factor. The factor is
//               lowered until it fits the usable area (at least x1).

#include <cstdint>

#include "viewport.h"

// False if aspect, height or the usable area is 0.
bool window_size_fixed(double aspect, uint32_t height, uint32_t usable_width, uint32_t usable_height,
                       uint32_t* width, uint32_t* out_height);

// `picture` describes the frame and Picture settings (window fields unused).
// `used_scale` gets the factor that fit. False if there's no frame or scale is 0.
bool window_size_dynamic(const ViewportInput* picture, uint32_t scale, uint32_t usable_width, uint32_t usable_height,
                         uint32_t* width, uint32_t* height, uint32_t* used_scale);
