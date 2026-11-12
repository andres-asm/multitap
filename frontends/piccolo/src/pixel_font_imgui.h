#pragma once

// piccolo's pixel font (pixel_font.h) as a Dear ImGui font.
//
// Plugged in as a custom ImFontLoader: whenever ImGui needs a glyph at some
// size, we draw it from the 5x5 table with square blocks of `size / 7` pixels.
// So the font is crisp at every whole-number scale; use it at sizes that are
// multiples of PIXEL_FONT_IMGUI_UNIT (7 pixels per line at 1x).
//
// Call after ImGui is initialized and after pixel_font_init().

struct ImFont;

constexpr float PIXEL_FONT_IMGUI_UNIT = 7.0f;   // font size at 1x (= PIXEL_FONT_LINE_HEIGHT)

// UTF-8 for the Unicode arrows the font draws with its own arrow glyphs.
#define PIXEL_FONT_UTF8_ARROW_UP    "\xE2\x96\xB2"   // U+25B2
#define PIXEL_FONT_UTF8_ARROW_DOWN  "\xE2\x96\xBC"   // U+25BC
#define PIXEL_FONT_UTF8_ARROW_LEFT  "\xE2\x97\x80"   // U+25C0
#define PIXEL_FONT_UTF8_ARROW_RIGHT "\xE2\x96\xB6"   // U+25B6

// Adds the font to ImGui's atlas. Returns nullptr (and logs) on failure.
ImFont* pixel_font_imgui_add();
