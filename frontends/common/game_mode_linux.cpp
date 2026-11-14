// GameMode through its client header (deps/gamemode/lib, BSD-3-Clause):
// static inline functions that dlopen libgamemode.so.0 on first use.

#include "game_mode.h"

#include <SDL3/SDL.h>

#include "gamemode_client.h"

void game_mode_set(GameMode* game_mode, bool wanted)
{
    if (wanted == game_mode->requested)
    {
        return;
    }
    if (!wanted)
    {
        gamemode_request_end();
        game_mode->requested = false;
        game_mode->state = GameModeState::off;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameMode: released");
        return;
    }
    // Held as "requested" even on failure, so a missing GameMode isn't
    // asked again every loop; letting go resets it.
    game_mode->requested = true;
    if (gamemode_request_start() != 0)
    {
        game_mode->state = GameModeState::not_available;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameMode: not available (%s)", gamemode_error_string());
        return;
    }
    game_mode->state = GameModeState::active;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GameMode: requested");
}
