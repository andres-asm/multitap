#pragma once

// The core's latest video frame, kept by the host.
//
// libretro only guarantees the pointer passed to video_refresh is valid DURING
// the callback (invader kept it and broke on duplicate frames). So the host
// copies every frame into its own buffer, converted to one format: XRGB8888
// with the X byte set to 0xFF. In memory that is B, G, R, 0xFF per pixel, which
// a frontend can upload directly as a B8G8R8A8 texture.

#include <cstddef>
#include <cstdint>

#include "libretro.h"

struct LibretroVideoFrame
{
    uint32_t* pixels          = nullptr;   // width * height pixels, tightly packed (pitch = width * 4)
    size_t    capacity_pixels = 0;         // allocated size of `pixels`
    unsigned  width           = 0;
    unsigned  height          = 0;

    uint64_t  serial          = 0;         // +1 on every new frame (not on duplicates)
    uint64_t  duplicates      = 0;         // frames where the core passed NULL ("show the last one again")
};

// Converts one core frame into `frame` (growing the buffer if needed).
// source_pitch is in bytes, as the core passed it. Returns false only on a
// bad format or an allocation failure.
bool libretro_video_store_frame(LibretroVideoFrame* frame, const void* source, unsigned width, unsigned height,
                                size_t source_pitch, retro_pixel_format format);

void libretro_video_free(LibretroVideoFrame* frame);
