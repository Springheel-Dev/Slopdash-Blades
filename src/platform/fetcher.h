// Online details for the library, fetched once per title and cached.
//
// The cache is game:\cache\<key>\ (next to SlopDash.xex; the key is the title
// ID, or for an executable without one "HB" and a number made from its path):
// info.txt (the details, or found=0), icon.png (XboxUnity), cover.jpg (a front
// cover), wrap.jpg (a whole case wrap, until its front is cropped into
// cover.jpg). Titles cached before covers were fetched get topped up once.
// At startup the cache is read for every game; titles without an entry are
// fetched on a background thread, so nothing waits on the network. After that
// SlopDash works offline.
//
// First Microsoft's catalog; for a title it doesn't know (Original Xbox games,
// homebrew, apps, emulators), the second look (core/catalog.h): xemu's xdb for
// Original Xbox names and front covers, then XboxUnity for a cover.
#pragma once

#include <xtl.h>
#include <xui.h>
#include <string>
#include <vector>

#include "core/catalog.h"

void StartFetcher();

// Stops the background fetch (before the library is rescanned: its entries
// follow the game list). False if it didn't stop. StartFetcher again afterwards.
bool StopFetcher();

// True once, after the background fetch has found official names the library
// doesn't show yet (main.cpp: StopFetcher and StartFetcher again apply them).
bool TakeLearnedNames();

// The cached catalog details for library game n (false if there are none: the
// second look's finds carry no description, developer...).
bool GameDetails(size_t game, catalog::Details& out);

// The game's front cover, if one is cached: "file://game:/cache/<key>/cover.jpg",
// or "slopdash://wrapcover/<key>" for a case wrap not cropped yet.
std::wstring CoverLocator(size_t game);

// The game's screenshot (the catalog's first), if one is cached:
// "file://game:/cache/<key>/shot.jpg" ("" none). The Launch view shows it.
std::wstring ShotLocator(size_t game);

// The texture loader's part for slopdash://wrapcover/: the wrap's front
// (right-hand side), cropped and scaled to a cover's size, then kept as the
// title's cover.jpg so it's cropped only once.
bool IsWrapCoverLocator(LPCWSTR name);
HRESULT WrapCoverTexture(IXuiDevice* device, LPCWSTR name, XUIImageInfo* info, IDirect3DTexture9** tex);

// A fetched icon for game n (when the game's own package has none usable).
bool FetchedIcon(size_t game, std::vector<unsigned char>& png);
// ... or, for homebrew, XboxUnity's JPEG one: "file://game:/cache/<key>/icon.jpg" ("" none).
std::wstring FetchedIconFile(size_t game);
