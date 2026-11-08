// The library's slow details made ahead, on a thread of their own: each game's
// screenshot decoded
// and shrunk to the size Launch shows it (a DXT1 texture, ~90 KB), its ESRB
// rating read from its xex, and whether it has downloadable content - from the
// highlighted game outwards. Made on the UI's thread, a screenshot (a 1920 x
// 1080 JPEG) took 0.8 s: the highlight stopped dead.
#pragma once

#include <xtl.h>
#include <string>
#include <vector>

struct PrefetchJob
{
    std::wstring shot;          // its screenshot's locator (fetcher ShotLocator), "" if none
    std::string path;           // what launches it (for its ESRB rating: scanner EsrbOf)
    unsigned contentType;
    unsigned titleId;
};

// The device textures are made on: the texture loader gives it, each call.
void PrefetchSetDevice(IDirect3DDevice9* device);

// The library's games, by value (a rescan can't pull them away), the worker
// starting at `focus` and working outwards. Again with a new library: what's
// made already is kept.
void PrefetchStart(const std::vector<PrefetchJob>& jobs, int focus);
// The highlight moved: the worker goes on from there.
void PrefetchFocus(int focus);
// Game `job`'s details all made (its screenshot, rating, content).
bool PrefetchFinished(int job);

// The texture made for this locator, if it's made (AddRef'd for the caller),
// with its size.
bool PrefetchedTexture(const wchar_t* locator, IDirect3DTexture9** tex, UINT* width, UINT* height);

// Downloadable content for the title on either drive (looked at once a title;
// thread-safe: the worker and the UI both ask).
bool TitleHasContent(unsigned titleId);
