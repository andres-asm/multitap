#include "menus.h"

struct MenuItem
{
    const char* label;
    MenuAction  action;
};

struct Menu
{
    const char*     label;
    const MenuItem* items;
    int32_t         item_count;
};

static const MenuItem GAME_ITEMS[] = {
    { "LOAD GAME...", MenuAction::load_game },
    { "RESET",        MenuAction::reset },
    { "SAVE STATE",   MenuAction::save_state },
    { "LOAD STATE",   MenuAction::load_state },
    { "QUIT",         MenuAction::quit },
};

static const MenuItem CONFIG_ITEMS[] = {
    { "INPUT...", MenuAction::input },
    { "VIDEO...", MenuAction::video },
    { "SOUND...", MenuAction::sound },
};

static const MenuItem MISC_ITEMS[] = {
    { "DEBUG WINDOW",  MenuAction::debug_window },
    { "ABOUT PICCOLO", MenuAction::about },
};

static const Menu MENUS[] = {
    { "GAME",   GAME_ITEMS,   static_cast<int32_t>(sizeof(GAME_ITEMS) / sizeof(GAME_ITEMS[0])) },
    { "CONFIG", CONFIG_ITEMS, static_cast<int32_t>(sizeof(CONFIG_ITEMS) / sizeof(CONFIG_ITEMS[0])) },
    { "MISC",   MISC_ITEMS,   static_cast<int32_t>(sizeof(MISC_ITEMS) / sizeof(MISC_ITEMS[0])) },
};

constexpr int32_t MENU_COUNT = static_cast<int32_t>(sizeof(MENUS) / sizeof(MENUS[0]));

int32_t menus_count()
{
    return MENU_COUNT;
}

const char* menus_label(int32_t menu)
{
    return MENUS[menu].label;
}

int32_t menus_item_count(int32_t menu)
{
    return MENUS[menu].item_count;
}

const char* menus_item_label(int32_t menu, int32_t item)
{
    return MENUS[menu].items[item].label;
}

MenuAction menus_item_action(int32_t menu, int32_t item)
{
    return MENUS[menu].items[item].action;
}

const char* menu_action_name(MenuAction action)
{
    switch (action)
    {
        case MenuAction::none:       return "NONE";
        case MenuAction::load_game:  return "LOAD GAME";
        case MenuAction::reset:      return "RESET";
        case MenuAction::save_state: return "SAVE STATE";
        case MenuAction::load_state: return "LOAD STATE";
        case MenuAction::quit:       return "QUIT";
        case MenuAction::video:      return "VIDEO";
        case MenuAction::sound:      return "SOUND";
        case MenuAction::input:      return "INPUT";
        case MenuAction::about:      return "ABOUT";
        case MenuAction::debug_window: return "DEBUG WINDOW";
        default:                     return "?";
    }
}
