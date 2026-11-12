#include "pixel_font_imgui.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_internal.h"   // ImFontLoader and the atlas packing functions a loader needs

#include "pixel_font.h"

// Whole-number pixel size of one font pixel for a requested font size.
static int32_t block_size(float font_size)
{
    int32_t size = static_cast<int32_t>(font_size / PIXEL_FONT_IMGUI_UNIT);
    return size < 1 ? 1 : size;
}

// ImGui reserves glyph index 0 for "not found", so index = character + 1.
static int loader_glyph_index(ImFontAtlas* atlas, ImFontConfig* src, ImWchar codepoint)
{
    (void)atlas;
    (void)src;
    if (codepoint >= 256)
    {
        return 0;
    }
    uint8_t rows[PIXEL_FONT_GLYPH_HEIGHT] = {};
    // pixel_font_glyph_rows() maps lowercase to capitals itself.
    return pixel_font_glyph_rows(static_cast<char>(codepoint), rows) ? static_cast<int>(codepoint) + 1 : 0;
}

static bool loader_src_init(ImFontAtlas* atlas, ImFontConfig* src)
{
    (void)atlas;
    (void)src;
    return true;   // nothing to load: the glyphs are compiled in
}

// Line metrics: 1 empty font pixel above the 5-pixel glyph, 1 below (7 per line).
static bool loader_baked_init(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void* loader_data)
{
    (void)atlas;
    (void)loader_data;
    if (!src->MergeMode)
    {
        float block = static_cast<float>(block_size(baked->Size));
        baked->Ascent = block * 6.0f;
        baked->Descent = -block;
    }
    return true;
}

static bool loader_load_glyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void* loader_data,
                              int glyph_index, ImFontGlyph* out_glyph, float* out_advance_x)
{
    (void)loader_data;
    int32_t block = block_size(baked->Size);
    float advance = static_cast<float>(PIXEL_FONT_ADVANCE * block);

    // "Advance only" requests (ImGui measuring text) need no bitmap.
    if (out_advance_x != nullptr)
    {
        *out_advance_x = advance;
        return true;
    }
    out_glyph->AdvanceX = advance;

    uint8_t rows[PIXEL_FONT_GLYPH_HEIGHT] = {};
    pixel_font_glyph_rows(static_cast<char>(glyph_index - 1), rows);
    bool any_pixel = false;
    for (int32_t y = 0; y < PIXEL_FONT_GLYPH_HEIGHT; y++)
    {
        any_pixel = any_pixel || rows[y] != 0;
    }
    if (!any_pixel)
    {
        return true;   // space: advance only, nothing to draw
    }

    int width = PIXEL_FONT_GLYPH_WIDTH * block;
    int height = PIXEL_FONT_GLYPH_HEIGHT * block;
    ImFontAtlasRectId pack_id = ImFontAtlasPackAddRect(atlas, width, height);
    if (pack_id == ImFontAtlasRectId_Invalid)
    {
        return false;
    }
    ImTextureRect* rect = ImFontAtlasPackGetRect(atlas, pack_id);

    // One byte of coverage per pixel: 255 = on, 0 = off. No anti-aliasing on purpose.
    ImVector<unsigned char>* pixels = &atlas->Builder->TempBuffer;
    pixels->resize(width * height);
    std::memset(pixels->Data, 0, static_cast<size_t>(width * height));
    for (int32_t y = 0; y < PIXEL_FONT_GLYPH_HEIGHT; y++)
    {
        for (int32_t x = 0; x < PIXEL_FONT_GLYPH_WIDTH; x++)
        {
            if (((rows[y] >> (PIXEL_FONT_GLYPH_WIDTH - 1 - x)) & 1u) == 0)
            {
                continue;
            }
            for (int32_t by = 0; by < block; by++)
            {
                std::memset(pixels->Data + (y * block + by) * width + x * block, 255, static_cast<size_t>(block));
            }
        }
    }

    // Glyph quad relative to the text position: one font pixel down from the line's top.
    out_glyph->X0 = 0.0f;
    out_glyph->Y0 = static_cast<float>(block);
    out_glyph->X1 = static_cast<float>(width);
    out_glyph->Y1 = static_cast<float>(block + height);
    out_glyph->Visible = true;
    out_glyph->PackId = pack_id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out_glyph, rect, pixels->Data, ImTextureFormat_Alpha8, width);
    return true;
}

ImFont* pixel_font_imgui_add()
{
    static ImFontLoader loader;
    loader.Name = "piccolo pixel font";
    loader.FontSrcInit = loader_src_init;
    loader.FontSrcGetGlyphIndexFromCodepoint = loader_glyph_index;
    loader.FontBakedInit = loader_baked_init;
    loader.FontBakedLoadGlyph = loader_load_glyph;

    ImFontConfig config;
    config.FontLoader = &loader;
    config.SizePixels = PIXEL_FONT_IMGUI_UNIT;
    config.PixelSnapH = true;
    std::snprintf(config.Name, sizeof(config.Name), "%s", "piccolo pixel font");

    ImFont* font = ImGui::GetIO().Fonts->AddFont(&config);
    if (font == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not add the pixel font to ImGui");
    }
    return font;
}
