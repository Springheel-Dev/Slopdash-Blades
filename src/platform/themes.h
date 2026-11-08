// Themes: whole dashboard styles (Blades, the original Xbox dashboard, NXE...).
// SlopDash runs one at a time; System > Themes switches between them and sets
// the one it starts with. The choice is kept in game:\SlopDash.ini.
#pragma once

#include <string>
#include <vector>

struct ThemeInfo
{
    const char* id;             // as stored in the settings file
    const wchar_t* name;
    const wchar_t* description;
    bool available;             // built into this SlopDash yet
};

const std::vector<ThemeInfo>& Themes();

// The theme SlopDash starts with (the settings file's choice, else Blades).
std::string DefaultTheme();
bool SetDefaultTheme(const std::string& id);

// The one running now.
std::string CurrentTheme();
const ThemeInfo* FindTheme(const std::string& id);

// Switches to another theme: restarts SlopDash into it (SlopDash relaunches
// itself with the theme as its start-up choice for this run). False if the
// theme isn't available.
bool SwitchTheme(const std::string& id);

// Every theme's own dashboard files sit in one folder per theme (e.g.
// <Themes>\blades1888\dash.xex). They are the user's own files: SlopDash reads
// them and never ships them. The SlopDash Importer puts them in SlopDash's own
// folder on the USB stick, game:\Themes; a theme that
// isn't there is looked for on the hard drive, kThemesDir.
static const char* const kStickThemesDir = "game:\\Themes";
static const char* const kThemesDir = "hdd:\\SlopDash\\Themes";
std::string ThemeFolder(const std::string& id);
// A theme's files in one pack instead (<Themes>\<id>.pak, core/pak.h: the
// importer writes Blades' so), looked for in the same two places; "" if there's
// none. A pack is used before a folder.
std::string ThemePack(const std::string& id);

// Called at start-up: the theme to run (a pending switch, else the default).
std::string StartupTheme();

// The system Guide's skin (see plugin/SlopGuide.cpp). Each theme may have one
// at game:\Guide\<theme id>\skin.xzp; the SlopGuide plugin gives the Guide
// whatever is at game:\Guide\skin.xzp when the console starts. That follows
// the default theme (the one SlopDash starts in), unless the theme's Guide is
// set to the system's. A change shows after a restart.
bool ThemeGuideAvailable(const std::string& id);   // the theme has a Guide skin
bool ThemeGuideOn(const std::string& id);          // and it's to be used (default yes)
bool SetThemeGuide(const std::string& id, bool on);
// Puts the default theme's Guide skin in place, or takes it away. True if the
// Guide will be themed after the next restart.
bool SyncGuideSkin();
