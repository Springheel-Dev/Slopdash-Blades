// The game library: found on the console's drives at startup.
#pragma once

#include <string>
#include <vector>

#include "core/library.h"

// Scans the usual places (HDD and USB content folders, Games folders). Logs a
// line per game. Synchronous: it only reads headers.
void ScanLibrary();

// A drive plugged in or out (main.cpp): the same scan on a thread of its own,
// which also stops the fetcher. False if one is already under way.
bool StartBackgroundScan();
bool BackgroundScanBusy();

// On the UI thread: once the background scan is done, its list becomes the
// library's (the fetcher started again). How many entries are new and gone.
bool TakeBackgroundScan(int& added, int& removed);

// Whether the library's list was replaced since last asked (an open Game
// Library shows the new one), and how many entries it showed before.
bool TakeLibraryReplaced(size_t& shownBefore);
void MarkLibraryReplaced();             // (the names changed: an open Game Library shows them)

// Official names found online (the fetcher's, one per AllGames() entry, ""
// none): each game without a name of the user's own takes its, and the list
// is sorted again. For each new place, the index the game had before.
std::vector<size_t> RenameGames(const std::vector<std::wstring>& names);

// The games the library shows: those of the current scope, by name.
const std::vector<library::Game>& Games();

// Which games the library shows (one scan; the scope picks from it): all of
// them, Xbox LIVE Arcade only (Blades (launch): Xbox Live Arcade > My Arcade
// Games), or everything else (its Game Library).
// Applications and Emulators are what's in the Apps and Emulators folders
// (HDD and USB, where the Marketplace's store installs them), apart from the
// games.
enum LibraryScope { kScopeAll, kScopeArcade, kScopeNoArcade, kScopeApps, kScopeEmulators };
enum Place { kPlaceGames, kPlaceApps, kPlaceEmulators };
void SetLibraryScope(LibraryScope scope);
LibraryScope CurrentLibraryScope();

// Everything the scan found, whatever the scope (the fetcher works on these),
// and where a game of Games() is in it ((size_t)-1 if out of range).
const std::vector<library::Game>& AllGames();
size_t AllIndex(size_t game);

// "slopdash://icon/<n>/<TitleID>": game n's own icon (a PNG), for the texture loader.
std::wstring IconLocator(size_t game);
bool IconPng(const wchar_t* locator, const unsigned char** data, unsigned* size);

// Game n's ESRB rating, from its xex's header (library::XexEsrb: 0 EC .. 14 AO),
// -1 if it has none or it can't be read. Read once a title (a GOD install's
// default.xex inside its data files), then remembered.
int GameEsrb(size_t game);
// The same for a game by its path and content type (thread-safe).
int EsrbOf(const std::string& path, unsigned contentType);
