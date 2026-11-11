#include "libretro_video.h"

#include <cstring>
#include <new>

// Expands an N-bit channel to 8 bits by repeating its top bits, so full
// intensity maps to 255 (not 248) and black stays 0.
static uint32_t expand_5_bits(uint32_t value)
{
    return (value << 3) | (value >> 2);
}

static uint32_t expand_6_bits(uint32_t value)
{
    return (value << 2) | (value >> 4);
}

static constexpr uint32_t ALPHA_OPAQUE = 0xFF000000u;

static void convert_row_xrgb8888(uint32_t* destination, const uint8_t* source, unsigned width)
{
    for (unsigned x = 0; x < width; x++)
    {
        uint32_t pixel = 0;
        std::memcpy(&pixel, source + x * 4u, sizeof(pixel));   // memcpy: the core's rows need not be 4-byte aligned
        destination[x] = pixel | ALPHA_OPAQUE;
    }
}

static void convert_row_rgb565(uint32_t* destination, const uint8_t* source, unsigned width)
{
    for (unsigned x = 0; x < width; x++)
    {
        uint16_t pixel = 0;
        std::memcpy(&pixel, source + x * 2u, sizeof(pixel));
        uint32_t red   = expand_5_bits((pixel >> 11) & 0x1Fu);
        uint32_t green = expand_6_bits((pixel >> 5) & 0x3Fu);
        uint32_t blue  = expand_5_bits(pixel & 0x1Fu);
        destination[x] = ALPHA_OPAQUE | (red << 16) | (green << 8) | blue;
    }
}

static void convert_row_0rgb1555(uint32_t* destination, const uint8_t* source, unsigned width)
{
    for (unsigned x = 0; x < width; x++)
    {
        uint16_t pixel = 0;
        std::memcpy(&pixel, source + x * 2u, sizeof(pixel));
        uint32_t red   = expand_5_bits((pixel >> 10) & 0x1Fu);
        uint32_t green = expand_5_bits((pixel >> 5) & 0x1Fu);
        uint32_t blue  = expand_5_bits(pixel & 0x1Fu);
        destination[x] = ALPHA_OPAQUE | (red << 16) | (green << 8) | blue;
    }
}

bool libretro_video_store_frame(LibretroVideoFrame* frame, const void* source, unsigned width, unsigned height,
                                size_t source_pitch, retro_pixel_format format)
{
    if (format != RETRO_PIXEL_FORMAT_XRGB8888 && format != RETRO_PIXEL_FORMAT_RGB565 &&
        format != RETRO_PIXEL_FORMAT_0RGB1555)
    {
        return false;
    }

    // A row in the core's buffer can't be shorter than the pixels it holds; reading
    // with such a pitch would run past the end of the core's buffer.
    size_t bytes_per_pixel = format == RETRO_PIXEL_FORMAT_XRGB8888 ? 4u : 2u;
    if (width == 0 || height == 0 || source_pitch < static_cast<size_t>(width) * bytes_per_pixel)
    {
        return false;
    }

    size_t needed = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (needed > frame->capacity_pixels)
    {
        uint32_t* bigger = new (std::nothrow) uint32_t[needed];
        if (bigger == nullptr)
        {
            return false;
        }
        delete[] frame->pixels;
        frame->pixels = bigger;
        frame->capacity_pixels = needed;
    }

    const uint8_t* source_bytes = static_cast<const uint8_t*>(source);
    for (unsigned y = 0; y < height; y++)
    {
        const uint8_t* source_row = source_bytes + static_cast<size_t>(y) * source_pitch;
        uint32_t* destination_row = frame->pixels + static_cast<size_t>(y) * width;

        switch (format)
        {
            case RETRO_PIXEL_FORMAT_XRGB8888: convert_row_xrgb8888(destination_row, source_row, width); break;
            case RETRO_PIXEL_FORMAT_RGB565:   convert_row_rgb565(destination_row, source_row, width);   break;
            case RETRO_PIXEL_FORMAT_0RGB1555: convert_row_0rgb1555(destination_row, source_row, width); break;
            default: break;
        }
    }

    frame->width = width;
    frame->height = height;
    frame->serial++;
    return true;
}

void libretro_video_free(LibretroVideoFrame* frame)
{
    delete[] frame->pixels;
    *frame = LibretroVideoFrame{};
}
