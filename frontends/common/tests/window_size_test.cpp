// Checks the window size math (window_size.h, viewport_picture_size) with
// hand-worked cases: fixed presets on a 1920x1080 display with a 1050-line
// usable area, and SNES / GBA pictures in dynamic mode.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>

#include "window_size.h"

static int g_failures = 0;

static void check_size(bool ok, uint32_t width, uint32_t height, uint32_t expected_width, uint32_t expected_height,
                       const char* what)
{
    if (!ok || width != expected_width || height != expected_height)
    {
        std::printf("FAIL %s: expected %ux%u, got %ux%u%s\n", what, expected_width, expected_height, width, height,
                    ok ? "" : " (returned false)");
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

static void test_fixed()
{
    uint32_t width = 0;
    uint32_t height = 0;
    bool ok = window_size_fixed(16.0 / 9.0, 720, 1920, 1050, &width, &height);
    check_size(ok, width, height, 1280, 720, "16:9 720 fits");
    ok = window_size_fixed(16.0 / 10.0, 900, 1920, 1050, &width, &height);
    check_size(ok, width, height, 1440, 900, "16:10 900 fits");
    ok = window_size_fixed(4.0 / 3.0, 720, 1920, 1050, &width, &height);
    check_size(ok, width, height, 960, 720, "4:3 720 fits");
    // 1920x1080 on 1050 usable lines: shrinks by 1050/1080, keeping 16:9.
    ok = window_size_fixed(16.0 / 9.0, 1080, 1920, 1050, &width, &height);
    check_size(ok, width, height, 1866, 1050, "16:9 1080 shrinks to fit the height");
    // 2560x1440 too: limited by height again.
    ok = window_size_fixed(16.0 / 9.0, 1440, 1920, 1050, &width, &height);
    check_size(ok, width, height, 1866, 1050, "16:9 1440 shrinks to the same size");
    // A narrow usable area limits by width instead.
    ok = window_size_fixed(16.0 / 9.0, 1080, 1280, 1050, &width, &height);
    check_size(ok, width, height, 1280, 720, "narrow area: limited by width");
    check(!window_size_fixed(0.0, 720, 1920, 1050, &width, &height), "no aspect: false");
}

static ViewportInput snes()
{
    ViewportInput input = {};
    input.frame_width = 256;
    input.frame_height = 224;
    input.core_aspect = 4.0 / 3.0;
    return input;
}

static void test_dynamic()
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t used = 0;

    ViewportInput input = snes();
    bool ok = window_size_dynamic(&input, 3, 1920, 1050, &width, &height, &used);
    check_size(ok, width, height, 896, 672, "SNES 4:3 x3");
    check(used == 3, "x3 used");

    // x5 is 1120 lines: too tall for 1050, so x4.
    ok = window_size_dynamic(&input, 5, 1920, 1050, &width, &height, &used);
    check_size(ok, width, height, 1195, 896, "SNES 4:3 x5 lowered to x4");
    check(used == 4, "x4 used");

    // 8 + 8 lines cropped: 208 lines, the pixels keep their shape.
    input.crop.top = 8;
    input.crop.bottom = 8;
    ok = window_size_dynamic(&input, 3, 1920, 1050, &width, &height, &used);
    check_size(ok, width, height, 896, 624, "SNES cropped x3");

    input = snes();
    input.aspect = ViewportAspect::square_pixels;
    ok = window_size_dynamic(&input, 2, 1920, 1050, &width, &height, &used);
    check_size(ok, width, height, 512, 448, "SNES square pixels x2");

    input.aspect = ViewportAspect::stretch;
    ok = window_size_dynamic(&input, 2, 1920, 1050, &width, &height, &used);
    check_size(ok, width, height, 597, 448, "stretch has no window here: core aspect");

    ViewportInput gba = {};
    gba.frame_width = 240;
    gba.frame_height = 160;
    gba.core_aspect = 1.5;
    ok = window_size_dynamic(&gba, 10, 1920, 1050, &width, &height, &used);
    check_size(ok, width, height, 1440, 960, "GBA x10 lowered to x6");
    check(used == 6, "x6 used");

    // Nothing fits: still x1.
    ok = window_size_dynamic(&gba, 4, 200, 100, &width, &height, &used);
    check_size(ok, width, height, 240, 160, "tiny area: x1 at least");
    check(used == 1, "x1 used");

    ViewportInput empty = {};
    check(!window_size_dynamic(&empty, 3, 1920, 1050, &width, &height, &used), "no frame: false");
    check(!window_size_dynamic(&gba, 0, 1920, 1050, &width, &height, &used), "scale 0: false");
}

int main()
{
    test_fixed();
    test_dynamic();
    if (g_failures == 0)
    {
        std::printf("window size: all checks passed\n");
        return 0;
    }
    std::printf("window size: %d check(s) failed\n", g_failures);
    return 1;
}
