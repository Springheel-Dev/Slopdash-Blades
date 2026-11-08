// The launch dashboard theme (Blades, 2.0.1888): SlopDash's own versions of
// the dashboard's classes, for the 1888 scenes in dash.xex and xam.xex.
// Kept apart from the 6717 theme (theme/blades.h); only one runs at a time.
#pragma once

#include <xtl.h>
#include <xui.h>

// Registers the implemented classes. Call before RegisterStandIns, which then
// skips them.
HRESULT RegisterBlades1888Classes();
void UnregisterBlades1888Classes();

// The element the main scene sits in: screens opened from the blades and the
// Guide (xam's hudbkgnd) go in it too, so they share its scale.
void SetBlades1888Layer(HXUIOBJ layer);

// B in a screen opened over the blades that nothing handled: back (main.cpp
// calls it when the focus hasn't moved two frames after B).
void Blades1888Back();

// A screen was opened from one open over the blades: play its frames now.
void Blades1888ScreenOpened();

// Screens opened one over another in one go (the Xbox theme's game screens,
// blades.opengame: a game's menu, then its screen over it): each counted at
// its own level, as if opened in turn (the blades only saw the last one, as
// a first level: its screen was drawn over the blade still showing).
// `focus`: what had the focus on the blades before (its blade's content is
// hidden while they're open, as a press on a blade's button hides it).
void Blades1888ScreensOpened(HXUIOBJ first, HXUIOBJ second, HXUIOBJ focus);


// The Guide closed: the gamer cards' picture, gamerscore and zone filled again
// from what the Guide read from the profile (it may have changed them there).
void RefreshGamerCards1888();
