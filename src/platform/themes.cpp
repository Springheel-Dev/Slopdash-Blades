#include "platform/themes.h"

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "platform/log.h"
#include "platform/settings.h"

namespace {

const char* const kDefaultKey = "theme.default";
const char* const kNextKey = "theme.next";      // a switch in progress (one run only)

std::vector<ThemeInfo> MakeThemes()
{
    std::vector<ThemeInfo> t;
    // One Blades theme: the launch dashboard's blades with
    // the Marketplace's added. "blades" (the 6717 build) is no longer listed;
    // a default still naming it falls back to this one.
    ThemeInfo launch = { "blades1888", L"Blades Homebrew",
                         L"The Xbox 360's Blades dashboard, with its Marketplace blade.", true };
    ThemeInfo nxe = { "nxe", L"New Xbox Experience",
                      L"The Xbox 360's 2008 dashboard, with avatars and its channel of tiles. Not built yet.", false };
    t.push_back(launch);
    t.push_back(nxe);
    return t;
}

std::string g_current;

std::string ReadValue(const char* key)
{
    return GetSetting(key);
}

bool WriteValue(const char* key, const std::string& value)
{
    return SetSetting(key, value);
}

}  // namespace

const std::vector<ThemeInfo>& Themes()
{
    static const std::vector<ThemeInfo> themes = MakeThemes();
    return themes;
}

const ThemeInfo* FindTheme(const std::string& id)
{
    const std::vector<ThemeInfo>& t = Themes();
    for (size_t i = 0; i < t.size(); i++)
        if (id == t[i].id)
            return &t[i];
    return NULL;
}

std::string DefaultTheme()
{
    std::string id = ReadValue(kDefaultKey);
    const ThemeInfo* t = FindTheme(id);
    return t && t->available ? id : "blades1888";      // the launch dashboard, unless chosen otherwise
}

bool SetDefaultTheme(const std::string& id)
{
    const ThemeInfo* t = FindTheme(id);
    if (!t || !t->available)
        return false;
    Log("themes: default is now %s", id.c_str());
    bool ok = WriteValue(kDefaultKey, id);
    SyncGuideSkin();                    // the Guide follows the default
    return ok;
}

// --- where each theme's dashboard files are -------------------------------------
std::string ThemeFolder(const std::string& id)
{
    std::string stick = std::string(kStickThemesDir) + "\\" + id;
    DWORD a = GetFileAttributesA(stick.c_str());
    if (a != (DWORD)-1 && (a & FILE_ATTRIBUTE_DIRECTORY))
        return stick;
    return std::string(kThemesDir) + "\\" + id;
}

std::string ThemePack(const std::string& id)
{
    const char* const dirs[] = { kStickThemesDir, kThemesDir };
    for (size_t i = 0; i < 2; i++)
    {
        std::string pack = std::string(dirs[i]) + "\\" + id + ".pak";
        DWORD a = GetFileAttributesA(pack.c_str());
        if (a != (DWORD)-1 && !(a & FILE_ATTRIBUTE_DIRECTORY))
            return pack;
    }
    return "";
}

// --- the system Guide's skin ----------------------------------------------------
namespace {

const char* const kGuideDir = "game:\\Guide";
const char* const kActiveSkin = "game:\\Guide\\skin.xzp";

std::string ThemeSkin(const std::string& id)
{
    return std::string(kGuideDir) + "\\" + id + "\\skin.xzp";
}

LONGLONG FileSize(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return -1;
    return ((LONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

}  // namespace

bool ThemeGuideAvailable(const std::string& id)
{
    return FileSize(ThemeSkin(id)) > 0;
}

bool ThemeGuideOn(const std::string& id)
{
    return ReadValue(("guide." + id).c_str()) != "system";
}

bool SetThemeGuide(const std::string& id, bool on)
{
    bool ok = WriteValue(("guide." + id).c_str(), on ? "" : "system");
    SyncGuideSkin();
    return ok;
}

// Which theme's Guide SlopGuide gives the console at its next start: the
// default theme's id, or "system" when that theme's Guide is turned off.
// (Blades (launch) has its own package, game:\Guide\blades1888.xzp.)
void WriteGuideTheme(const std::string& id)
{
    std::string value = ThemeGuideOn(id) ? id : "system";
    CreateDirectoryA(kGuideDir, NULL);
    HANDLE h = CreateFileA("game:\\Guide\\theme.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("guide: COULDN'T write theme.txt");
        return;
    }
    DWORD put = 0;
    WriteFile(h, value.c_str(), (DWORD)value.size(), &put, NULL);
    CloseHandle(h);
}

bool SyncGuideSkin()
{
    std::string id = DefaultTheme();
    WriteGuideTheme(id);
    std::string src = ThemeSkin(id);
    LONGLONG size = FileSize(src);
    if (size > 0 && ThemeGuideOn(id))
    {
        if (FileSize(kActiveSkin) == size)
            return true;                // in place (a theme's skin only changes with its size, in practice)
        CreateDirectoryA(kGuideDir, NULL);
        BOOL ok = CopyFileA(src.c_str(), kActiveSkin, FALSE);
        Log("guide: %s's skin %s (%I64d bytes)", id.c_str(), ok ? "put in place" : "COULDN'T be copied", size);
        return ok != FALSE;
    }
    if (FileSize(kActiveSkin) >= 0)
    {
        BOOL ok = DeleteFileA(kActiveSkin);
        Log("guide: the system's skin from the next restart (%s)%s", id.c_str(), ok ? "" : ": COULDN'T remove the old one");
    }
    return false;
}

std::string CurrentTheme()
{
    return g_current.empty() ? DefaultTheme() : g_current;
}

std::string StartupTheme()
{
    std::string next = ReadValue(kNextKey);
    if (!next.empty())
        WriteValue(kNextKey, "");       // a switch lasts one run; the default stays
    const ThemeInfo* t = FindTheme(next);
    g_current = t && t->available ? next : DefaultTheme();
    Log("themes: running %s (default %s)", g_current.c_str(), DefaultTheme().c_str());
    SyncGuideSkin();
    return g_current;
}

bool SwitchTheme(const std::string& id)
{
    const ThemeInfo* t = FindTheme(id);
    if (!t || !t->available)
        return false;
    if (id == CurrentTheme())
        return true;
    // Each theme is its own set of scenes and classes: SlopDash restarts into it.
    if (!WriteValue(kNextKey, id))
        return false;
    Log("themes: switching to %s", id.c_str());
    LogFlush();
    XLaunchNewImage("game:\\SlopDash.xex", 0);
    return false;                       // only reached if the relaunch failed
}
