// GameMode only exists on Linux: here it's never available.

#include "game_mode.h"

void game_mode_set(GameMode* game_mode, bool wanted)
{
    game_mode->requested = false;
    game_mode->state = wanted ? GameModeState::not_supported : GameModeState::off;
}
