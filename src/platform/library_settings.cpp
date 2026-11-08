#include "platform/library_settings.h"

#include <xtl.h>
#include <stdio.h>

#include "platform/log.h"
#include "platform/scanner.h"
#include "platform/settings.h"

namespace {

const char* const kPathsKey = "library.path";
const char* const kViewKey = "library.view";        // (before 0.0.121: one view for every library)
bool g_changed = false;

std::vector<LibraryView> MakeViews()
{
    // The views: one List with its panel's choice, one Grid
    // of any size up to 5 x 10, Tiles, Index; no Carousel (it lagged), no As It
    // Shipped (1888's own Played Games list: it didn't work yet).
    std::vector<LibraryView> v;
    LibraryView list = { "list", L"List",
                         L"A list of your games, and beside it the highlighted game's description or its cover art.",
                         true };
    LibraryView flow = { "coverflow", L"Cover Flow", L"Game covers side by side, sliding past as you move.", true };
    LibraryView grid = { "grid", L"Grid",
                         L"Covers in rows, as many as you like: up to 5 rows of 10.", true };
    LibraryView tiles = { "tiles", L"Tiles",
                          L"Rows of covers that roll up and down with you as you move through the library.",
                          true };
    LibraryView marquee = { "marquee", L"Marquee", L"One game at a time, big, with its artwork.", true };
    LibraryView index = { "index", L"Index",
                          L"Just the names, small, in columns: the whole library at a glance.",
                          true };
    // The user's, 2026-11-02: the 2005 "Select a Game" screen.
    LibraryView launch = { "launch", L"Launch",
                           L"Your games with their covers, and the highlighted game's screenshot and story beside "
                           L"them, as the Xbox 360 showed games in 2005.",
                           true };
    v.push_back(list);
    v.push_back(flow);
    v.push_back(grid);
    v.push_back(tiles);
    v.push_back(marquee);
    v.push_back(index);
    v.push_back(launch);
    return v;
}

// The open library's own setting: library.view.games, library.view.arcade...
std::string ScopedKey(const char* key)
{
    static const char* const kScopes[] = { "all", "arcade", "games", "apps", "emulators" };    // (LibraryScope order)
    int s = (int)CurrentLibraryScope();
    return std::string(key) + "." + kScopes[s >= 0 && s < 5 ? s : 0];
}

std::string Scoped(const char* key)
{
    std::string v = GetSetting(ScopedKey(key).c_str());
    return v.empty() ? GetSetting(key) : v;         // (the one setting there was before, until changed)
}

bool SetScoped(const char* key, const std::string& value)
{
    return SetSetting(ScopedKey(key).c_str(), value);
}

}  // namespace

std::vector<std::string> DefaultScanPaths()
{
    std::vector<std::string> p;
    p.push_back("hdd:\\Content\\0000000000000000");
    p.push_back("hdd:\\Games");
    p.push_back("usb:\\Content\\0000000000000000");
    p.push_back("usb:\\Games");
    return p;
}

std::vector<std::string> ScanPaths()
{
    bool set = false;
    std::vector<std::string> p = GetSettingList(kPathsKey, &set);
    return set ? p : DefaultScanPaths();
}

bool SetScanPaths(const std::vector<std::string>& paths)
{
    g_changed = true;
    Log("library: %u scan paths", (unsigned)paths.size());
    return SetSettingList(kPathsKey, paths);
}

bool TakeLibraryChanged()
{
    bool c = g_changed;
    g_changed = false;
    return c;
}

const std::vector<LibraryView>& LibraryViews()
{
    static const std::vector<LibraryView> views = MakeViews();
    return views;
}

std::string CurrentLibraryView()
{
    std::string id = Scoped(kViewKey);
    // None chosen: Launch.
    if (id.empty() || id == "default")
        return "launch";
    // The views before the current set, as their new ones.
    if (id == "listcover" || id == "shipped")
        return "list";
    if (id == "carousel")
        return "coverflow";
    if (id == "grid37")
        return "grid";
    const std::vector<LibraryView>& v = LibraryViews();
    for (size_t i = 0; i < v.size(); i++)
        if (id == v[i].id && v[i].available)
            return id;
    return "launch";
}

bool SetLibraryView(const std::string& id)
{
    const std::vector<LibraryView>& v = LibraryViews();
    for (size_t i = 0; i < v.size(); i++)
        if (id == v[i].id && v[i].available)
            return SetScoped(kViewKey, id);
    return false;
}

bool ListShowsCover()
{
    std::string v = Scoped("library.listpanel");
    return v == "cover" || (v.empty() && Scoped(kViewKey) == "listcover");     // (List V2 before)
}

bool SetListShowsCover(bool cover)
{
    return SetScoped("library.listpanel", cover ? "cover" : "description");
}

void LibraryGridSize(int& rows, int& cols)
{
    rows = 2;                                       // (the old Grid [2x5]; Grid [3x7] before: 3 x 7)
    cols = 5;
    std::string v = Scoped("library.grid");
    if (v.empty() && Scoped(kViewKey) == "grid37")
        rows = 3, cols = 7;
    else if (v.size() >= 3)
        sscanf_s(v.c_str(), "%dx%d", &rows, &cols);
    rows = rows < 1 ? 1 : rows > kGridMaxRows ? kGridMaxRows : rows;
    cols = cols < 1 ? 1 : cols > kGridMaxCols ? kGridMaxCols : cols;
}

bool SetLibraryGridSize(int rows, int cols)
{
    char b[16];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%dx%d", rows, cols);
    return SetScoped("library.grid", b);
}

void LibraryTilesSize(int& rows, int& cols)
{
    rows = 3;
    cols = 7;
    std::string v = Scoped("library.tiles");
    if (v.size() >= 3)
        sscanf_s(v.c_str(), "%dx%d", &rows, &cols);
    rows = rows < 1 ? 1 : rows > kGridMaxRows ? kGridMaxRows : rows;
    cols = cols < 1 ? 1 : cols > kGridMaxCols ? kGridMaxCols : cols;
}

bool SetLibraryTilesSize(int rows, int cols)
{
    char b[16];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%dx%d", rows, cols);
    return SetScoped("library.tiles", b);
}

bool IndexIcons()
{
    return Scoped("library.indexicons") == "1";
}

bool SetIndexIcons(bool on)
{
    return SetScoped("library.indexicons", on ? "1" : "0");
}

std::string LibraryViewStamp()
{
    int rows = 0, cols = 0, trows = 0, tcols = 0;
    LibraryGridSize(rows, cols);
    LibraryTilesSize(trows, tcols);
    char b[96];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%s|%d|%dx%d|%dx%d|%d", CurrentLibraryView().c_str(), ListShowsCover() ? 1 : 0,
                rows, cols, trows, tcols, IndexIcons() ? 1 : 0);
    return b;
}

namespace {

std::string NameKey(unsigned titleId)
{
    char k[40];
    _snprintf_s(k, sizeof(k), _TRUNCATE, "library.name.%08X", titleId);
    return k;
}

}  // namespace

// Stored as UTF-8 (the settings file is plain text).
std::wstring CustomGameName(unsigned titleId)
{
    std::string v = GetSetting(NameKey(titleId).c_str());
    if (v.empty())
        return L"";
    wchar_t buf[256];
    int n = MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, buf, 256);
    return n > 0 ? std::wstring(buf) : L"";
}

bool SetCustomGameName(unsigned titleId, const std::wstring& name)
{
    char buf[512] = { 0 };
    if (!name.empty())
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, buf, sizeof(buf), NULL, NULL);
    g_changed = true;
    return SetSetting(NameKey(titleId).c_str(), buf);
}

void MarkLibraryChanged()
{
    g_changed = true;
}
