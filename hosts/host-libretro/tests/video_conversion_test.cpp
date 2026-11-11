// Checks libretro_video_store_frame(): every libretro pixel format must come
// out as XRGB8888 with alpha 0xFF, rows must be read with the core's pitch,
// and a pitch shorter than a row must be refused.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libretro_video.h"

static int g_failures = 0;

static void check_pixel(const LibretroVideoFrame* frame, unsigned x, unsigned y, uint32_t expected, const char* what)
{
    uint32_t actual = frame->pixels[y * frame->width + x];
    if (actual != expected)
    {
        std::printf("FAIL %s: pixel (%u,%u) = 0x%08x, expected 0x%08x\n", what, x, y, actual, expected);
        g_failures++;
    }
}

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

int main()
{
    LibretroVideoFrame frame = {};

    // --- XRGB8888: 2x2, pitch padded to 16 bytes, X byte garbage (must become 0xFF).
    {
        uint8_t buffer[16 * 2] = {};
        uint32_t row0[2] = { 0x00FF0000u, 0x1200FF00u };   // red, green with junk in X
        uint32_t row1[2] = { 0xAB0000FFu, 0x00000000u };   // blue with junk, black
        std::memcpy(buffer, row0, sizeof(row0));
        std::memcpy(buffer + 16, row1, sizeof(row1));

        check(libretro_video_store_frame(&frame, buffer, 2, 2, 16, RETRO_PIXEL_FORMAT_XRGB8888), "XRGB8888 store");
        check_pixel(&frame, 0, 0, 0xFFFF0000u, "XRGB8888 red");
        check_pixel(&frame, 1, 0, 0xFF00FF00u, "XRGB8888 green");
        check_pixel(&frame, 0, 1, 0xFF0000FFu, "XRGB8888 blue");
        check_pixel(&frame, 1, 1, 0xFF000000u, "XRGB8888 black");
        check(frame.serial == 1, "XRGB8888 serial");
    }

    // --- RGB565: full intensity must expand to 255, not 248/252.
    {
        uint8_t buffer[8 * 2] = {};
        uint16_t row0[2] = { 0xF800u, 0x07E0u };   // red, green
        uint16_t row1[2] = { 0x001Fu, 0xFFFFu };   // blue, white
        std::memcpy(buffer, row0, sizeof(row0));
        std::memcpy(buffer + 8, row1, sizeof(row1));

        check(libretro_video_store_frame(&frame, buffer, 2, 2, 8, RETRO_PIXEL_FORMAT_RGB565), "RGB565 store");
        check_pixel(&frame, 0, 0, 0xFFFF0000u, "RGB565 red");
        check_pixel(&frame, 1, 0, 0xFF00FF00u, "RGB565 green");
        check_pixel(&frame, 0, 1, 0xFF0000FFu, "RGB565 blue");
        check_pixel(&frame, 1, 1, 0xFFFFFFFFu, "RGB565 white");
    }

    // --- 0RGB1555: the top bit is unused and must be ignored.
    {
        uint8_t buffer[4 * 2] = {};
        uint16_t row0[2] = { 0x7C00u, 0x83E0u };   // red, green (with the unused top bit set)
        uint16_t row1[2] = { 0x001Fu, 0x7FFFu };   // blue, white
        std::memcpy(buffer, row0, sizeof(row0));
        std::memcpy(buffer + 4, row1, sizeof(row1));

        check(libretro_video_store_frame(&frame, buffer, 2, 2, 4, RETRO_PIXEL_FORMAT_0RGB1555), "0RGB1555 store");
        check_pixel(&frame, 0, 0, 0xFFFF0000u, "0RGB1555 red");
        check_pixel(&frame, 1, 0, 0xFF00FF00u, "0RGB1555 green");
        check_pixel(&frame, 0, 1, 0xFF0000FFu, "0RGB1555 blue");
        check_pixel(&frame, 1, 1, 0xFFFFFFFFu, "0RGB1555 white");
    }

    // --- Refusals: pitch shorter than a row, zero size, unknown format. Frame must stay as it was.
    {
        uint8_t buffer[64] = {};
        uint64_t serial_before = frame.serial;
        check(!libretro_video_store_frame(&frame, buffer, 4, 2, 8, RETRO_PIXEL_FORMAT_XRGB8888), "pitch too small refused");
        check(!libretro_video_store_frame(&frame, buffer, 0, 2, 8, RETRO_PIXEL_FORMAT_XRGB8888), "zero width refused");
        check(!libretro_video_store_frame(&frame, buffer, 2, 2, 8, static_cast<retro_pixel_format>(77)), "bad format refused");
        check(frame.serial == serial_before, "refused frames don't count");
        check_pixel(&frame, 1, 1, 0xFFFFFFFFu, "refused frames leave the old image");
    }

    // --- Growing: a bigger frame reallocates.
    {
        uint32_t big[8 * 8] = {};
        big[63] = 0x00123456u;
        check(libretro_video_store_frame(&frame, big, 8, 8, 32, RETRO_PIXEL_FORMAT_XRGB8888), "grow store");
        check(frame.capacity_pixels >= 64, "grow capacity");
        check_pixel(&frame, 7, 7, 0xFF123456u, "grow last pixel");
    }

    libretro_video_free(&frame);

    if (g_failures == 0)
    {
        std::printf("video conversion: all checks passed\n");
        return 0;
    }
    std::printf("video conversion: %d check(s) failed\n", g_failures);
    return 1;
}
