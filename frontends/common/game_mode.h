#pragma once

// GameMode (Feral Interactive, Linux): asks the gamemoded daemon to put the
// machine in a "game" state while a game runs: above all the CPU governor at
// performance, so the clock doesn't drop in bursts under a light-looking
// load. Measured on the RK3588 low-end target (2026-11-14, 90 s each):
// ondemand 14 audio underruns and the automatic latency up to 112 ms;
// under GameMode 0 underruns, latency at the 52 ms floor.
//
// The client header (deps/gamemode, BSD-3-Clause) loads libgamemode at run
// time: without GameMode installed nothing happens and nothing is linked.
// On other systems this module does nothing (game_mode_none.cpp).
//
// Requests are D-Bus calls that may block a few milliseconds: only make
// them when the wish changes (game_mode_set does that), never every frame.
// The governor also needs the user in the `gamemode` group (polkit);
// otherwise GameMode runs but the daemon logs that it couldn't change it.

enum class GameModeState
{
    off,             // not requested
    active,          // requested and the daemon took it
    not_available,   // GameMode isn't installed or the daemon didn't answer
    not_supported,   // not on this system
};

struct GameMode
{
    bool          requested = false;   // we hold a request now
    GameModeState state     = GameModeState::off;
};

// Asks for GameMode (wanted) or lets it go. Does nothing when unchanged.
// After a failed request it doesn't retry until the wish changes again.
void game_mode_set(GameMode* game_mode, bool wanted);
