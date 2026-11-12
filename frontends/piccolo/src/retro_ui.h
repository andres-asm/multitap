#pragma once

// piccolo's retro-style UI, built from themed Dear ImGui widgets with
// piccolo's pixel font (decided by Andrés, 2026-11-12, after comparing with a
// software-drawn version: ImGui gives navigation and widgets for free).
//
// "Retro pixels": everything is laid out in units of the 5x5 font's pixels,
// times `scale` window pixels, so the UI stays crisp at any whole-number scale.

#include <cstdint>

#include "menus.h"

struct ImFont;

// The scale for a window: about 640 retro pixels across, at least 1.
int32_t retro_ui_scale_for_width(float window_width);

// The menu bar at the top of the window. Call between ImGui::NewFrame() and
// rendering. Returns the item picked this frame (MenuAction::none if none).
MenuAction retro_ui_menu_bar(ImFont* pixel_font, int32_t scale);

// One line of text at the bottom left of the window, behind other windows, in
// the pixel font with a drop shadow.
void retro_ui_status_line(ImFont* pixel_font, int32_t scale, const char* text);
