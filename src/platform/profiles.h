// The console's gamer profiles, and switching between them from the dashboard.
#pragma once

#include <xtl.h>
#include <string>
#include <vector>

struct ProfileEntry
{
    XUID xuid;
    std::wstring gamertag;
    std::vector<unsigned char> picture;    // the gamer picture (PNG), or empty
    int signedInAs;                        // user index, or -1
    // The gamer card's numbers, from the profile's dashboard settings
    // (FFFE07D1.gpd inside it; cached for when it's signed in and locked). -1: unknown.
    int gamerscore, gamesPlayed, achievements;
    bool onUsb;                            // found on the USB stick (else the hard drive)
    bool live;                             // an Xbox Live profile (seen while signed in; cached)
};

// The card numbers of a profile (any of -1 if unknown); false if no such profile.
bool ProfileNumbers(XUID xuid, int& gamerscore, int& gamesPlayed, int& achievements);

// The profile's gamer zone name as the Guide last read it (SlopGuide writes
// cache\profiles\<XUID>.zone: XPROFILE_GAMERCARD_ZONE's number), or "Homebrew"
// when there's none.
const wchar_t* ProfileZoneName(XUID xuid);

// The card numbers the Guide last read from the profile itself (cache\profiles\
// <XUID>.card, SlopGuide): false if it hasn't.
bool GuideCardNumbers(XUID xuid, int& gamerscore, int& gamesPlayed, int& achievements);

// Finds the profiles on the HDD and USB (Content\<XUID>\FFFE07D1\00010000\<XUID>).
// A signed-in profile's file is locked by the system: its name comes from
// XUserGetName and its picture from game:\cache\profiles\, saved on an
// earlier scan while it was readable.
void ScanProfiles();
// True if a signed-out profile's file changed (or went) since the last scan:
// cheap (a directory search per profile), for a check every few seconds.
bool ProfilesChangedOnDisk();
const std::vector<ProfileEntry>& Profiles();
// The BadAvatar exploit's profile (gamertag ABadAvatar), which Profiles()
// leaves out: its XUID, or 0 if there's none.
XUID HiddenProfile();

// "slopdash://profile/<XUID>": that profile's picture, for the texture
// loader. Empty if there's none.
std::wstring ProfilePictureLocator(XUID xuid);
bool ProfilePng(const wchar_t* locator, const unsigned char** data, unsigned* size);

// "slopdash://gamerpic/<XUID>": the gamer picture the system keeps for a
// profile (XUserReadGamerPicture while it's signed in, saved to the cache for
// later), as kGamerPicSize x kGamerPicSize 32-bit ARGB pixels.
const unsigned kGamerPicSize = 64;
std::wstring GamerPicLocator(XUID xuid);
bool GamerPicPixels(const wchar_t* locator, std::vector<unsigned char>& pixels);

// Signs the profile in at user slot `slot` (whoever is there is signed out
// first), or with xuid 0 just signs that slot out. Runs on a worker thread:
// returns at once; the sign-in state changes when it's done.
bool StartProfileSwitch(int slot, XUID xuid);

// A sign-in change (XN_SYS_SIGNINCHANGED, or at start-up with announce false):
// the console's own notification for each profile signed in or out
// ("<gamertag> signed in"), as the dashboard showed.
void SigninChanged(bool announce);
bool ProfileSwitchBusy();
