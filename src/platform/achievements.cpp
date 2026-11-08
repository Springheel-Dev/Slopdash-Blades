#include "platform/achievements.h"

#include <stdio.h>
#include <algorithm>

#include "core/stfs.h"
#include "platform/log.h"

namespace {

AchievementData g_data;

// A signed-in profile's file is locked: its settings files come from the copy
// SlopDash keeps of them (platform/profiles.cpp, from the last time it read
// the profile signed out). XamProfileOpen, as 1888's hud.xex used, failed on
// 17559 (14) and then hung SlopDash (0.0.98).
struct Source
{
    bool mounted;                           // read from the cache folder
    std::string dir;
    std::vector<unsigned char> package;     // the profile package, when read directly

    Source() : mounted(false) {}

    bool Read(const char* name, std::vector<unsigned char>& out)
    {
        out.clear();
        if (mounted)
        {
            std::string path = dir + "\\" + name;
            HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (h == INVALID_HANDLE_VALUE)
                return false;
            DWORD size = GetFileSize(h, NULL), got = 0;
            out.resize(size);
            BOOL ok = size == 0 || (ReadFile(h, &out[0], size, &got, NULL) && got == size);
            CloseHandle(h);
            return ok && !out.empty();
        }
        return !package.empty() && stfs::ReadFile(&package[0], package.size(), name, out) && !out.empty();
    }
};

bool SignedIn(XUID xuid)
{
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
    {
        XUID x = 0;
        if (XUserGetSigninState(i) != eXUserSigninState_NotSignedIn && XUserGetXUID(i, &x) == ERROR_SUCCESS &&
            x == xuid)
            return true;
    }
    return false;
}

bool ReadPackage(XUID xuid, std::vector<unsigned char>& out)
{
    char hex[17];
    _snprintf_s(hex, sizeof(hex), _TRUNCATE, "%016I64X", (unsigned __int64)xuid);
    static const char* const kRoots[] = { "hdd:\\Content\\", "usb:\\Content\\" };
    for (int r = 0; r < 2; r++)
    {
        std::string path = std::string(kRoots[r]) + hex + "\\FFFE07D1\\00010000\\" + hex;
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        DWORD size = GetFileSize(h, NULL), got = 0;
        out.resize(size);
        BOOL ok = size && ReadFile(h, &out[0], size, &got, NULL) && got == size;
        CloseHandle(h);
        if (ok)
            return true;
    }
    out.clear();
    return false;
}

bool LastPlayedFirst(const AchTitle& a, const AchTitle& b)
{
    return a.info.lastPlayed > b.info.lastPlayed;
}

}  // namespace

bool LoadAchievements(XUID xuid, AchievementData& out, bool cardOnly)
{
    out = AchievementData();
    out.xuid = xuid;
    out.unlocked = out.achievements = out.earned = out.gamerscore = 0;
    out.gamerscoreSetting = out.playedSetting = out.earnedSetting = -1;
    out.hasRep = false;
    out.rep = 0;
    DWORD t0 = GetTickCount();

    Source src;
    if (SignedIn(xuid) || !ReadPackage(xuid, src.package))
    {
        char dir[64];
        _snprintf_s(dir, sizeof(dir), _TRUNCATE, "game:\\cache\\profiles\\%016I64X",
                    (unsigned __int64)xuid);
        src.dir = dir;
        src.mounted = GetFileAttributesA((src.dir + "\\FFFE07D1.gpd").c_str()) != (DWORD)-1;
        Log("achievements: profile %016I64X from the saved copy: %s", (unsigned __int64)xuid,
            src.mounted ? "there" : "none yet");
    }
    if (!src.mounted && src.package.empty())
    {
        Log("achievements: profile %016I64X can't be read", (unsigned __int64)xuid);
        return false;
    }

    std::vector<unsigned char> dash;
    std::vector<xdbf::Entry> es;
    bool ok = src.Read("FFFE07D1.gpd", dash) && xdbf::Entries(&dash[0], dash.size(), es);
    if (ok)
    {
        xdbf::SettingInt(es, 0x10040006, out.gamerscoreSetting);       // XPROFILE_GAMERCARD_CRED
        xdbf::SettingInt(es, 0x10040007, out.playedSetting);           // XPROFILE_GAMERCARD_TITLES_PLAYED
        xdbf::SettingInt(es, 0x10040008, out.earnedSetting);           // XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED
        out.hasRep = xdbf::SettingFloat(es, 0x5004000B, out.rep);      // XPROFILE_GAMERCARD_REP
        for (size_t i = 0; i < es.size(); i++)
        {
            AchTitle t;
            if (xdbf::ReadTitle(es[i], t.info) && t.info.achievements > 0)
                out.titles.push_back(t);
        }
        std::sort(out.titles.begin(), out.titles.end(), LastPlayedFirst);
        for (size_t ti = 0; ti < out.titles.size(); ti++)
        {
            AchTitle& t = out.titles[ti];
            out.unlocked += t.info.unlocked;
            out.achievements += t.info.achievements;
            out.earned += t.info.earned;
            out.gamerscore += t.info.gamerscore;
            if (cardOnly)
                continue;
            char name[16];
            _snprintf_s(name, sizeof(name), _TRUNCATE, "%08X.gpd", t.info.titleId);
            std::vector<unsigned char> g;
            std::vector<xdbf::Entry> ge;
            if (!src.Read(name, g) || !xdbf::Entries(&g[0], g.size(), ge))
                continue;
            if (const xdbf::Entry* tile = xdbf::Find(ge, xdbf::kImage, 0x8000))
                t.tile.assign(tile->data, tile->data + tile->size);
            for (size_t i = 0; i < ge.size(); i++)
            {
                AchItem item;
                if (!xdbf::ReadAchievement(ge[i], item.a))
                    continue;
                item.title = ti;
                if (item.a.Earned())
                    if (const xdbf::Entry* img = xdbf::Find(ge, xdbf::kImage, item.a.imageId))
                        item.png.assign(img->data, img->data + img->size);
                t.items.push_back(out.items.size());
                out.items.push_back(item);
            }
        }
    }
    Log("achievements: %u titles, %u achievements (%d/%d unlocked, %d/%d gamerscore), rep %s %.1f, in %u ms",
        (unsigned)out.titles.size(), (unsigned)out.items.size(), out.unlocked, out.achievements, out.earned,
        out.gamerscore, out.hasRep ? "set" : "none", out.rep, (unsigned)(GetTickCount() - t0));
    return ok;
}

AchievementData& CurrentAchievements()
{
    return g_data;
}

// The ids make the names unique per profile and title: XUI keeps textures by name.
std::wstring AchTitleLocator(size_t title)
{
    wchar_t b[64];
    _snwprintf_s(b, 64, _TRUNCATE, L"slopdash://ach/t/%u/%08X", (unsigned)title,
                 title < g_data.titles.size() ? g_data.titles[title].info.titleId : 0);
    return b;
}

std::wstring AchItemLocator(size_t item)
{
    wchar_t b[64];
    unsigned id = 0;
    if (item < g_data.items.size())
        id = (g_data.titles[g_data.items[item].title].info.titleId << 8) ^ g_data.items[item].a.id;
    _snwprintf_s(b, 64, _TRUNCATE, L"slopdash://ach/a/%u/%08X%08X", (unsigned)item, id,
                 (unsigned)(g_data.xuid & 0xFFFFFFFF));
    return b;
}

bool AchievementPng(const wchar_t* locator, const unsigned char** data, unsigned* size)
{
    static const wchar_t kTitle[] = L"slopdash://ach/t/", kItem[] = L"slopdash://ach/a/";
    const size_t n = sizeof(kTitle) / sizeof(kTitle[0]) - 1;
    const std::vector<unsigned char>* png = NULL;
    if (!wcsncmp(locator, kTitle, n))
    {
        size_t i = (size_t)_wtoi(locator + n);
        if (i < g_data.titles.size())
            png = &g_data.titles[i].tile;
    }
    else if (!wcsncmp(locator, kItem, n))
    {
        size_t i = (size_t)_wtoi(locator + n);
        if (i < g_data.items.size())
            png = &g_data.items[i].png;
    }
    if (!png || png->empty())
        return false;
    *data = &(*png)[0];
    *size = (unsigned)png->size();
    return true;
}
