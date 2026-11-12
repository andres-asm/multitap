// pixel_font_preview: draws piccolo's pixel font into a PNG so the glyphs can
// be judged by eye, and checks the font table.
//
// Usage:
//   pixel_font_preview [--scale N] [output.png]   default: pixel_font_preview.png, scale 4
//   pixel_font_preview --check                    table checks only (CTest)
//
// The PNG is written by hand (uncompressed deflate "stored" blocks), so the
// tool needs no image library.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pixel_font.h"

// Retro-ish colors (0xAARRGGBB).
static constexpr uint32_t BACKGROUND = 0xFF20304Cu;
static constexpr uint32_t TEXT_COLOR = 0xFFE8E8F0u;
static constexpr uint32_t DIM_COLOR  = 0xFF8090B0u;
static constexpr uint32_t CELL_COLOR = 0xFF283A5Cu;

// ---------------------------------------------------------------------------
// Minimal PNG writer
// ---------------------------------------------------------------------------

static uint32_t g_crc_table[256] = {};

static void build_crc_table()
{
    for (uint32_t n = 0; n < 256; n++)
    {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
        {
            c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        g_crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t* data, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        crc = g_crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

static void write_u32_be(FILE* file, uint32_t value)
{
    uint8_t bytes[4] = {
        static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value),
    };
    std::fwrite(bytes, 1, 4, file);
}

static void write_chunk(FILE* file, const char type[4], const uint8_t* data, size_t size)
{
    write_u32_be(file, static_cast<uint32_t>(size));
    std::fwrite(type, 1, 4, file);
    if (size > 0)
    {
        std::fwrite(data, 1, size, file);
    }
    uint32_t crc = crc_update(0xFFFFFFFFu, reinterpret_cast<const uint8_t*>(type), 4);
    crc = crc_update(crc, data, size);
    write_u32_be(file, crc ^ 0xFFFFFFFFu);
}

// pixels: width*height 0xAARRGGBB values. Written as 8-bit RGB.
static bool write_png(const char* path, const uint32_t* pixels, int32_t width, int32_t height)
{
    // Raw image data: each row starts with filter byte 0, then R,G,B per pixel.
    size_t row_size = 1 + static_cast<size_t>(width) * 3;
    size_t raw_size = row_size * static_cast<size_t>(height);
    uint8_t* raw = new uint8_t[raw_size];
    for (int32_t y = 0; y < height; y++)
    {
        uint8_t* row = raw + static_cast<size_t>(y) * row_size;
        row[0] = 0;
        for (int32_t x = 0; x < width; x++)
        {
            uint32_t pixel = pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
            row[1 + x * 3 + 0] = static_cast<uint8_t>(pixel >> 16);
            row[1 + x * 3 + 1] = static_cast<uint8_t>(pixel >> 8);
            row[1 + x * 3 + 2] = static_cast<uint8_t>(pixel);
        }
    }

    // zlib stream with stored (uncompressed) deflate blocks of at most 65535 bytes.
    size_t block_count = (raw_size + 65534) / 65535;
    size_t zlib_size = 2 + raw_size + block_count * 5 + 4;
    uint8_t* zlib = new uint8_t[zlib_size];
    size_t out = 0;
    zlib[out++] = 0x78;   // deflate, 32 KB window
    zlib[out++] = 0x01;   // no preset dictionary, fastest; (0x7801 % 31 == 0)
    uint32_t adler_a = 1;
    uint32_t adler_b = 0;
    for (size_t offset = 0; offset < raw_size; offset += 65535)
    {
        size_t length = raw_size - offset < 65535 ? raw_size - offset : 65535;
        bool last = offset + length == raw_size;
        zlib[out++] = last ? 1 : 0;
        zlib[out++] = static_cast<uint8_t>(length);
        zlib[out++] = static_cast<uint8_t>(length >> 8);
        zlib[out++] = static_cast<uint8_t>(~length);
        zlib[out++] = static_cast<uint8_t>(~length >> 8);
        std::memcpy(zlib + out, raw + offset, length);
        out += length;
        for (size_t i = 0; i < length; i++)
        {
            adler_a = (adler_a + raw[offset + i]) % 65521u;
            adler_b = (adler_b + adler_a) % 65521u;
        }
    }
    uint32_t adler = (adler_b << 16) | adler_a;
    zlib[out++] = static_cast<uint8_t>(adler >> 24);
    zlib[out++] = static_cast<uint8_t>(adler >> 16);
    zlib[out++] = static_cast<uint8_t>(adler >> 8);
    zlib[out++] = static_cast<uint8_t>(adler);

    bool ok = false;
    FILE* file = std::fopen(path, "wb");
    if (file != nullptr)
    {
        static const uint8_t SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        std::fwrite(SIGNATURE, 1, 8, file);

        uint8_t header[13] = {};
        header[0] = static_cast<uint8_t>(width >> 24);
        header[1] = static_cast<uint8_t>(width >> 16);
        header[2] = static_cast<uint8_t>(width >> 8);
        header[3] = static_cast<uint8_t>(width);
        header[4] = static_cast<uint8_t>(height >> 24);
        header[5] = static_cast<uint8_t>(height >> 16);
        header[6] = static_cast<uint8_t>(height >> 8);
        header[7] = static_cast<uint8_t>(height);
        header[8] = 8;    // bits per channel
        header[9] = 2;    // RGB
        write_chunk(file, "IHDR", header, sizeof(header));
        write_chunk(file, "IDAT", zlib, out);
        write_chunk(file, "IEND", nullptr, 0);
        ok = std::fclose(file) == 0;
    }

    delete[] zlib;
    delete[] raw;
    return ok;
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

// No two characters may look the same (e.g. 0 and O): that's unreadable in a UI.
static bool check_glyphs_distinct()
{
    const char* characters = pixel_font_characters();
    size_t count = std::strlen(characters);
    bool ok = true;
    for (size_t i = 0; i < count; i++)
    {
        uint8_t a[PIXEL_FONT_GLYPH_HEIGHT] = {};
        pixel_font_glyph_rows(characters[i], a);
        for (size_t j = i + 1; j < count; j++)
        {
            uint8_t b[PIXEL_FONT_GLYPH_HEIGHT] = {};
            pixel_font_glyph_rows(characters[j], b);
            if (std::memcmp(a, b, sizeof(a)) == 0 && characters[i] != ' ')
            {
                std::printf("FAIL glyphs '%c' and '%c' are identical\n", characters[i], characters[j]);
                ok = false;
            }
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Preview image
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    const char* output = "pixel_font_preview.png";
    int32_t scale = 4;
    bool check_only = false;

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--check") == 0)
        {
            check_only = true;
        }
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
        {
            i++;
            scale = static_cast<int32_t>(std::strtol(argv[i], nullptr, 10));
            if (scale < 1 || scale > 16)
            {
                std::fprintf(stderr, "--scale must be 1..16\n");
                return 2;
            }
        }
        else
        {
            output = argv[i];
        }
    }

    if (!pixel_font_init() || !check_glyphs_distinct())
    {
        return 1;
    }
    std::printf("pixel font: %zu glyphs, table OK\n", std::strlen(pixel_font_characters()));
    if (check_only)
    {
        return 0;
    }

    // Layout: a grid of every glyph (16 per row, each in a tinted cell), then sample text.
    const char* characters = pixel_font_characters();
    int32_t count = static_cast<int32_t>(std::strlen(characters));
    constexpr int32_t COLUMNS = 16;
    int32_t cell = (PIXEL_FONT_ADVANCE + 2) * scale;
    int32_t grid_rows = (count + COLUMNS - 1) / COLUMNS;
    int32_t margin = 4 * scale;

    static const char* const SAMPLE =
        "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG.\n"
        "0123456789  +-*/=  ()[]{}<>  !?.,:;  @#$%&_\n"
        "\n"
        "GAME  CONFIG  CHEAT  NETPLAY  MISC\n"
        "LOAD GAME...      F2\n"
        "SAVE STATE  SLOT 0 [OK]\n"
        "VIDEO MODE: 720X480 (3:2)\n"
        "SOUND: 48000 HZ STEREO\n"
        "\x01 \x02 \x03 \x04  ARROWS\n"
        "lowercase draws as capitals";
    int32_t sample_width = 0;
    int32_t sample_height = 0;
    pixel_font_measure(SAMPLE, scale, &sample_width, &sample_height);

    int32_t width = margin * 2 + (COLUMNS * cell > sample_width ? COLUMNS * cell : sample_width);
    int32_t height = margin * 3 + grid_rows * cell + sample_height;
    uint32_t* pixels = new uint32_t[static_cast<size_t>(width) * static_cast<size_t>(height)];
    for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); i++)
    {
        pixels[i] = BACKGROUND;
    }

    for (int32_t i = 0; i < count; i++)
    {
        int32_t cell_x = margin + (i % COLUMNS) * cell;
        int32_t cell_y = margin + (i / COLUMNS) * cell;
        for (int32_t y = cell_y; y < cell_y + cell - scale; y++)
        {
            for (int32_t x = cell_x; x < cell_x + cell - scale; x++)
            {
                pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = CELL_COLOR;
            }
        }
        char text[2] = { characters[i], '\0' };
        pixel_font_draw_text(pixels, width, height, width, cell_x + scale, cell_y + scale, text, TEXT_COLOR, scale);
    }

    pixel_font_draw_text(pixels, width, height, width, margin, margin * 2 + grid_rows * cell, SAMPLE, DIM_COLOR, scale);

    build_crc_table();
    bool ok = write_png(output, pixels, width, height);
    delete[] pixels;
    if (!ok)
    {
        std::fprintf(stderr, "could not write %s\n", output);
        return 1;
    }
    std::printf("wrote %s (%dx%d, scale %d)\n", output, width, height, scale);
    return 0;
}
