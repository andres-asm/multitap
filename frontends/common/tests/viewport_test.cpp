// Checks the viewport math (viewport.h) with real systems and windows:
// SNES 256x224 (4:3 and 8:7), GBA 240x160, on 1920x1080 and 1280x800.
// Every expected number below was worked out by hand from the rules in
// viewport.h.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>

#include "viewport.h"

static int g_failures = 0;

struct Expected
{
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t integer_scale;
};

static void check_rect(const ViewportInput* input, Expected expected, const char* what)
{
    ViewportResult result = {};
    bool ok = viewport_compute(input, &result);
    bool same = ok && result.x == expected.x && result.y == expected.y && result.width == expected.width &&
                result.height == expected.height && result.integer_scale == expected.integer_scale;
    if (!same)
    {
        std::printf("FAIL %s: expected %dx%d at (%d,%d) x%u, got %ux%u at (%d,%d) x%u%s\n", what, expected.width,
                    expected.height, expected.x, expected.y, expected.integer_scale, result.width, result.height,
                    result.x, result.y, result.integer_scale, ok ? "" : " (returned false)");
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

static ViewportInput snes(uint32_t window_width, uint32_t window_height)
{
    ViewportInput input = {};
    input.frame_width = 256;
    input.frame_height = 224;
    input.core_aspect = 4.0 / 3.0;
    input.window_width = window_width;
    input.window_height = window_height;
    return input;
}

static ViewportInput gba(uint32_t window_width, uint32_t window_height)
{
    ViewportInput input = {};
    input.frame_width = 240;
    input.frame_height = 160;
    input.core_aspect = 3.0 / 2.0;
    input.window_width = window_width;
    input.window_height = window_height;
    return input;
}

static void test_snes_4_3()
{
    ViewportInput input = snes(1920, 1080);
    input.scaling = ViewportScaling::full;
    check_rect(&input, {240, 0, 1440, 1080, 0}, "SNES 4:3 full 1080p");

    // fit = min(1920 / 298.67, 1080 / 224) = 4.82
    input.scaling = ViewportScaling::integer_under;
    check_rect(&input, {362, 92, 1195, 896, 4}, "SNES 4:3 integer under 1080p");
    input.scaling = ViewportScaling::integer_over;
    check_rect(&input, {213, -20, 1493, 1120, 5}, "SNES 4:3 integer over 1080p");

    // x5 is 1120 lines on 1080: 3.7% past the window.
    input.scaling = ViewportScaling::automatic;
    input.max_offscreen_percent = 5.0;
    check_rect(&input, {213, -20, 1493, 1120, 5}, "SNES 4:3 auto 5% 1080p: x5 allowed");
    input.max_offscreen_percent = 3.0;
    check_rect(&input, {362, 92, 1195, 896, 4}, "SNES 4:3 auto 3% 1080p: x5 too far");

    ViewportResult result = {};
    input.scaling = ViewportScaling::integer_under;
    input.filter = ViewportFilter::sharp_bilinear;
    viewport_compute(&input, &result);
    check(result.filter == ViewportFilter::sharp_bilinear && result.prescale_x == 4 && result.prescale_y == 4,
          "sharp bilinear prescale x4 (1195 / 256 = 4.67, 896 / 224 = 4)");
    input.scaling = ViewportScaling::full;
    viewport_compute(&input, &result);
    check(result.prescale_x == 5 && result.prescale_y == 4, "sharp bilinear prescale at full (5.6, 4.8)");
}

static void test_snes_8_7()
{
    ViewportInput input = snes(1280, 800);
    input.aspect = ViewportAspect::fixed;
    input.fixed_aspect = 8.0 / 7.0;

    // 8:7 on 256x224 is square pixels: x3 = 768x672.
    input.scaling = ViewportScaling::integer_under;
    check_rect(&input, {256, 64, 768, 672, 3}, "SNES 8:7 integer under 1280x800");
    // x4 would be 896 lines on 800: 12% past.
    input.scaling = ViewportScaling::automatic;
    check_rect(&input, {256, 64, 768, 672, 3}, "SNES 8:7 auto 1280x800 stays x3");
    input.scaling = ViewportScaling::full;
    check_rect(&input, {183, 0, 914, 800, 0}, "SNES 8:7 full 1280x800");

    input = snes(1920, 1080);
    input.aspect = ViewportAspect::square_pixels;
    input.scaling = ViewportScaling::full;
    check_rect(&input, {343, 0, 1234, 1080, 0}, "SNES square pixels full 1080p");

    input.aspect = ViewportAspect::core;
    input.core_aspect = 0.0;
    check_rect(&input, {343, 0, 1234, 1080, 0}, "core gives no aspect: frame w/h");
}

static void test_gba()
{
    ViewportInput input = gba(1920, 1080);
    input.scaling = ViewportScaling::integer_under;
    check_rect(&input, {240, 60, 1440, 960, 6}, "GBA integer under 1080p");
    input.scaling = ViewportScaling::automatic;
    check_rect(&input, {120, -20, 1680, 1120, 7}, "GBA auto 1080p: x7 is 3.7% past");

    input = gba(1280, 800);
    input.scaling = ViewportScaling::full;
    check_rect(&input, {40, 0, 1200, 800, 0}, "GBA full 1280x800");
    input.scaling = ViewportScaling::automatic;
    check_rect(&input, {40, 0, 1200, 800, 5}, "GBA auto 1280x800: fits exactly at x5");
    input.scaling = ViewportScaling::integer_over;
    check_rect(&input, {40, 0, 1200, 800, 5}, "GBA integer over 1280x800: exact fit stays x5");

    // A window smaller than the picture at x1: still x1, past the edges.
    input = gba(200, 100);
    input.scaling = ViewportScaling::integer_under;
    check_rect(&input, {-20, -30, 240, 160, 1}, "GBA in a tiny window: x1 at least");
    input.scaling = ViewportScaling::automatic;
    check_rect(&input, {-20, -30, 240, 160, 1}, "GBA auto in a tiny window: x1 at least");
}

static void test_stretch()
{
    ViewportInput input = snes(1280, 800);
    input.aspect = ViewportAspect::stretch;
    input.scaling = ViewportScaling::full;
    check_rect(&input, {0, 0, 1280, 800, 0}, "stretch full fills the window");
    input.scaling = ViewportScaling::integer_under;
    check_rect(&input, {102, 64, 1075, 672, 3}, "stretch integer under: window aspect, x3 height");
}

static void test_anchors()
{
    ViewportInput input = gba(1920, 1080);
    input.scaling = ViewportScaling::integer_under;
    input.anchor = ViewportAnchor::top_left;
    check_rect(&input, {0, 0, 1440, 960, 6}, "anchor top left");
    input.anchor = ViewportAnchor::top;
    check_rect(&input, {240, 0, 1440, 960, 6}, "anchor top");
    input.anchor = ViewportAnchor::right;
    check_rect(&input, {480, 60, 1440, 960, 6}, "anchor right");
    input.anchor = ViewportAnchor::bottom_right;
    check_rect(&input, {480, 120, 1440, 960, 6}, "anchor bottom right");
    input.anchor = ViewportAnchor::bottom_left;
    check_rect(&input, {0, 120, 1440, 960, 6}, "anchor bottom left");

    input.scaling = ViewportScaling::automatic;
    input.anchor = ViewportAnchor::bottom;
    check_rect(&input, {120, -40, 1680, 1120, 7}, "anchor bottom with a picture taller than the window");
}

static void test_crop()
{
    // 8 lines off the top and bottom of a SNES frame: 256x208 shown, the
    // pixels keep their shape (the aspect is for the whole frame).
    ViewportInput input = snes(1920, 1080);
    input.crop.top = 8;
    input.crop.bottom = 8;
    input.scaling = ViewportScaling::integer_under;
    check_rect(&input, {213, 20, 1493, 1040, 5}, "SNES crop 8+8 integer under 1080p");
    ViewportResult result = {};
    viewport_compute(&input, &result);
    check(result.source_x == 0 && result.source_y == 8 && result.source_width == 256 && result.source_height == 208,
          "crop source rectangle");

    // A crop that leaves nothing on an axis is ignored on that axis only.
    input.crop.left = 200;
    input.crop.right = 100;
    viewport_compute(&input, &result);
    check(result.source_x == 0 && result.source_width == 256 && result.source_y == 8 && result.source_height == 208,
          "impossible horizontal crop ignored, vertical kept");

    input.crop = ViewportCrop{};
    input.crop.top = 0xFFFFFFFFu;
    input.crop.bottom = 2;
    viewport_compute(&input, &result);
    check(result.source_y == 0 && result.source_height == 224, "huge crop doesn't wrap around");
}

static void test_bad_input()
{
    ViewportInput input = snes(0, 1080);
    ViewportResult result = {};
    check(!viewport_compute(&input, &result) && result.width == 0, "no window size: false");
    input = snes(1920, 1080);
    input.frame_height = 0;
    check(!viewport_compute(&input, &result), "no frame size: false");
}

int main()
{
    test_snes_4_3();
    test_snes_8_7();
    test_gba();
    test_stretch();
    test_anchors();
    test_crop();
    test_bad_input();

    if (g_failures == 0)
    {
        std::printf("viewport: all checks passed\n");
        return 0;
    }
    std::printf("viewport: %d check(s) failed\n", g_failures);
    return 1;
}
