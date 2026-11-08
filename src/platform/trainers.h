// Trainers (core/trainer.h: the form Aurora's trainer packs take), per game in
// SlopDash's game:\Trainers\<TitleID>\. The one chosen for a game and its
// cheats are remembered (settings), and it's loaded just before the game
// starts, as Aurora 0.7b does it (its 822957A0, 82294450): the file read into
// memory and loaded as the system DLL "dll.aurora.trainer"; its export 2
// called with the tag 'AUOA' and the cheats' mask (bit n, cheat n). Nonzero:
// it runs on in the background and works on the game. Zero: its export 3,
// and it's unloaded. One stays loaded until the console restarts.
#pragma once

#include <string>
#include <vector>

#include "core/trainer.h"

namespace trainers {

struct Found
{
    std::string path, file;         // full path; file name (what the settings know it by)
    bool valid;
    trainer::Info info;             // when valid
    std::string error;              // why not, when not
    trainer::Fit fit;               // for the game as it is (kFits, kWrongUpdate, kUnknownUpdate)
};

// The installed game: which release it is (its package's or executable's
// header) and the title update switched on (the number in its file's name,
// tuNNNNNNNN_...; 0 for none, -1 if the files' names don't tell).
trainer::GameVersion VersionOf(unsigned titleId);

// The game's trainers. Ones for another release of it are left out (as
// Aurora does); ones for another title update are listed with their fit.
std::vector<Found> Find(unsigned titleId);
std::string Folder(unsigned titleId);           // game:\Trainers\<TitleID>

// The trainer that starts with the game ("" for none).
std::string Chosen(unsigned titleId);
void Choose(unsigned titleId, const std::string& file);

// Its cheats switched on (the trainer's own defaults until changed).
unsigned Cheats(unsigned titleId, const Found& t);
void SetCheats(unsigned titleId, const Found& t, unsigned mask);

// A trainer is in memory already (one loaded earlier, until a restart).
bool AnyLoaded();

// Just before the game starts: the chosen trainer loaded and started. True
// if there was none to load. False, with the reason (short, for a toast).
bool LoadFor(unsigned titleId, std::wstring& why);

}  // namespace trainers
