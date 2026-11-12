#pragma once

// What piccolo's menus contain: menus, their items and the action each item
// triggers. Only data; retro_ui.h draws it. A TEST set for now: most actions
// only report that they were picked.

#include <cstdint>

enum class MenuAction
{
    none,
    load_game,
    reset,
    save_state,
    load_state,
    quit,
    video,
    sound,
    input,
    about,
};

int32_t     menus_count();
const char* menus_label(int32_t menu);
int32_t     menus_item_count(int32_t menu);
const char* menus_item_label(int32_t menu, int32_t item);
MenuAction  menus_item_action(int32_t menu, int32_t item);

// "LOAD GAME" etc., for logs and status text.
const char* menu_action_name(MenuAction action);
