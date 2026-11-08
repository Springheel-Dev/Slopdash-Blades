// Starting a game from the library. On success SlopDash ends and the game runs.
#pragma once

#include "core/library.h"

// Launches g (its package, default.xex or default.xbe). Returns only if the
// launch failed; the log says why.
void LaunchGame(const library::Game& g);
