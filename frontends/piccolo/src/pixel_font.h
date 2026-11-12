#pragma once

// piccolo's pixel font: 5x5 capitals for the retro-style UI.
//
// Drawn for piccolo from scratch: every glyph was designed as "a 5x5 letter",
// not traced from any other font. (The retro UI recreates the FEEL of a classic
// emulator GUI, but that software is GPL: its font, logo and graphics are never
// copied. See CLAUDE.md.)
//
// Layout: each glyph is 5x5 pixels; text advances 6 pixels per character
// (1 pixel gap) and 7 pixels per line. Lowercase letters draw as capitals.
// Characters without a glyph draw as a hollow box, so missing ones are obvious.

#include <cstdint>

constexpr int32_t PIXEL_FONT_GLYPH_WIDTH  = 5;
constexpr int32_t PIXEL_FONT_GLYPH_HEIGHT = 5;
constexpr int32_t PIXEL_FONT_ADVANCE      = 6;   // glyph width + 1 pixel gap
constexpr int32_t PIXEL_FONT_LINE_HEIGHT  = 7;   // glyph height + 2 pixels

// Special characters outside ASCII printing range, for UI arrows.
constexpr char PIXEL_FONT_ARROW_UP    = '\x01';
constexpr char PIXEL_FONT_ARROW_DOWN  = '\x02';
constexpr char PIXEL_FONT_ARROW_LEFT  = '\x03';
constexpr char PIXEL_FONT_ARROW_RIGHT = '\x04';

// One glyph row as 5 bits: bit 4 = leftmost pixel, bit 0 = rightmost.
// Returns false if `character` has no glyph (the rows of the hollow box are
// returned then). Call pixel_font_init() once first.
bool pixel_font_glyph_rows(char character, uint8_t rows[PIXEL_FONT_GLYPH_HEIGHT]);

// Builds the bit rows from the readable table in pixel_font.cpp, checking it
// (rows of exactly 5 '#'/'.' characters, no character defined twice). Returns
// false and logs the first problem if the table is malformed.
bool pixel_font_init();

// Draws `text` into a 32-bit pixel buffer (pitch in pixels), clipped to it.
// Each font pixel becomes a scale x scale block. '\n' starts a new line.
void pixel_font_draw_text(uint32_t* pixels, int32_t width, int32_t height, int32_t pitch_pixels,
                          int32_t x, int32_t y, const char* text, uint32_t color, int32_t scale);

// Width and height in pixels `text` takes at `scale` (longest line, all lines).
void pixel_font_measure(const char* text, int32_t scale, int32_t* width, int32_t* height);

// Every character that has a glyph, in table order (for previews and tests).
const char* pixel_font_characters();
