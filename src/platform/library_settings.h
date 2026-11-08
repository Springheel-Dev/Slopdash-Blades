// Games Library settings (Games Library > Y): where to look for games, and how
// the library shows them. Kept in game:\SlopDash.ini.
#pragma once

#include <string>
#include <vector>

// The folders the scanner searches (each to a depth of 4). Until the user
// changes them: the console's content folders and Games folders, HDD and USB.
std::vector<std::string> ScanPaths();
std::vector<std::string> DefaultScanPaths();
bool SetScanPaths(const std::vector<std::string>& paths);

// Set when the paths change: the library rescans when it next shows.
bool TakeLibraryChanged();

// Library views: ways to show the games, chosen for each library on its own
// (the Game Library, Xbox Live Arcade, Emulators, Applications: the scope it
// opens in, scanner.h). "list" is List & Description: the list, and the
// highlighted game's description (or its cover art) beside it.
struct LibraryView
{
    const char* id;
    const wchar_t* name;
    const wchar_t* description;
    bool available;
};

const std::vector<LibraryView>& LibraryViews();
std::string CurrentLibraryView();           // the open library's (older names read as their new ones)
bool SetLibraryView(const std::string& id);

// Each view's own options, for the open library too.
bool ListShowsCover();                      // List & Description: cover art instead of the description
bool SetListShowsCover(bool cover);
const int kGridMaxRows = 5, kGridMaxCols = 10;
void LibraryGridSize(int& rows, int& cols);  // Grid: 1..5 rows of 1..10 covers
bool SetLibraryGridSize(int rows, int cols);
void LibraryTilesSize(int& rows, int& cols);  // Tiles: the same, 3 x 7 until chosen
bool SetLibraryTilesSize(int rows, int cols);
bool IndexIcons();                          // Index: small icons beside the names
bool SetIndexIcons(bool on);
// The view and its options in one string: the library redraws when it changes.
std::string LibraryViewStamp();

// A name the user gave a game (Game Settings > Rename), by title ID; "" if none.
std::wstring CustomGameName(unsigned titleId);
bool SetCustomGameName(unsigned titleId, const std::wstring& name);   // "" goes back to its own

// The library rescans when it next shows (after a rename or a delete too).
void MarkLibraryChanged();
