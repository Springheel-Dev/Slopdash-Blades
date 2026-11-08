// The Blades (launch) theme's colours, the user's own (System > Themes > X):
// each blade's colour (its background, its
// jewel and its glow), the blades' frames, and the background behind them.
// A colour keeps 1888's shading: each of the skins' own colours takes the
// chosen hue, keeping its lightness (and, for the blades, its saturation).
// The skins are recoloured in memory as SlopDash starts, before XUI reads
// them (theme/blade_colours.inc: where each colour is, tools/bladecolours.py),
// so a change shows after SlopDash restarts.
#pragma once

#include <string>

// The parts that can be coloured, in the order the Themes screen lists them.
int BladeColourParts();
const wchar_t* BladeColourPartName(int part);

// The colours a part can take: 0 is "Original" (1888's own).
int BladeColourCount(int part);
const wchar_t* BladeColourName(int part, int colour);

// A colour by its name ("Dark", "Black"...), 0 (Original) if none has it.
int BladeColourIndex(const wchar_t* name);

int BladeColourChoice(int part);
bool SetBladeColourChoice(int part, int colour);

// Recolours the skins (1888's dashuisk, 6717's under M6) for the choices made.
// Call before the skins are loaded.
void ApplyBladeColours();

// Restarts SlopDash so new colours show.
void RestartForBladeColours();

// The gamer card's look (Customize > Gamer Card): 0 Xbox Live Gold (the
// default), 1 Xbox Live Silver, 2 Offline (1888's offline card).
int GamerCardStyles();
const wchar_t* GamerCardStyleName(int style);
int GamerCardStyle();
bool SetGamerCardStyle(int style);

// Dashboard Trim (the Guide's Personal Settings > Customize, 1888's
// DashStyleSelect): 0 Default, 1 Carbon, 2 Glass. 1888's dash.xex loaded
// dashskn<trim>#BladeSkin.xur over its skin for the signed-in profile's
// DashStyle (920A43C4). SlopDash can't open a signed-in profile, so SlopGuide
// leaves the choice in hdd:\SlopDash\Trims\<XUID>.txt.
int ProfileTrim();
// Loads the trim's blade visuals over the skin (after the skin is loaded).
void LoadTrim();
// A trim chosen or a profile signed in: true when the trim to show differs
// from the one loaded (it shows after SlopDash restarts).
bool TrimChanged();
