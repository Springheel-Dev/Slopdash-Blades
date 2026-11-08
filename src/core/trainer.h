// Trainers in the form Aurora's are made (the trainer packs people share):
// an .xex that is a system DLL (module flags DLL | exports to title) with an
// XEX resource "aurora" holding JSON about it, found by game in
// Trainers\<TitleID>\. As Aurora 0.7b reads them (its 824641F8):
//
//   { "header":   { "framework", "engine", "icon" },
//     "execinfo": { "titleid", "mediaid", "basever", "discnum", "tuver" },
//     "metadata": { "displayname", "description", "author", "version" },
//     "cheats":   [ { "text", "type": "toggle" | "button", "default" }, ... ] }
//
// (The packs people share put "cheats" inside "metadata": both are read.)
//
// The cheats switched on are a 32-bit mask, bit n for cheat n.
//
// Portable C++: builds with the XDK compiler (VS2010) and with g++ for tests.
#pragma once

#include <string>
#include <vector>

namespace trainer {

struct Cheat
{
    std::wstring text;
    bool button;                // pressed in the game (else switched on or off before it)
    bool on;                    // on unless switched off ("default")
};

struct Info
{
    std::wstring name, author, version, description, framework, engine;
    // The game it's for (media ID, base version, disc: 0xFFFFFFFF = any), and
    // the title updates it's for ("tuver": one number or a list; none: the
    // game without one).
    unsigned titleId, mediaId, baseVersion, discNum;
    std::vector<unsigned> titleUpdates;
    std::vector<Cheat> cheats;
    std::vector<unsigned char> icon;    // its "icon" resource, if it has one

    Info() : titleId(0), mediaId(0), baseVersion(0), discNum(0) {}
    unsigned DefaultMask() const;
    int Toggles() const;                // cheats switched before the game (the rest are buttons)
    std::wstring UpdatesText() const;   // "Title Update 3", "Title Updates 0-4", "No Title Update"
};

// The installed game, as a trainer is matched against it.
struct GameVersion
{
    unsigned titleId, mediaId, baseVersion, discNum;
    int titleUpdate;                    // the active one's number, 0 for none, -1 if it can't be told
    GameVersion() : titleId(0), mediaId(0), baseVersion(0), discNum(0), titleUpdate(-1) {}
};

// Whether a trainer suits the game, as Aurora 0.7b decides (82265464-822654F8):
// the same title, and media ID, base version and disc each equal (or any);
// then the game's title update in the trainer's list (an empty list: the
// game without one). Aurora drops other games' trainers and lists the rest,
// counting only those for the title update as compatible.
enum Fit { kFits, kWrongUpdate, kUnknownUpdate, kOtherGame };
Fit Check(const Info& t, const GameVersion& g);

// Reads a trainer .xex (the whole file). False, with the reason, if it isn't
// one (not a system DLL, no "aurora" resource, its JSON not as above).
bool Read(const unsigned char* file, size_t size, Info& out, std::string& error);

}  // namespace trainer
