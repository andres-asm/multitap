#include "retro_ui.h"

#include "imgui.h"
#include "imgui_internal.h"   // DC.CurrLineTextBaseOffset: where ImGui puts a widget's text on its line

#include "pixel_font_imgui.h"

// Palette (0xAARRGGBB): our own colors.
constexpr uint32_t BAR_FACE       = 0xFF34507Cu;
constexpr uint32_t BAR_LIGHT      = 0xFF5878A8u;
constexpr uint32_t BAR_DARK       = 0xFF1A2840u;
constexpr uint32_t TEXT_NORMAL    = 0xFFE0E6F0u;
constexpr uint32_t TEXT_HIGHLIGHT = 0xFFFFFFFFu;
constexpr uint32_t TEXT_SHADOW    = 0xFF101828u;
constexpr uint32_t HIGHLIGHT_FACE = 0xFF7090C8u;
constexpr uint32_t PANEL_FACE     = 0xFF2A4068u;
constexpr uint32_t PANEL_LIGHT    = 0xFF5878A8u;
constexpr uint32_t PANEL_DARK     = 0xFF101828u;

static ImVec4 to_imgui(uint32_t argb)
{
    return ImVec4(static_cast<float>((argb >> 16) & 0xFFu) / 255.0f, static_cast<float>((argb >> 8) & 0xFFu) / 255.0f,
                  static_cast<float>(argb & 0xFFu) / 255.0f, static_cast<float>(argb >> 24) / 255.0f);
}

static ImU32 to_imgui_u32(uint32_t argb)
{
    return ImGui::ColorConvertFloat4ToU32(to_imgui(argb));
}

// ImGui's borders are one flat color, so the raised edge is drawn by hand:
// light top/left, dark bottom/right, one retro pixel (`block` window pixels) thick.
static void draw_bevel(ImVec2 position, ImVec2 size, float block, uint32_t light, uint32_t dark, bool sides)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 end = ImVec2(position.x + size.x, position.y + size.y);
    draw_list->AddRectFilled(position, ImVec2(end.x, position.y + block), to_imgui_u32(light));
    draw_list->AddRectFilled(ImVec2(position.x, end.y - block), end, to_imgui_u32(dark));
    if (sides)
    {
        draw_list->AddRectFilled(position, ImVec2(position.x + block, end.y), to_imgui_u32(light));
        draw_list->AddRectFilled(ImVec2(end.x - block, position.y), end, to_imgui_u32(dark));
    }
}

// Text from the pixel font must be sampled with nearest filtering to stay sharp.
// The callbacks switch the sampler for the rest of this window's draw list.
static void use_nearest_sampler()
{
    ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
}

static void use_linear_sampler()
{
    ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
}

// ImGui has no shadowed text, so menu labels are drawn by us: the widgets get
// a fully transparent text color (they still handle layout, hover, clicks and
// keyboard navigation, and draw their highlight), and we paint shadow + text
// on top at the exact position ImGui would have used.
static void draw_label(ImDrawList* draw_list, ImVec2 text_position, const char* label, bool lit, float block)
{
    ImFont* font = ImGui::GetFont();
    float size = ImGui::GetFontSize();
    draw_list->AddText(font, size, ImVec2(text_position.x + block, text_position.y + block), to_imgui_u32(TEXT_SHADOW), label);
    draw_list->AddText(font, size, text_position, to_imgui_u32(lit ? TEXT_HIGHLIGHT : TEXT_NORMAL), label);
}

// Where ImGui will put the text of the next widget on this line.
static ImVec2 next_text_position(float extra_x)
{
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    return ImVec2(cursor.x + extra_x, cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
}

static void push_invisible_text()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
}

int32_t retro_ui_scale_for_width(float window_width)
{
    int32_t scale = static_cast<int32_t>(window_width / 640.0f);
    return scale < 1 ? 1 : scale;
}

MenuAction retro_ui_menu_bar(ImFont* pixel_font, int32_t scale)
{
    MenuAction picked = MenuAction::none;
    float block = static_cast<float>(scale);

    // Metrics in retro pixels x scale:
    //   bar  = 2 + 7 (font line) + 2 = 11, label padding 3,
    //   item = 7 (font line) + 2 spacing = 9, panel padding 2 (+4 text indent).
    ImGui::PushFont(pixel_font, PIXEL_FONT_IMGUI_UNIT * block);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f * block, 2.0f * block));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f * block, 2.0f * block));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * block, 2.0f * block));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, to_imgui(BAR_FACE));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, to_imgui(PANEL_FACE));
    ImGui::PushStyleColor(ImGuiCol_Text, to_imgui(TEXT_NORMAL));
    ImGui::PushStyleColor(ImGuiCol_Header, to_imgui(HIGHLIGHT_FACE));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, to_imgui(HIGHLIGHT_FACE));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, to_imgui(HIGHLIGHT_FACE));
    ImGui::PushStyleColor(ImGuiCol_Border, to_imgui(PANEL_DARK));

    if (ImGui::BeginMainMenuBar())
    {
        use_nearest_sampler();
        draw_bevel(ImGui::GetWindowPos(), ImGui::GetWindowSize(), block, BAR_LIGHT, BAR_DARK, false);

        ImDrawList* bar_draw_list = ImGui::GetWindowDrawList();
        for (int32_t menu = 0; menu < menus_count(); menu++)
        {
            const char* label = menus_label(menu);

            // On the bar, BeginMenu indents its text by half the item spacing (imgui_widgets.cpp).
            ImVec2 label_position = next_text_position(IM_TRUNC(ImGui::GetStyle().ItemSpacing.x * 0.5f));
            push_invisible_text();
            bool open = ImGui::BeginMenu(label);
            ImGui::PopStyleColor();

            // When open, the current window is now the dropdown: the label still
            // goes into the bar's draw list (saved above). Hover is only known
            // when closed; an open menu is lit anyway.
            bool lit = open || ImGui::IsItemHovered();
            draw_label(bar_draw_list, label_position, label, lit, block);

            if (open)
            {
                use_nearest_sampler();
                draw_bevel(ImGui::GetWindowPos(), ImGui::GetWindowSize(), block, PANEL_LIGHT, PANEL_DARK, true);
                for (int32_t item = 0; item < menus_item_count(menu); item++)
                {
                    const char* item_label = menus_item_label(menu, item);
                    ImVec2 item_position = next_text_position(0.0f);
                    push_invisible_text();
                    if (ImGui::MenuItem(item_label))
                    {
                        picked = menus_item_action(menu, item);
                    }
                    ImGui::PopStyleColor();
                    draw_label(ImGui::GetWindowDrawList(), item_position, item_label, ImGui::IsItemHovered(), block);
                }
                use_linear_sampler();
                ImGui::EndMenu();
            }
        }

        use_linear_sampler();
        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(8);
    ImGui::PopFont();
    return picked;
}

void retro_ui_status_line(ImFont* pixel_font, int32_t scale, const char* text)
{
    float block = static_cast<float>(scale);
    const ImGuiIO& io = ImGui::GetIO();

    // A transparent, input-less window over the whole screen, kept behind every
    // other window, just to have somewhere to draw.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##retro_status", nullptr, flags);
    ImGui::PushFont(pixel_font, PIXEL_FONT_IMGUI_UNIT * block);
    use_nearest_sampler();
    // 4 retro pixels from the left and from the bottom (line = 7, glyph starts 1 down).
    ImVec2 position = ImVec2(4.0f * block, io.DisplaySize.y - (4.0f + 6.0f) * block);
    draw_label(ImGui::GetWindowDrawList(), position, text, false, block);
    use_linear_sampler();
    ImGui::PopFont();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Theme for windows and widgets
// ---------------------------------------------------------------------------

// Window and widget colors, all from the palette above (plus two darker
// shades for list and field backgrounds).
constexpr uint32_t FIELD_FACE   = 0xFF1E2E4Cu;
constexpr uint32_t FIELD_HOVER  = 0xFF26385Cu;

struct ThemeColor
{
    ImGuiCol slot;
    uint32_t argb;
};

static const ThemeColor THEME_COLORS[] = {
    { ImGuiCol_Text,                 TEXT_NORMAL },
    { ImGuiCol_TextDisabled,         0xFF8090B0u },
    { ImGuiCol_WindowBg,             PANEL_FACE },
    { ImGuiCol_ChildBg,              FIELD_FACE },
    { ImGuiCol_PopupBg,              PANEL_FACE },
    { ImGuiCol_Border,               PANEL_DARK },
    { ImGuiCol_TitleBg,              BAR_FACE },
    { ImGuiCol_TitleBgActive,        BAR_FACE },
    { ImGuiCol_TitleBgCollapsed,     BAR_FACE },
    { ImGuiCol_MenuBarBg,            BAR_FACE },
    { ImGuiCol_FrameBg,              FIELD_FACE },
    { ImGuiCol_FrameBgHovered,       FIELD_HOVER },
    { ImGuiCol_FrameBgActive,        FIELD_HOVER },
    { ImGuiCol_Button,               BAR_FACE },
    { ImGuiCol_ButtonHovered,        HIGHLIGHT_FACE },
    { ImGuiCol_ButtonActive,         HIGHLIGHT_FACE },
    { ImGuiCol_Header,               HIGHLIGHT_FACE },
    { ImGuiCol_HeaderHovered,        HIGHLIGHT_FACE },
    { ImGuiCol_HeaderActive,         HIGHLIGHT_FACE },
    { ImGuiCol_CheckMark,            TEXT_HIGHLIGHT },
    { ImGuiCol_ScrollbarBg,          FIELD_FACE },
    { ImGuiCol_ScrollbarGrab,        BAR_FACE },
    { ImGuiCol_ScrollbarGrabHovered, HIGHLIGHT_FACE },
    { ImGuiCol_ScrollbarGrabActive,  HIGHLIGHT_FACE },
    { ImGuiCol_NavCursor,            TEXT_HIGHLIGHT },
};

constexpr int THEME_COLOR_COUNT = static_cast<int>(sizeof(THEME_COLORS) / sizeof(THEME_COLORS[0]));
constexpr int THEME_VAR_COUNT = 12;   // the PushStyleVar calls in retro_ui_push_theme

void retro_ui_push_theme(ImFont* pixel_font, int32_t scale)
{
    float block = static_cast<float>(scale);
    ImGui::PushFont(pixel_font, PIXEL_FONT_IMGUI_UNIT * block);
    for (int i = 0; i < THEME_COLOR_COUNT; i++)
    {
        ImGui::PushStyleColor(THEME_COLORS[i].slot, to_imgui(THEME_COLORS[i].argb));
    }
    // Same metrics as the menu bar, in retro pixels.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * block, 4.0f * block));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f * block, 2.0f * block));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f * block, 2.0f * block));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(3.0f * block, 2.0f * block));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 5.0f * block);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);   // bevels drawn instead
}

void retro_ui_pop_theme()
{
    ImGui::PopStyleVar(THEME_VAR_COUNT);
    ImGui::PopStyleColor(THEME_COLOR_COUNT);
    ImGui::PopFont();
}

void retro_ui_begin_window(int32_t scale)
{
    use_nearest_sampler();
    draw_bevel(ImGui::GetWindowPos(), ImGui::GetWindowSize(), static_cast<float>(scale), PANEL_LIGHT, PANEL_DARK, true);
}

void retro_ui_end_window()
{
    use_linear_sampler();
}

// ---------------------------------------------------------------------------
// Game picture
// ---------------------------------------------------------------------------

void retro_ui_game_picture(uint64_t texture_id, float uv_max_x, float uv_max_y, float aspect)
{
    const ImGuiIO& io = ImGui::GetIO();
    float width = io.DisplaySize.x;
    float height = width / aspect;
    if (height > io.DisplaySize.y)
    {
        height = io.DisplaySize.y;
        width = height * aspect;
    }
    // Whole pixels, so the edges don't land between two screen pixels.
    width = static_cast<float>(static_cast<int32_t>(width));
    height = static_cast<float>(static_cast<int32_t>(height));
    ImVec2 position((io.DisplaySize.x - width) * 0.5f, (io.DisplaySize.y - height) * 0.5f);
    position.x = static_cast<float>(static_cast<int32_t>(position.x));
    position.y = static_cast<float>(static_cast<int32_t>(position.y));

    // The background draw list is behind every window, menu bar included.
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    draw_list->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
    draw_list->AddImage(ImTextureRef(static_cast<ImTextureID>(texture_id)), position,
                        ImVec2(position.x + width, position.y + height), ImVec2(0.0f, 0.0f), ImVec2(uv_max_x, uv_max_y));
    draw_list->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
}
