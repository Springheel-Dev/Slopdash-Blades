// A profile's achievements and game history, read from its own files: the
// dashboard's settings (FFFE07D1.gpd: the titles played, the gamer card's
// numbers) and each title's GPD (its achievements and their pictures).
#pragma once

#include <xtl.h>
#include <string>
#include <vector>

#include "core/xdbf.h"

struct AchTitle
{
    xdbf::Title info;
    std::vector<unsigned char> tile;        // the game's 64 x 64 picture (PNG), or empty
    std::vector<size_t> items;              // its achievements, in AchievementData::items
};

struct AchItem
{
    size_t title;                           // in AchievementData::titles
    xdbf::Achievement a;
    std::vector<unsigned char> png;         // its picture (only earned ones have one), or empty
};

struct AchievementData
{
    XUID xuid;
    std::vector<AchTitle> titles;           // those with achievements, the last played first
    std::vector<AchItem> items;
    int unlocked, achievements, earned, gamerscore;     // over all the titles
    // The gamer card's settings (-1 / false if the profile has none).
    int gamerscoreSetting, playedSetting, earnedSetting;
    bool hasRep;
    float rep;
};

// Reads the profile: a signed-in one through the system (XamProfileOpen, as
// 1888's hud.xex opens a profile's files), else its package on HDD or USB.
// cardOnly: the dashboard's settings and title list only, not each title's file.
bool LoadAchievements(XUID xuid, AchievementData& out, bool cardOnly = false);

// The last data loaded (the screen and the texture loader share it).
AchievementData& CurrentAchievements();

// "slopdash://ach/t/<title>/<id>" a title's picture, "slopdash://ach/a/<item>/<id>"
// an achievement's, for the texture loader.
std::wstring AchTitleLocator(size_t title);
std::wstring AchItemLocator(size_t item);
bool AchievementPng(const wchar_t* locator, const unsigned char** data, unsigned* size);
