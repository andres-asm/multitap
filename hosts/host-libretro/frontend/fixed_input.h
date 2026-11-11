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

#include "libretro_host.h"

struct SDL_Gamepad;

// keyboard_enabled = false while the user types into a text field.
void fixed_input_read(LibretroInputState* input, bool keyboard_enabled, SDL_Gamepad* gamepad);
