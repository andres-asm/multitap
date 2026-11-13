#pragma once

// The keyboard as a libretro keyboard, for cores that want one (typing,
// computers, the Test libretro core's SRAM text field).
//
// "Game focus" (toggled with Scroll Lock like RetroArch, or F12): while it's on, the
// keyboard belongs to the core alone. Keys become RETRO_DEVICE_KEYBOARD state
// and key events for the core's keyboard callback, typed text becomes
// characters, and the fixed joypad keys (arrows, Z/X, Enter...) are off, so
// typing "q" doesn't also press L. While it's off, nothing goes to the core
// as a keyboard. The gamepad works either way. (F12 is therefore never sent
// to the core as a key.)

#include <cstdint>

#include "libretro_host.h"

struct SDL_Window;
union SDL_Event;

struct KeyboardInput
{
    bool    game_focus = false;
    uint8_t keys[LIBRETRO_KEY_COUNT] = {};   // RETROK_* -> held, while game focus is on
};

// Call for every SDL event. Returns true if the event was the keyboard's
// (game focus toggle, or a key/text event sent to the core) and nothing else
// should act on it.
bool keyboard_input_handle_event(KeyboardInput* keyboard, const SDL_Event* event, LibretroHost* host, SDL_Window* window);

// Turns game focus on or off (releases every key the core saw as held).
void keyboard_input_set_game_focus(KeyboardInput* keyboard, bool on, LibretroHost* host, SDL_Window* window);

// Once per frame: keeps SDL text input on while game focus is on (Dear ImGui
// turns it off when one of its own text fields loses focus).
void keyboard_input_update(KeyboardInput* keyboard, SDL_Window* window);

// Copies the held keys into this frame's input state.
void keyboard_input_read(const KeyboardInput* keyboard, LibretroInputState* input);
