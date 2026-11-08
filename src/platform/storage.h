// System > Memory's content, as 1888's
// Memory area had it (memory#Categories / Items / ItemDetails): a drive's
// Content folder - <drive>:\Content\<owner XUID>\<title ID>\<content type>\
// <package> - read package by package (its STFS header: name, game, type,
// thumbnail), sorted into 1888's categories (Games, Gamer Profiles, Demos,
// Videos and Trailers, Themes, Gamer Pictures), and copied, moved or deleted
// between the hard drive and a USB drive on a thread of its own.
//
// The BadAvatar exploit's profile (gamertag ABadAvatar) and everything it
// owns are never listed: SlopDash hides that profile everywhere, and it
// starts the softmod.
#pragma once

#include <xtl.h>
#include <string>
#include <vector>

namespace storage {

// 1888's categories, in its order (memory Strings.xus 4, 6, 2, 8, 7, 3).
enum Category { kGames, kProfiles, kDemos, kVideos, kThemes, kGamerPictures, kCategoryCount };
const wchar_t* CategoryName(Category c);

struct Item
{
    std::string path;               // the package
    std::string dataDir;            // its "<package>.data" folder (games on demand), or empty
    XUID owner;                     // 0: the console's (0000000000000000)
    DWORD titleId, type;
    std::wstring name, titleName;
    unsigned long long size;
    std::vector<unsigned char> icon;    // its thumbnail (PNG), or empty
};

struct Game
{
    DWORD titleId;
    std::wstring name;
    std::vector<size_t> items;
};

// Reads a drive's content ("hdd:" or "usb:"). How many items.
size_t Scan(const std::string& drive);
const std::string& Drive();
std::wstring DriveName(const std::string& drive);   // "Hard Drive", "USB Storage Device"

size_t Count();
const Item& At(size_t i);
std::vector<size_t> InCategory(Category c);         // its items (for Games: each game's, by game)
const std::vector<Game>& Games();                   // the games that have items here

const wchar_t* TypeName(DWORD type);                // "Saved Game", "Gamer Profile"...
std::wstring OwnerName(size_t i);                    // its profile's gamertag, "Unknown Profile", or ""
std::wstring SizeText(unsigned long long bytes);     // "12.3 MB"
std::wstring Locator(size_t i);                      // "slopdash://content/<i>": its thumbnail
bool ContentPng(const wchar_t* locator, const unsigned char** data, unsigned* size);

// The other drive an item can go to ("usb:" from "hdd:"...), empty if none.
std::string OtherDrive();
// The item's path on another drive (the same place under Content).
std::string PathOn(size_t i, const std::string& drive);

enum Op { kCopy, kMove, kDelete };
// Starts it on its own thread; false if one is running or it can't start.
bool Start(Op op, size_t item, const std::string& toDrive);
bool Busy();
int Percent();
bool LastSucceeded();
const wchar_t* LastError();         // 1888's words for what went wrong, or ""

}  // namespace storage
