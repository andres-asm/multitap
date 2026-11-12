#include "pixel_font.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// The glyphs
// ---------------------------------------------------------------------------
//
// Written as pictures so anyone can read and edit them: '#' = pixel on,
// '.' = off. Drawn for piccolo (see pixel_font.h). Style: capitals that use
// the full 5x5 cell, rounded where a curve reads better, digits with a dot in
// the zero so 0 and O differ.

struct GlyphPicture
{
    char        character;
    const char* rows[PIXEL_FONT_GLYPH_HEIGHT];
};

static const GlyphPicture GLYPHS[] = {
    { ' ', { ".....", ".....", ".....", ".....", "....." } },

    // Digits
    { '0', { ".###.", "#...#", "#.#.#", "#...#", ".###." } },
    { '1', { "..#..", ".##..", "..#..", "..#..", "#####" } },
    { '2', { ".###.", "#...#", "...#.", "..#..", "#####" } },
    { '3', { "####.", "....#", "..##.", "....#", "####." } },
    { '4', { "#..#.", "#..#.", "#####", "...#.", "...#." } },
    { '5', { "#####", "#....", "#####", "....#", "#####" } },
    { '6', { "..##.", ".#...", "####.", "#...#", ".###." } },
    { '7', { "#####", "....#", "...#.", "..#..", "..#.." } },
    { '8', { ".###.", "#...#", "#####", "#...#", ".###." } },
    { '9', { ".###.", "#...#", ".####", "...#.", ".##.." } },

    // Letters
    { 'A', { "..#..", ".#.#.", "#...#", "#####", "#...#" } },
    { 'B', { "####.", "#..#.", "####.", "#...#", "####." } },
    { 'C', { ".####", "#....", "#....", "#....", ".####" } },
    { 'D', { "###..", "#..#.", "#...#", "#..#.", "###.." } },
    { 'E', { "#####", "#....", "###..", "#....", "#####" } },
    { 'F', { "#####", "#....", "###..", "#....", "#...." } },
    { 'G', { ".###.", "#....", "#.###", "#...#", ".####" } },
    { 'H', { "#...#", "#...#", "#####", "#...#", "#...#" } },
    { 'I', { ".###.", "..#..", "..#..", "..#..", ".###." } },
    { 'J', { "....#", "....#", "....#", "#...#", ".###." } },
    { 'K', { "#...#", "#..#.", "###..", "#..#.", "#...#" } },
    { 'L', { "#....", "#....", "#....", "#....", "#####" } },
    { 'M', { "#...#", "##.##", "#.#.#", "#...#", "#...#" } },
    { 'N', { "#...#", "##..#", "#.#.#", "#..##", "#...#" } },
    { 'O', { ".###.", "#...#", "#...#", "#...#", ".###." } },
    { 'P', { "####.", "#...#", "#...#", "####.", "#...." } },
    { 'Q', { ".###.", "#...#", "#...#", ".###.", "...##" } },
    { 'R', { "####.", "#...#", "#...#", "####.", "#...#" } },
    { 'S', { ".###.", "#....", ".###.", "....#", ".###." } },
    { 'T', { "#####", "..#..", "..#..", "..#..", "..#.." } },
    { 'U', { "#...#", "#...#", "#...#", "#...#", ".###." } },
    { 'V', { "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
    { 'W', { "#...#", "#...#", "#.#.#", "##.##", "#...#" } },
    { 'X', { "#...#", ".#.#.", "..#..", ".#.#.", "#...#" } },
    { 'Y', { "#...#", ".#.#.", "..#..", "..#..", "..#.." } },
    { 'Z', { "#####", "...#.", "..#..", ".#...", "#####" } },

    // Punctuation and symbols
    { '.', { ".....", ".....", ".....", ".....", "..#.." } },
    { ',', { ".....", ".....", ".....", "..#..", ".#..." } },
    { ':', { ".....", "..#..", ".....", "..#..", "....." } },
    { ';', { ".....", "..#..", ".....", "..#..", ".#..." } },
    { '!', { "..#..", "..#..", "..#..", ".....", "..#.." } },
    { '?', { "####.", "....#", "..##.", ".....", "..#.." } },
    { '-', { ".....", ".....", ".###.", ".....", "....." } },
    { '_', { ".....", ".....", ".....", ".....", "#####" } },
    { '+', { ".....", "..#..", ".###.", "..#..", "....." } },
    { '=', { ".....", ".###.", ".....", ".###.", "....." } },
    { '*', { ".....", "#.#.#", ".###.", "#.#.#", "....." } },
    { '/', { "....#", "...#.", "..#..", ".#...", "#...." } },
    { '\\', { "#....", ".#...", "..#..", "...#.", "....#" } },
    { '|', { "..#..", "..#..", "..#..", "..#..", "..#.." } },
    { '(', { "..#..", ".#...", ".#...", ".#...", "..#.." } },
    { ')', { "..#..", "...#.", "...#.", "...#.", "..#.." } },
    { '[', { ".##..", ".#...", ".#...", ".#...", ".##.." } },
    { ']', { "..##.", "...#.", "...#.", "...#.", "..##." } },
    { '{', { "..##.", "..#..", ".##..", "..#..", "..##." } },
    { '}', { ".##..", "..#..", "..##.", "..#..", ".##.." } },
    { '<', { "...#.", "..#..", ".#...", "..#..", "...#." } },
    { '>', { ".#...", "..#..", "...#.", "..#..", ".#..." } },
    { '\'', { "..#..", "..#..", ".....", ".....", "....." } },
    { '"', { "#.#..", "#.#..", ".....", ".....", "....." } },
    { '`', { ".#...", "..#..", ".....", ".....", "....." } },
    { '^', { "..#..", ".#.#.", ".....", ".....", "....." } },
    { '~', { ".....", ".#..#", "#.##.", ".....", "....." } },
    { '#', { ".#.#.", "#####", ".#.#.", "#####", ".#.#." } },
    { '$', { ".###.", "#.#..", ".###.", "..#.#", ".###." } },
    { '%', { "##..#", "##.#.", "..#..", ".#.##", "#..##" } },
    { '&', { ".##..", "#..#.", ".##.#", "#..#.", ".##.#" } },
    { '@', { ".###.", "#.#.#", "#.###", "#....", ".####" } },

    // UI arrows (small solid triangles)
    { PIXEL_FONT_ARROW_UP,    { ".....", "..#..", ".###.", "#####", "....." } },
    { PIXEL_FONT_ARROW_DOWN,  { ".....", "#####", ".###.", "..#..", "....." } },
    { PIXEL_FONT_ARROW_LEFT,  { "...#.", "..##.", ".###.", "..##.", "...#." } },
    { PIXEL_FONT_ARROW_RIGHT, { ".#...", ".##..", ".###.", ".##..", ".#..." } },
};

constexpr size_t GLYPH_COUNT = sizeof(GLYPHS) / sizeof(GLYPHS[0]);

// Drawn for characters without a glyph.
static const char* const MISSING_ROWS[PIXEL_FONT_GLYPH_HEIGHT] = { "#####", "#...#", "#...#", "#...#", "#####" };

// ---------------------------------------------------------------------------
// Lookup table built from the pictures
// ---------------------------------------------------------------------------

// Indexed by the (unsigned) character. has_glyph = false -> MISSING_ROWS.
static uint8_t g_rows[256][PIXEL_FONT_GLYPH_HEIGHT] = {};
static bool    g_has_glyph[256] = {};
static uint8_t g_missing[PIXEL_FONT_GLYPH_HEIGHT] = {};
static char    g_characters[GLYPH_COUNT + 1] = {};
static bool    g_ready = false;

static bool row_to_bits(const char* row, uint8_t* bits)
{
    if (row == nullptr || std::strlen(row) != static_cast<size_t>(PIXEL_FONT_GLYPH_WIDTH))
    {
        return false;
    }
    uint8_t value = 0;
    for (int32_t x = 0; x < PIXEL_FONT_GLYPH_WIDTH; x++)
    {
        value = static_cast<uint8_t>(value << 1);
        if (row[x] == '#')
        {
            value = static_cast<uint8_t>(value | 1u);
        }
        else if (row[x] != '.')
        {
            return false;
        }
    }
    *bits = value;
    return true;
}

bool pixel_font_init()
{
    std::memset(g_rows, 0, sizeof(g_rows));
    std::memset(g_has_glyph, 0, sizeof(g_has_glyph));

    for (int32_t y = 0; y < PIXEL_FONT_GLYPH_HEIGHT; y++)
    {
        row_to_bits(MISSING_ROWS[y], &g_missing[y]);
    }

    for (size_t i = 0; i < GLYPH_COUNT; i++)
    {
        const GlyphPicture* glyph = &GLYPHS[i];
        uint8_t index = static_cast<uint8_t>(glyph->character);
        if (g_has_glyph[index])
        {
            std::fprintf(stderr, "pixel_font: character 0x%02x is defined twice\n", index);
            return false;
        }
        for (int32_t y = 0; y < PIXEL_FONT_GLYPH_HEIGHT; y++)
        {
            if (!row_to_bits(glyph->rows[y], &g_rows[index][y]))
            {
                std::fprintf(stderr, "pixel_font: character 0x%02x row %d is not 5 of '#'/'.'\n", index, y);
                return false;
            }
        }
        g_has_glyph[index] = true;
        g_characters[i] = glyph->character;
    }
    g_characters[GLYPH_COUNT] = '\0';
    g_ready = true;
    return true;
}

bool pixel_font_glyph_rows(char character, uint8_t rows[PIXEL_FONT_GLYPH_HEIGHT])
{
    // Lowercase draws as uppercase: the font is capitals only.
    if (character >= 'a' && character <= 'z')
    {
        character = static_cast<char>(character - 'a' + 'A');
    }
    uint8_t index = static_cast<uint8_t>(character);
    bool found = g_ready && g_has_glyph[index];
    const uint8_t* source = found ? g_rows[index] : g_missing;
    std::memcpy(rows, source, PIXEL_FONT_GLYPH_HEIGHT);
    return found;
}

const char* pixel_font_characters()
{
    return g_characters;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void fill_block(uint32_t* pixels, int32_t width, int32_t height, int32_t pitch_pixels,
                       int32_t x, int32_t y, int32_t size, uint32_t color)
{
    for (int32_t block_y = y; block_y < y + size; block_y++)
    {
        if (block_y < 0 || block_y >= height)
        {
            continue;
        }
        for (int32_t block_x = x; block_x < x + size; block_x++)
        {
            if (block_x >= 0 && block_x < width)
            {
                pixels[static_cast<size_t>(block_y) * static_cast<size_t>(pitch_pixels) + static_cast<size_t>(block_x)] = color;
            }
        }
    }
}

void pixel_font_draw_text(uint32_t* pixels, int32_t width, int32_t height, int32_t pitch_pixels,
                          int32_t x, int32_t y, const char* text, uint32_t color, int32_t scale)
{
    int32_t pen_x = x;
    int32_t pen_y = y;
    for (const char* cursor = text; *cursor != '\0'; cursor++)
    {
        if (*cursor == '\n')
        {
            pen_x = x;
            pen_y += PIXEL_FONT_LINE_HEIGHT * scale;
            continue;
        }

        uint8_t rows[PIXEL_FONT_GLYPH_HEIGHT] = {};
        pixel_font_glyph_rows(*cursor, rows);
        for (int32_t row = 0; row < PIXEL_FONT_GLYPH_HEIGHT; row++)
        {
            for (int32_t column = 0; column < PIXEL_FONT_GLYPH_WIDTH; column++)
            {
                // Bit 4 is the leftmost pixel.
                if ((rows[row] >> (PIXEL_FONT_GLYPH_WIDTH - 1 - column)) & 1u)
                {
                    fill_block(pixels, width, height, pitch_pixels, pen_x + column * scale, pen_y + row * scale, scale, color);
                }
            }
        }
        pen_x += PIXEL_FONT_ADVANCE * scale;
    }
}

void pixel_font_measure(const char* text, int32_t scale, int32_t* width, int32_t* height)
{
    int32_t longest = 0;
    int32_t current = 0;
    int32_t lines = 1;
    for (const char* cursor = text; *cursor != '\0'; cursor++)
    {
        if (*cursor == '\n')
        {
            lines++;
            current = 0;
            continue;
        }
        current++;
        if (current > longest)
        {
            longest = current;
        }
    }
    // The last character needs no gap after it; the last line no spacing below it.
    *width = longest > 0 ? (longest * PIXEL_FONT_ADVANCE - 1) * scale : 0;
    *height = ((lines - 1) * PIXEL_FONT_LINE_HEIGHT + PIXEL_FONT_GLYPH_HEIGHT) * scale;
}
