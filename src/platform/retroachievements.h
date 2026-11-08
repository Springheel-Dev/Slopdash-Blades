// RetroAchievements, the way EmuDeck and RetroDECK do it: one sign-in in the
// dashboard (the Connect API's login2: username and password once, then a
// token), kept for the emulators that can use it. Only the username, the
// token and the points are kept (settings ra.*), never the password; the
// token is a secret too (RetroAchievements' own docs).
//
// No Xbox 360 emulator speaks RetroAchievements yet (RetroArch's last 360
// build, 1.0.0.2, came before RetroArch's support), so for now the sign-in
// shows the account and its points, the token kept for when one does.
#pragma once

#include <string>
#include <vector>

namespace ra {

struct Account
{
    bool signedIn;
    std::string user, token;
    int points, softcorePoints;     // -1 until known
};
Account Current();

// On a thread of their own; Busy() while it runs, then TakeOutcome() once:
// "" for none yet, else what happened, short (for the screen's status line).
bool StartSignIn(const std::string& user, const std::string& password);
bool StartRefresh();                // the points again, with the token
bool Busy();
std::wstring TakeOutcome();

void SignOut();

}  // namespace ra
