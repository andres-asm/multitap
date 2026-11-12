#pragma once

// Fixed controls for the host-libretro test frontend: no remapping, on purpose.
//
// Keyboard (RetroArch's defaults):          Gamepad (SDL3, by position):
//   Arrows        D-pad                       D-pad          D-pad
//   X / Z         A / B                       East / South   A / B
//   S / A         X / Y                       North / West   X / Y
//   Q / W         L / R                       Shoulders      L / R
//   E / R         L2 / R2                     Triggers       L2 / R2
//   Enter         Start                       Start / Back   Start / Select
//   Right Shift   Select                      Stick clicks   L3 / R3
//                                             Sticks         left / right analog
// Everything goes to port 0 (keyboard and the first gamepad are merged).
//
// Short taps: the button state is read once per frame, so a key pressed AND
// released between two frames would be missed. Every press that arrives as an
// SDL event is therefore remembered until the next fixed_input_read(), which
// reports it as held for that one frame.

#include <cstdint>

#include "libretro_host.h"

struct SDL_Gamepad;
union SDL_Event;

struct FixedInput
{
    uint16_t pressed_since_last_read = 0;   // RETRO_DEVICE_ID_JOYPAD_* bits from press events
};

// Call for every SDL event. keyboard_enabled = false while the user types into a text field.
void fixed_input_handle_event(FixedInput* fixed, const SDL_Event* event, bool keyboard_enabled, SDL_Gamepad* gamepad);

// Current state plus the presses remembered since the last call (which it then forgets).
void fixed_input_read(FixedInput* fixed, LibretroInputState* input, bool keyboard_enabled, SDL_Gamepad* gamepad);
