#include "keyboard_input.h"

#include <cstring>

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// SDL keycode -> RETROK_*
// ---------------------------------------------------------------------------

struct KeyMapping
{
    SDL_Keycode sdl;
    unsigned    retro;
};

// Keys that aren't printable ASCII. Printable ASCII keycodes are the same in
// both (SDL3 and libretro both use the lowercase character).
static const KeyMapping SPECIAL_KEYS[] = {
    { SDLK_RETURN,       RETROK_RETURN },
    { SDLK_ESCAPE,       RETROK_ESCAPE },
    { SDLK_BACKSPACE,    RETROK_BACKSPACE },
    { SDLK_TAB,          RETROK_TAB },
    { SDLK_DELETE,       RETROK_DELETE },
    { SDLK_UP,           RETROK_UP },
    { SDLK_DOWN,         RETROK_DOWN },
    { SDLK_LEFT,         RETROK_LEFT },
    { SDLK_RIGHT,        RETROK_RIGHT },
    { SDLK_INSERT,       RETROK_INSERT },
    { SDLK_HOME,         RETROK_HOME },
    { SDLK_END,          RETROK_END },
    { SDLK_PAGEUP,       RETROK_PAGEUP },
    { SDLK_PAGEDOWN,     RETROK_PAGEDOWN },
    { SDLK_F1,           RETROK_F1 },
    { SDLK_F2,           RETROK_F2 },
    { SDLK_F3,           RETROK_F3 },
    { SDLK_F4,           RETROK_F4 },
    { SDLK_F5,           RETROK_F5 },
    { SDLK_F6,           RETROK_F6 },
    { SDLK_F7,           RETROK_F7 },
    { SDLK_F8,           RETROK_F8 },
    { SDLK_F9,           RETROK_F9 },
    { SDLK_F10,          RETROK_F10 },
    { SDLK_F11,          RETROK_F11 },
    { SDLK_F12,          RETROK_F12 },
    { SDLK_LSHIFT,       RETROK_LSHIFT },
    { SDLK_RSHIFT,       RETROK_RSHIFT },
    { SDLK_LCTRL,        RETROK_LCTRL },
    { SDLK_RCTRL,        RETROK_RCTRL },
    { SDLK_LALT,         RETROK_LALT },
    { SDLK_RALT,         RETROK_RALT },
    { SDLK_LGUI,         RETROK_LSUPER },
    { SDLK_RGUI,         RETROK_RSUPER },
    { SDLK_CAPSLOCK,     RETROK_CAPSLOCK },
    { SDLK_NUMLOCKCLEAR, RETROK_NUMLOCK },
    { SDLK_PAUSE,        RETROK_PAUSE },
    { SDLK_PRINTSCREEN,  RETROK_PRINT },
    { SDLK_KP_0,         RETROK_KP0 },
    { SDLK_KP_1,         RETROK_KP1 },
    { SDLK_KP_2,         RETROK_KP2 },
    { SDLK_KP_3,         RETROK_KP3 },
    { SDLK_KP_4,         RETROK_KP4 },
    { SDLK_KP_5,         RETROK_KP5 },
    { SDLK_KP_6,         RETROK_KP6 },
    { SDLK_KP_7,         RETROK_KP7 },
    { SDLK_KP_8,         RETROK_KP8 },
    { SDLK_KP_9,         RETROK_KP9 },
    { SDLK_KP_PERIOD,    RETROK_KP_PERIOD },
    { SDLK_KP_DIVIDE,    RETROK_KP_DIVIDE },
    { SDLK_KP_MULTIPLY,  RETROK_KP_MULTIPLY },
    { SDLK_KP_MINUS,     RETROK_KP_MINUS },
    { SDLK_KP_PLUS,      RETROK_KP_PLUS },
    { SDLK_KP_ENTER,     RETROK_KP_ENTER },
    { SDLK_KP_EQUALS,    RETROK_KP_EQUALS },
};

constexpr size_t SPECIAL_KEY_COUNT = sizeof(SPECIAL_KEYS) / sizeof(SPECIAL_KEYS[0]);

static unsigned retro_key_for(SDL_Keycode key)
{
    if (key >= 0x20 && key < 0x7F)
    {
        return static_cast<unsigned>(key);
    }
    for (size_t i = 0; i < SPECIAL_KEY_COUNT; i++)
    {
        if (SPECIAL_KEYS[i].sdl == key)
        {
            return SPECIAL_KEYS[i].retro;
        }
    }
    return RETROK_UNKNOWN;
}

static uint16_t retro_modifiers(SDL_Keymod mod)
{
    struct ModifierMapping
    {
        SDL_Keymod sdl;
        uint16_t   retro;
    };
    static const ModifierMapping MODIFIERS[] = {
        { SDL_KMOD_SHIFT,  RETROKMOD_SHIFT },
        { SDL_KMOD_CTRL,   RETROKMOD_CTRL },
        { SDL_KMOD_ALT,    RETROKMOD_ALT },
        { SDL_KMOD_GUI,    RETROKMOD_META },
        { SDL_KMOD_NUM,    RETROKMOD_NUMLOCK },
        { SDL_KMOD_CAPS,   RETROKMOD_CAPSLOCK },
        { SDL_KMOD_SCROLL, RETROKMOD_SCROLLOCK },
    };
    uint16_t result = 0;
    for (size_t i = 0; i < sizeof(MODIFIERS) / sizeof(MODIFIERS[0]); i++)
    {
        if ((mod & MODIFIERS[i].sdl) != 0)
        {
            result = static_cast<uint16_t>(result | MODIFIERS[i].retro);
        }
    }
    return result;
}

// Next UTF-8 code point from *text (advanced past it); 0 at the end.
// Invalid bytes come out as U+FFFD instead of being skipped silently.
static uint32_t next_code_point(const char** text)
{
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(*text);
    if (bytes[0] == 0)
    {
        return 0;
    }
    uint32_t code = bytes[0];
    size_t length = 1;
    if (code >= 0xF0 && code < 0xF8)
    {
        code &= 0x07u;
        length = 4;
    }
    else if (code >= 0xE0 && code < 0xF0)
    {
        code &= 0x0Fu;
        length = 3;
    }
    else if (code >= 0xC0 && code < 0xE0)
    {
        code &= 0x1Fu;
        length = 2;
    }
    else if (code >= 0x80)
    {
        *text += 1;   // a continuation or invalid lead byte on its own
        return 0xFFFDu;
    }
    for (size_t i = 1; i < length; i++)
    {
        if ((bytes[i] & 0xC0u) != 0x80u)
        {
            *text += i;
            return 0xFFFDu;
        }
        code = (code << 6) | (bytes[i] & 0x3Fu);
    }
    *text += length;
    return code;
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

void keyboard_input_set_game_focus(KeyboardInput* keyboard, bool on, LibretroHost* host, SDL_Window* window)
{
    // Release whatever the core saw as held, so no key stays stuck.
    for (unsigned key = 0; key < LIBRETRO_KEY_COUNT; key++)
    {
        if (keyboard->keys[key] != 0)
        {
            keyboard->keys[key] = 0;
            libretro_host_keyboard_event(host, false, key, 0, 0);
        }
    }
    keyboard->game_focus = on;
    if (on)
    {
        SDL_StartTextInput(window);
    }
    else
    {
        SDL_StopTextInput(window);
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Game focus %s", on ? "on: the keyboard goes to the core" : "off");
}

bool keyboard_input_handle_event(KeyboardInput* keyboard, const SDL_Event* event, LibretroHost* host, SDL_Window* window)
{
    bool key_event = event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP;

    // Scroll Lock (as in RetroArch) or F12 (for keyboards without Scroll Lock)
    // toggle game focus in either state; the core never sees them.
    if (key_event && (event->key.key == SDLK_SCROLLLOCK || event->key.key == SDLK_F12))
    {
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
        {
            keyboard_input_set_game_focus(keyboard, !keyboard->game_focus, host, window);
        }
        return true;
    }
    if (!keyboard->game_focus)
    {
        return false;
    }

    if (key_event)
    {
        bool down = event->type == SDL_EVENT_KEY_DOWN;
        unsigned key = retro_key_for(event->key.key);
        if (key == RETROK_UNKNOWN)
        {
            return true;
        }
        keyboard->keys[key] = down ? 1 : 0;
        // Characters come separately, as SDL text input (below): that's what
        // handles shift, dead keys and keyboard layouts correctly.
        libretro_host_keyboard_event(host, down, key, 0, retro_modifiers(event->key.mod));
        return true;
    }

    if (event->type == SDL_EVENT_TEXT_INPUT && event->text.text != nullptr)
    {
        uint16_t modifiers = retro_modifiers(SDL_GetModState());
        const char* text = event->text.text;
        for (uint32_t character = next_code_point(&text); character != 0; character = next_code_point(&text))
        {
            libretro_host_keyboard_event(host, true, RETROK_UNKNOWN, character, modifiers);
        }
        return true;
    }
    return false;
}

void keyboard_input_update(KeyboardInput* keyboard, SDL_Window* window)
{
    if (keyboard->game_focus && !SDL_TextInputActive(window))
    {
        SDL_StartTextInput(window);
    }
}

void keyboard_input_read(const KeyboardInput* keyboard, LibretroInputState* input)
{
    std::memcpy(input->keys, keyboard->keys, sizeof(input->keys));
}
