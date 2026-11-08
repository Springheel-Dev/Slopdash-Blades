// XDBF: the format of a profile's settings files (*.gpd inside a profile
// package): an entry table (namespace, 64-bit id, offset, length) then the data.
// Checked against a real profile (FFFE07D1.gpd and the titles' own GPDs).
#pragma once

#include <string>
#include <vector>

namespace xdbf {

enum Namespace
{
    kAchievement = 1,
    kImage = 2,
    kSetting = 3,
    kTitle = 4,
    kString = 5,
};

struct Entry
{
    unsigned ns;
    unsigned long long id;
    const unsigned char* data;
    size_t size;
};

// The file's entries (pointing into gpd, which must outlive them). False if
// it isn't an XDBF file.
bool Entries(const unsigned char* gpd, size_t size, std::vector<Entry>& out);
const Entry* Find(const std::vector<Entry>& entries, unsigned ns, unsigned long long id);

// A title the profile has played (FFFE07D1.gpd, namespace 4): +0 title ID, +4
// achievements, +8 unlocked, +0x0C gamerscore possible, +0x10 earned, +0x20
// last played (FILETIME), +0x28 the name (UTF-16BE, ends with 0).
struct Title
{
    unsigned titleId;
    int achievements, unlocked, gamerscore, earned;
    unsigned long long lastPlayed;
    std::wstring name;
};
bool ReadTitle(const Entry& e, Title& t);

// An achievement (a title's own GPD, namespace 1): +4 its id, +8 its picture's
// image id, +0x0C gamerscore, +0x10 flags, +0x14 when it was unlocked
// (FILETIME), +0x1C three UTF-16BE strings: name, description, the
// description while it's locked.
struct Achievement
{
    unsigned id, imageId, flags;
    int gamerscore;
    unsigned long long unlocked;
    std::wstring name, description, lockedDescription;
    bool Earned() const { return (flags & 0x30000) != 0; }     // 0x20000 earned, 0x10000 earned online
    bool ShownLocked() const { return (flags & 0x8) != 0; }    // else secret while locked
};
bool ReadAchievement(const Entry& e, Achievement& a);

// A setting (namespace 3): +0 its id, +8 its type, +0x10 the value. Int32 (type
// 1) and float (type 5) here.
bool SettingInt(const std::vector<Entry>& entries, unsigned id, int& value);
bool SettingFloat(const std::vector<Entry>& entries, unsigned id, float& value);

}  // namespace xdbf
