#include "fixed_input.h"

#include <SDL3/SDL.h>

struct KeyBinding
{
    SDL_Scancode key;
    unsigned     button;   // RETRO_DEVICE_ID_JOYPAD_*
};

static const KeyBinding KEY_BINDINGS[] = {
    { SDL_SCANCODE_UP,     RETRO_DEVICE_ID_JOYPAD_UP },
    { SDL_SCANCODE_DOWN,   RETRO_DEVICE_ID_JOYPAD_DOWN },
    { SDL_SCANCODE_LEFT,   RETRO_DEVICE_ID_JOYPAD_LEFT },
    { SDL_SCANCODE_RIGHT,  RETRO_DEVICE_ID_JOYPAD_RIGHT },
    { SDL_SCANCODE_X,      RETRO_DEVICE_ID_JOYPAD_A },
    { SDL_SCANCODE_Z,      RETRO_DEVICE_ID_JOYPAD_B },
    { SDL_SCANCODE_S,      RETRO_DEVICE_ID_JOYPAD_X },
    { SDL_SCANCODE_A,      RETRO_DEVICE_ID_JOYPAD_Y },
    { SDL_SCANCODE_Q,      RETRO_DEVICE_ID_JOYPAD_L },
    { SDL_SCANCODE_W,      RETRO_DEVICE_ID_JOYPAD_R },
    { SDL_SCANCODE_E,      RETRO_DEVICE_ID_JOYPAD_L2 },
    { SDL_SCANCODE_R,      RETRO_DEVICE_ID_JOYPAD_R2 },
    { SDL_SCANCODE_RETURN, RETRO_DEVICE_ID_JOYPAD_START },
    { SDL_SCANCODE_RSHIFT, RETRO_DEVICE_ID_JOYPAD_SELECT },
};

constexpr size_t KEY_BINDING_COUNT = sizeof(KEY_BINDINGS) / sizeof(KEY_BINDINGS[0]);

struct GamepadBinding
{
    SDL_GamepadButton gamepad_button;
    unsigned          button;   // RETRO_DEVICE_ID_JOYPAD_*
};

// By position: the RetroPad uses the SNES layout (B bottom, A right, Y left, X top).
static const GamepadBinding GAMEPAD_BINDINGS[] = {
    { SDL_GAMEPAD_BUTTON_DPAD_UP,        RETRO_DEVICE_ID_JOYPAD_UP },
    { SDL_GAMEPAD_BUTTON_DPAD_DOWN,      RETRO_DEVICE_ID_JOYPAD_DOWN },
    { SDL_GAMEPAD_BUTTON_DPAD_LEFT,      RETRO_DEVICE_ID_JOYPAD_LEFT },
    { SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     RETRO_DEVICE_ID_JOYPAD_RIGHT },
    { SDL_GAMEPAD_BUTTON_SOUTH,          RETRO_DEVICE_ID_JOYPAD_B },
    { SDL_GAMEPAD_BUTTON_EAST,           RETRO_DEVICE_ID_JOYPAD_A },
    { SDL_GAMEPAD_BUTTON_WEST,           RETRO_DEVICE_ID_JOYPAD_Y },
    { SDL_GAMEPAD_BUTTON_NORTH,          RETRO_DEVICE_ID_JOYPAD_X },
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  RETRO_DEVICE_ID_JOYPAD_L },
    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_R },
    { SDL_GAMEPAD_BUTTON_LEFT_STICK,     RETRO_DEVICE_ID_JOYPAD_L3 },
    { SDL_GAMEPAD_BUTTON_RIGHT_STICK,    RETRO_DEVICE_ID_JOYPAD_R3 },
    { SDL_GAMEPAD_BUTTON_START,          RETRO_DEVICE_ID_JOYPAD_START },
    { SDL_GAMEPAD_BUTTON_BACK,           RETRO_DEVICE_ID_JOYPAD_SELECT },
};

constexpr size_t GAMEPAD_BINDING_COUNT = sizeof(GAMEPAD_BINDINGS) / sizeof(GAMEPAD_BINDINGS[0]);

// How far a trigger must be pulled (0..32767) to count as L2/R2 pressed.
constexpr int16_t TRIGGER_THRESHOLD = 16384;

static uint16_t button_bit(unsigned button)
{
    return static_cast<uint16_t>(1u << button);
}

void fixed_input_read(LibretroInputState* input, bool keyboard_enabled, SDL_Gamepad* gamepad)
{
    // Pointer fields are filled by the caller (they depend on where the image is drawn).
    for (unsigned port = 0; port < LIBRETRO_MAX_PORTS; port++)
    {
        input->joypad_buttons[port] = 0;
        input->analog[port][0][0] = 0;
        input->analog[port][0][1] = 0;
        input->analog[port][1][0] = 0;
        input->analog[port][1][1] = 0;
    }

    uint16_t buttons = 0;

    if (keyboard_enabled)
    {
        int key_count = 0;
        const bool* keys = SDL_GetKeyboardState(&key_count);
        for (size_t i = 0; i < KEY_BINDING_COUNT; i++)
        {
            const KeyBinding* binding = &KEY_BINDINGS[i];
            if (static_cast<int>(binding->key) < key_count && keys[binding->key])
            {
                buttons = static_cast<uint16_t>(buttons | button_bit(binding->button));
            }
        }
    }

    if (gamepad != nullptr)
    {
        for (size_t i = 0; i < GAMEPAD_BINDING_COUNT; i++)
        {
            const GamepadBinding* binding = &GAMEPAD_BINDINGS[i];
            if (SDL_GetGamepadButton(gamepad, binding->gamepad_button))
            {
                buttons = static_cast<uint16_t>(buttons | button_bit(binding->button));
            }
        }
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_THRESHOLD)
        {
            buttons = static_cast<uint16_t>(buttons | button_bit(RETRO_DEVICE_ID_JOYPAD_L2));
        }
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_THRESHOLD)
        {
            buttons = static_cast<uint16_t>(buttons | button_bit(RETRO_DEVICE_ID_JOYPAD_R2));
        }

        // SDL and libretro use the same axis range (-32768..32767, +y = down).
        input->analog[0][RETRO_DEVICE_INDEX_ANALOG_LEFT][RETRO_DEVICE_ID_ANALOG_X]  = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        input->analog[0][RETRO_DEVICE_INDEX_ANALOG_LEFT][RETRO_DEVICE_ID_ANALOG_Y]  = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        input->analog[0][RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        input->analog[0][RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_Y] = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    }

    input->joypad_buttons[0] = buttons;
}
