#include "platform/scanner.h"
#include "platform/library_settings.h"

#include <xtl.h>
#include <algorithm>
#include <map>
#include <set>

#include "core/catalog.h"
#include "core/god.h"
#include "platform/fetcher.h"
#include "platform/log.h"

namespace {

std::vector<library::Game> g_all;      // everything the scan found
std::vector<library::Game> g_games;    // what the library shows now (its scope)
std::vector<size_t> g_allIndex;        // each of those, in g_all
LibraryScope g_scope = kScopeAll;

// The walk's own state, one walk at a time (start-up and X Refresh on the UI
// thread, a plugged-in drive's on a thread of its own): what it has found so
// far, where it is, and the title IDs it has listed.
CRITICAL_SECTION g_walkLock;
struct WalkLockInit
{
    WalkLockInit() { InitializeCriticalSection(&g_walkLock); }
} g_walkLockInit;
std::vector<library::Game>* g_found = NULL;
int g_place = kPlaceGames;          // where the walk is (Add marks each entry with it)
std::set<unsigned> g_seen;          // title IDs already listed (multi-disc GOD, copies)

// A drive plugged in or out: the scan made again on a thread of its own, and
// taken on by the UI thread (TakeBackgroundScan).
volatile LONG g_bgScan = 0;         // 0 idle, 1 scanning, 2 done (waiting to be taken)
std::vector<library::Game> g_bgFound;
volatile LONG g_libraryReplaced = 0;
size_t g_replacedFrom = 0;          // how many the library showed before

bool ReadHead(const std::string& path, size_t want, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL), got = 0;
    if (want == 0 || want > size)
        want = size;
    out.resize(want);
    BOOL ok = want == 0 || (ReadFile(h, &out[0], (DWORD)want, &got, NULL) && got == want);
    CloseHandle(h);
    return ok != FALSE;
}

// Part of a file: length bytes from offset.
bool ReadRange(const std::string& path, size_t offset, size_t length, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD got = 0;
    out.resize(length);
    BOOL ok = SetFilePointer(h, (LONG)offset, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && length &&
              ReadFile(h, &out[0], (DWORD)length, &got, NULL) && got == length;
    CloseHandle(h);
    return ok != FALSE;
}

// A Games on Demand install's data files (its header's "<name>.data"
// folder), kept open while its disc image is read.
struct GodFiles
{
    std::string dir;
    std::map<int, HANDLE> open;
    ~GodFiles()
    {
        for (std::map<int, HANDLE>::iterator i = open.begin(); i != open.end(); ++i)
            CloseHandle(i->second);
    }
};

bool ReadGodData(void* context, int file, size_t offset, size_t n, unsigned char* out)
{
    GodFiles& g = *(GodFiles*)context;
    std::map<int, HANDLE>::iterator i = g.open.find(file);
    if (i == g.open.end())
    {
        char name[16];
        _snprintf_s(name, sizeof(name), _TRUNCATE, "\\Data%04d", file);
        HANDLE h = CreateFileA((g.dir + name).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        i = g.open.insert(std::make_pair(file, h)).first;
    }
    DWORD got = 0;
    return SetFilePointer(i->second, (LONG)offset, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
           ReadFile(i->second, out, (DWORD)n, &got, NULL) && got == n;
}

// A GOD install whose header has no image (Assassin's Creed II's, Batman's:
// sizes given, the bytes zero): the game's own default.xex has it (its title
// resource), read from the disc image (core/god) once and kept in
// game:\cache\icons\<TitleID>.png; an empty file there: it has none.
void GodIcon(const std::string& header, library::Game& g)
{
    if (!g.titleId)
        return;
    char cache[64];
    _snprintf_s(cache, sizeof(cache), _TRUNCATE, "game:\\cache\\icons\\%08X.png", g.titleId);
    std::vector<unsigned char> png;
    if (ReadHead(cache, 0, png))
    {
        g.icon = png;
        return;
    }
    DWORD t0 = GetTickCount();
    GodFiles files;
    files.dir = header + ".data";
    std::vector<unsigned char> xex;
    library::Game x;
    if (god::ReadRootFile(ReadGodData, &files, "default.xex", 96u << 20, xex) && !xex.empty() &&
        library::ReadXex(&xex[0], xex.size(), x))
        png = x.icon;
    CreateDirectoryA("game:\\cache", NULL);
    CreateDirectoryA("game:\\cache\\icons", NULL);
    HANDLE h = CreateFileA(cache, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD wrote = 0;
        if (!png.empty())
            WriteFile(h, &png[0], (DWORD)png.size(), &wrote, NULL);
        CloseHandle(h);
    }
    Log("library: %S: %s from its default.xex (%u KB, %u ms)", g.name.c_str(), png.empty() ? "no icon" : "icon",
        (unsigned)(xex.size() / 1024), (unsigned)(GetTickCount() - t0));
    g.icon = png;
}

void Add(library::Game& g, const std::string& path)
{
    g.place = g_place;
    // Games: one entry per title ID (multi-disc GOD installs, copies). Homebrew
    // without a title ID (360sweeper...) is listed by its path.
    if (g_place == kPlaceGames && g.titleId)
    {
        if (g_seen.count(g.titleId))
            return;
        g_seen.insert(g.titleId);
    }
    if (g.kind != library::kArcade && g.contentType == 0 && (g.name.empty() || g.name.size() == 8))
    {
        // An executable without a name of its own (homebrew, apps, emulators):
        // listed by its folder.
        std::string dir = path.substr(0, path.find_last_of('\\'));
        std::string folder = dir.substr(dir.find_last_of('\\') + 1);
        // (in a folder of the same name plus a version - the store's
        // Games\360sweeper\360sweeperv1.0.1 - the outer folder's name)
        std::string up = dir.substr(0, dir.find_last_of('\\'));
        std::string outer = up.substr(up.find_last_of('\\') + 1);
        if (outer.size() >= 3 && folder.size() > outer.size() && !_strnicmp(folder.c_str(), outer.c_str(), outer.size()))
            folder = outer;
        bool hexName = g.name.size() == 8 && wcsspn(g.name.c_str(), L"0123456789ABCDEFabcdef") == 8;
        if (g.name.empty() || hexName)
            g.name = std::wstring(folder.begin(), folder.end());
    }
    // An Original Xbox game in a folder of its own name ("Splinter Cell - Chaos
    // Theory.xiso"): that name, fuller than the XBE's own ("Splinter Cell-CT
    // vs"). The official one, when xdb has it, comes later (RenameGames).
    if (g.kind == library::kXboxOriginal)
    {
        std::string dir = path.substr(0, path.find_last_of('\\'));
        std::string folder = dir.substr(dir.find_last_of('\\') + 1);
        if (folder.size() > 5 && !_stricmp(folder.c_str() + folder.size() - 5, ".xiso"))
        {
            std::wstring named = catalog::FolderTitle(folder);
            if (!named.empty())
                g.name = named;
        }
    }
    g.path = path;
    std::wstring custom = CustomGameName(g.titleId);    // Game Settings > Rename
    if (!custom.empty())
        g.name = custom;
    if (g.name.empty())
    {
        wchar_t buf[16];
        _snwprintf_s(buf, 16, _TRUNCATE, L"%08X", g.titleId);
        g.name = buf;
    }
    g_found->push_back(g);
}

bool IsStfsFolder(const char* name)
{
    return !_stricmp(name, "00007000") || !_stricmp(name, "000D0000") || !_stricmp(name, "00005000") ||
           !_stricmp(name, "00080000");
}

// An executable's header (the first 4 KB is plenty): its title ID (execution
// info, key 0x40006), and whether it's a DLL (module flags bit 0x8: a
// DashLaunch plugin, not something to launch).
bool XexHeader(const std::vector<unsigned char>& data, unsigned& titleId, bool& dll)
{
    if (data.size() < 24 || memcmp(&data[0], "XEX2", 4))
        return false;
    unsigned flags = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    dll = (flags & 0x8) != 0;
    unsigned count = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    for (unsigned k = 0; k < count && 24 + k * 8 + 8 <= data.size(); k++)
    {
        const unsigned char* e = &data[24 + k * 8];
        unsigned key = (e[0] << 24) | (e[1] << 16) | (e[2] << 8) | e[3];
        unsigned at = (e[4] << 24) | (e[5] << 16) | (e[6] << 8) | e[7];
        if (key == 0x40006 && at + 0x10 <= data.size())
            titleId = (data[at + 0xC] << 24) | (data[at + 0xD] << 16) | (data[at + 0xE] << 8) | data[at + 0xF];
    }
    return true;
}

std::string Plain(const std::string& s)       // lower case, letters and digits only
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
        if (isalnum((unsigned char)s[i]))
            out += (char)tolower((unsigned char)s[i]);
    return out;
}

// An app's or emulator's folder: the executable it starts with. Homebrew
// seldom calls it default.xex (Aurora.xex, XM360.xex...): default.xex, else
// the one named like the folder, else the only one, else none (a folder of
// many, e.g. emulator cores, without a clear main one). Plugins (DLLs) never.
void AddApp(const std::string& dir, const std::vector<std::string>& xexes, const std::vector<unsigned>& sizes)
{
    std::string folder = Plain(dir.substr(dir.find_last_of('\\') + 1));
    int pick = -1;
    for (size_t i = 0; i < xexes.size() && pick < 0; i++)
        if (!_stricmp(xexes[i].c_str(), "default.xex"))
            pick = (int)i;
    for (size_t i = 0; i < xexes.size() && pick < 0; i++)
        if (Plain(xexes[i].substr(0, xexes[i].size() - 4)) == folder)
            pick = (int)i;
    if (pick < 0 && xexes.size() == 1)
        pick = 0;
    // Never SlopDash itself (Apps\SlopDash on the console), nor a retail
    // dashboard (dash.xex: the 6717 Blades SlopDash reads its art from, which
    // froze the console when launched from here, 2026-10-07).
    if (pick < 0 || !_stricmp(xexes[pick].c_str(), "SlopDash.xex") || !_stricmp(xexes[pick].c_str(), "SlopDash.xex") ||
        !_stricmp(xexes[pick].c_str(), "dash.xex"))
        return;
    std::string full = dir + "\\" + xexes[pick];
    std::vector<unsigned char> data;
    library::Game g;
    unsigned tid = 0;
    bool dll = false;
    // The whole header (its size is the image's offset, bytes 8-11).
    if (!ReadHead(full, 0x1000, data) || data.size() < 12)
        return;
    unsigned headerSize = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
    if (headerSize > data.size() && headerSize <= 0x40000)
        ReadHead(full, headerSize, data);
    if (!XexHeader(data, tid, dll) || dll)
        return;
    // Its own name and icon when the file is small enough to read whole at
    // start-up (Aurora's 12 MB alone took 2 s from USB); its folder names it
    // otherwise.
    if (sizes[pick] <= (g_place == kPlaceGames ? 16u * 1024 * 1024 : 1536u * 1024) && ReadHead(full, 0, data) &&
        !data.empty())
        library::ReadXex(&data[0], data.size(), g);
    g.titleId = tid;
    g.kind = library::kXbox360;
    Add(g, full);
}

// Depth-limited walk; a folder holding a default.xex / default.xbe is a game
// (in the apps and emulators folders, one with its main executable is an app)
// and isn't searched further.
void Walk(const std::string& dir, int depth, bool stfsFolder)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::string> subdirs, xexes;
    std::vector<unsigned> sizes;
    bool isGame = false;
    do
    {
        std::string name = fd.cFileName;
        if (name == "." || name == ".." || name[0] == '.')
            continue;
        std::string full = dir + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            subdirs.push_back(name);
            continue;
        }
        std::vector<unsigned char> data;
        library::Game g;
        if (stfsFolder)
        {
            if (ReadHead(full, library::kStfsHeaderBytes, data) && !data.empty() &&
                library::ReadStfs(&data[0], data.size(), g))
            {
                if (g.icon.empty() && g.contentType == 0x7000)
                    GodIcon(full, g);
                Add(g, full);
            }
        }
        else if (g_place != kPlaceGames ||
                 (_stricmp(name.c_str(), "default.xex") && name.size() > 4 &&
                  !_stricmp(name.c_str() + name.size() - 4, ".xex")))
        {
            // (apps and emulators: any executable; games: homebrew not called
            // default.xex, which the store installs as its archive has it)
            if (name.size() > 4 && !_stricmp(name.c_str() + name.size() - 4, ".xex"))
            {
                xexes.push_back(name);
                sizes.push_back(fd.nFileSizeHigh ? 0xFFFFFFFF : fd.nFileSizeLow);
            }
        }
        else if (!_stricmp(name.c_str(), "default.xex"))
        {
            isGame = true;
            if (ReadHead(full, 0, data) && !data.empty() && library::ReadXex(&data[0], data.size(), g))
                Add(g, full);
        }
        else if (!_stricmp(name.c_str(), "default.xbe"))
        {
            isGame = true;
            if (ReadHead(full, 0x10000, data) && !data.empty() && library::ReadXbe(&data[0], data.size(), g))
            {
                // its title image ($$XTIMAGE), as the original dashboard shows it
                size_t at = 0, length = 0;
                std::vector<unsigned char> image;
                if (library::XbeTitleImage(&data[0], data.size(), at, length) && ReadRange(full, at, length, image))
                    library::IconFromXpr(&image[0], image.size(), g.icon);
                Add(g, full);
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (!xexes.empty() && !isGame)
    {
        size_t before = g_found->size();
        AddApp(dir, xexes, sizes);
        isGame = g_found->size() != before;
    }
    if (isGame || depth <= 0)
        return;
    for (size_t i = 0; i < subdirs.size(); i++)
        Walk(dir + "\\" + subdirs[i], depth - 1, IsStfsFolder(subdirs[i].c_str()));
}

bool ByName(const library::Game& a, const library::Game& b)
{
    return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
}

bool InScope(const library::Game& g)
{
    if (g_scope == kScopeApps)
        return g.place == kPlaceApps;
    if (g_scope == kScopeEmulators)
        return g.place == kPlaceEmulators;
    if (g.place != kPlaceGames)
        return false;
    return g_scope == kScopeAll || (g_scope == kScopeArcade) == (g.kind == library::kArcade);
}

void ApplyScope()
{
    g_games.clear();
    g_allIndex.clear();
    for (size_t i = 0; i < g_all.size(); i++)
        if (InScope(g_all[i]))
        {
            g_games.push_back(g_all[i]);
            g_allIndex.push_back(i);
        }
}

// --- Pictures from elsewhere ------------------------------------
// For a game with no picture of its own, after the walk: a picture beside it
// (an extracted game's or homebrew's folder: a cover, box art or icon file, or
// else any one picture there), then the title's own tile from another package
// with its title ID (a title update, downloadable content, a saved game: each
// STFS header carries the title's picture). With none, the views show 1888's
// games icon (main.cpp TextureLoader).

bool IsPicture(const std::string& name, bool* png)
{
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    const char* ext = name.c_str() + dot;
    *png = !_stricmp(ext, ".png");
    return *png || !_stricmp(ext, ".jpg") || !_stricmp(ext, ".jpeg");
}

bool PictureBeside(library::Game& g)
{
    size_t slash = g.path.find_last_of('\\');
    size_t n = g.path.size();
    bool executable = n > 4 && (!_stricmp(g.path.c_str() + n - 4, ".xex") || !_stricmp(g.path.c_str() + n - 4, ".xbe"));
    if (!executable || slash == std::string::npos)
        return false;
    std::string dir = g.path.substr(0, slash);
    static const char* const kNames[] = { "cover", "boxart", "front", "folder", "icon", "default", "title", "logo" };
    std::string best, any;
    int bestRank = 99;
    bool bestPng = false, anyPng = false;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    do
    {
        bool png = false;
        std::string name = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name[0] == '.' || fd.nFileSizeHigh ||
            fd.nFileSizeLow > 4 * 1024 * 1024 || !IsPicture(name, &png))
            continue;
        std::string stem = name.substr(0, name.find_last_of('.'));
        for (int r = 0; r < (int)(sizeof(kNames) / sizeof(kNames[0])); r++)
            if (!_stricmp(stem.c_str(), kNames[r]) && r < bestRank)
            {
                bestRank = r;
                best = dir + "\\" + name;
                bestPng = png;
            }
        if (any.empty())
        {
            any = dir + "\\" + name;
            anyPng = png;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::string file = best.empty() ? any : best;
    bool png = best.empty() ? anyPng : bestPng;
    if (file.empty())
        return false;
    if (png && ReadHead(file, 0, g.icon) && !g.icon.empty())
        return true;
    g.icon.clear();
    g.art = file;                       // (a JPEG: XUI's own loader reads it, by path)
    return true;
}

bool TileElsewhere(library::Game& g)
{
    if (!g.titleId)
        return false;
    char tid[12];
    _snprintf_s(tid, sizeof(tid), _TRUNCATE, "%08X", g.titleId);
    static const char* const kRoots[] = { "hdd:\\Content", "usb:\\Content" };
    int looked = 0;
    for (size_t r = 0; r < sizeof(kRoots) / sizeof(kRoots[0]); r++)
    {
        WIN32_FIND_DATAA owner;
        HANDLE ho = FindFirstFileA((std::string(kRoots[r]) + "\\*").c_str(), &owner);
        if (ho == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if (!(owner.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || strlen(owner.cFileName) != 16)
                continue;
            std::string title = std::string(kRoots[r]) + "\\" + owner.cFileName + "\\" + tid;
            WIN32_FIND_DATAA type;
            HANDLE ht = FindFirstFileA((title + "\\*").c_str(), &type);
            if (ht == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                if (!(type.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || type.cFileName[0] == '.')
                    continue;
                std::string kind = title + "\\" + type.cFileName;
                WIN32_FIND_DATAA pkg;
                HANDLE hp = FindFirstFileA((kind + "\\*").c_str(), &pkg);
                if (hp == INVALID_HANDLE_VALUE)
                    continue;
                do
                {
                    if ((pkg.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || pkg.cFileName[0] == '.' || ++looked > 40)
                        continue;
                    std::string file = kind + "\\" + pkg.cFileName;
                    std::vector<unsigned char> head;
                    library::ContentHeader c;
                    if (_stricmp(file.c_str(), g.path.c_str()) && ReadHead(file, library::kStfsHeaderBytes, head) &&
                        !head.empty() && library::ReadContentHeader(&head[0], head.size(), c) &&
                        c.titleId == g.titleId && !c.titleIcon.empty())
                    {
                        g.icon = c.titleIcon;
                        FindClose(hp);
                        FindClose(ht);
                        FindClose(ho);
                        Log("library: %S: its picture from %s", g.name.c_str(), file.c_str());
                        return true;
                    }
                } while (FindNextFileA(hp, &pkg));
                FindClose(hp);
            } while (FindNextFileA(ht, &type));
            FindClose(ht);
        } while (FindNextFileA(ho, &owner));
        FindClose(ho);
    }
    return false;
}

// --- The library as the last scan found it --------------------------------------------
// game:\cache\library.bin: the list, icons and all, so start-up shows it at
// once and scans the drives in the background (the scan took 5-6 s of
// start-up with 67 games, 2026-11-08). Written after each scan.
const char kLibraryCache[] = "game:\\cache\\library.bin";
const unsigned kLibraryMagic = 0x53444C31;     // "SDL1"

void PutU32(std::string& out, unsigned v)
{
    out.append((const char*)&v, 4);
}

void PutBytes(std::string& out, const void* p, size_t n)
{
    PutU32(out, (unsigned)n);
    if (n)
        out.append((const char*)p, n);
}

struct CacheReader
{
    const unsigned char* p;
    const unsigned char* end;
    bool ok;
    bool U32(unsigned& v)
    {
        if (!ok || end - p < 4)
            return ok = false;
        memcpy(&v, p, 4);
        p += 4;
        return true;
    }
    bool Bytes(const unsigned char*& data, unsigned& n)
    {
        if (!U32(n) || (size_t)(end - p) < n)
            return ok = false;
        data = p;
        p += n;
        return true;
    }
    void Str(std::string& s)
    {
        const unsigned char* d;
        unsigned n;
        if (Bytes(d, n))
            s.assign((const char*)d, n);
    }
    void WStr(std::wstring& s)
    {
        const unsigned char* d;
        unsigned n;
        if (Bytes(d, n) && n % sizeof(wchar_t) == 0)
            s.assign((const wchar_t*)d, n / sizeof(wchar_t));
    }
};

void SaveLibraryCache(const std::vector<library::Game>& games)
{
    std::string out;
    PutU32(out, kLibraryMagic);
    PutU32(out, (unsigned)games.size());
    for (size_t i = 0; i < games.size(); i++)
    {
        const library::Game& g = games[i];
        PutU32(out, (unsigned)g.kind);
        PutU32(out, g.titleId);
        PutU32(out, g.contentType);
        PutU32(out, (unsigned)g.place);
        PutBytes(out, g.name.data(), g.name.size() * sizeof(wchar_t));
        PutBytes(out, g.publisher.data(), g.publisher.size() * sizeof(wchar_t));
        PutBytes(out, g.description.data(), g.description.size() * sizeof(wchar_t));
        PutBytes(out, g.icon.empty() ? NULL : &g.icon[0], g.icon.size());
        PutBytes(out, g.art.data(), g.art.size());
        PutBytes(out, g.path.data(), g.path.size());
    }
    CreateDirectoryA("game:\\cache", NULL);
    std::string tmp = std::string(kLibraryCache) + ".new";
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, out.data(), (DWORD)out.size(), &wrote, NULL) && wrote == out.size();
    CloseHandle(h);
    if (ok)
    {
        DeleteFileA(kLibraryCache);
        ok = MoveFileA(tmp.c_str(), kLibraryCache);
    }
    Log("library: %u games kept in %s (%u bytes)%s", (unsigned)games.size(), kLibraryCache, (unsigned)out.size(),
        ok ? "" : ": FAILED");
}

bool LoadLibraryCache(std::vector<library::Game>& games)
{
    std::vector<unsigned char> data;
    if (!ReadHead(kLibraryCache, 0, data) || data.size() < 8)
        return false;
    CacheReader r = { &data[0], &data[0] + data.size(), true };
    unsigned magic = 0, count = 0;
    if (!r.U32(magic) || magic != kLibraryMagic || !r.U32(count) || count > 100000)
        return false;
    std::vector<library::Game> out(count);
    for (unsigned i = 0; i < count && r.ok; i++)
    {
        library::Game& g = out[i];
        unsigned kind = 0, place = 0;
        r.U32(kind);
        r.U32(g.titleId);
        r.U32(g.contentType);
        r.U32(place);
        g.kind = (library::Kind)kind;
        g.place = (int)place;
        r.WStr(g.name);
        r.WStr(g.publisher);
        r.WStr(g.description);
        const unsigned char* icon;
        unsigned n;
        if (r.Bytes(icon, n))
            g.icon.assign(icon, icon + n);
        r.Str(g.art);
        r.Str(g.path);
    }
    if (!r.ok)
        return false;
    games.swap(out);
    return true;
}

// The drives walked, everything found put in `found`, by name.
void WalkAll(std::vector<library::Game>& found)
{
    EnterCriticalSection(&g_walkLock);
    DWORD t0 = GetTickCount();
    g_found = &found;
    g_seen.clear();
    // Games Library > Y > Scan Paths (default: the content and Games folders, HDD and USB).
    std::vector<std::string> roots = ScanPaths();
    for (size_t i = 0; i < roots.size(); i++)
    {
        // A path chosen right down to a content-type folder (e.g. ...\00007000) holds packages itself.
        size_t slash = roots[i].find_last_of('\\');
        std::string last = slash == std::string::npos ? roots[i] : roots[i].substr(slash + 1);
        Walk(roots[i], 4, IsStfsFolder(last.c_str()));
    }
    // Applications and Emulators (Media > Applications, Games > Emulators):
    // the Apps and Emulators folders, where the store installs them.
    static const struct { const char* root; int place; } kMore[] = {
        { "hdd:\\Apps", kPlaceApps }, { "usb:\\Apps", kPlaceApps },
        { "hdd:\\Emulators", kPlaceEmulators }, { "usb:\\Emulators", kPlaceEmulators } };
    for (size_t i = 0; i < sizeof(kMore) / sizeof(kMore[0]); i++)
    {
        g_place = kMore[i].place;
        Walk(kMore[i].root, 2, false);
    }
    g_place = kPlaceGames;
    g_found = NULL;
    for (size_t i = 0; i < found.size(); i++)
        if (found[i].icon.empty() && !PictureBeside(found[i]))
            TileElsewhere(found[i]);
    std::sort(found.begin(), found.end(), ByName);
    Log("library: %u games in %u ms", (unsigned)found.size(), (unsigned)(GetTickCount() - t0));
    for (size_t i = 0; i < found.size(); i++)
        Log("  %08X %-16s %S (%u-byte icon) %s", found[i].titleId, library::KindName(found[i].kind),
            found[i].name.c_str(), (unsigned)found[i].icon.size(), found[i].path.c_str());
    LeaveCriticalSection(&g_walkLock);
}

DWORD WINAPI BackgroundScan(LPVOID)
{
    std::vector<library::Game> found;
    WalkAll(found);
    SaveLibraryCache(found);
    g_bgFound.swap(found);
    // The fetcher follows g_all from its own thread: stopped here (it can
    // take a while, a request in flight), started again once the list is
    // taken on.
    for (int tries = 0; !StopFetcher(); tries++)
        if (tries == 0)
            Log("library: the fetcher didn't stop; the new scan waits");
    InterlockedExchange(&g_bgScan, 2);
    return 0;
}

}  // namespace

void ScanLibrary()
{
    std::vector<library::Game> found;
    WalkAll(found);
    SaveLibraryCache(found);
    g_all.swap(found);
    ApplyScope();
}

void StartupLibrary()
{
    std::vector<library::Game> found;
    DWORD t0 = GetTickCount();
    if (!LoadLibraryCache(found))
    {
        Log("library: no list kept yet (%s): the drives are scanned when the library opens", kLibraryCache);
        return;
    }
    g_all.swap(found);
    ApplyScope();
    Log("library: %u games as the last scan found them (%u ms)", (unsigned)g_all.size(),
        (unsigned)(GetTickCount() - t0));
}

bool StartBackgroundScan()
{
    if (InterlockedCompareExchange(&g_bgScan, 1, 0) != 0)
        return false;
    HANDLE t = CreateThread(NULL, 0, BackgroundScan, NULL, 0, NULL);
    if (!t)
    {
        InterlockedExchange(&g_bgScan, 0);
        return false;
    }
    CloseHandle(t);
    Log("library: scanning the drives in the background");
    return true;
}

bool BackgroundScanBusy()
{
    return g_bgScan != 0;
}

bool TakeBackgroundScan(int& added, int& removed)
{
    if (g_bgScan != 2)
        return false;
    std::set<std::string> before, after;
    for (size_t i = 0; i < g_all.size(); i++)
        before.insert(g_all[i].path);
    for (size_t i = 0; i < g_bgFound.size(); i++)
        after.insert(g_bgFound[i].path);
    added = removed = 0;
    for (std::set<std::string>::const_iterator i = after.begin(); i != after.end(); ++i)
        added += before.count(*i) ? 0 : 1;
    for (std::set<std::string>::const_iterator i = before.begin(); i != before.end(); ++i)
        removed += after.count(*i) ? 0 : 1;
    bool first = before.empty();        // (the first scan: every game is "new", no toast)
    g_replacedFrom = g_games.size();
    g_all.swap(g_bgFound);
    g_bgFound.clear();
    ApplyScope();
    StartFetcher();
    InterlockedExchange(&g_libraryReplaced, 1);
    InterlockedExchange(&g_bgScan, 0);
    Log("library: background scan taken: %d new, %d gone", added, removed);
    if (first)
        added = 0;
    return true;
}

bool TakeLibraryReplaced(size_t& shownBefore)
{
    shownBefore = g_replacedFrom;
    return InterlockedExchange(&g_libraryReplaced, 0) != 0;
}

void MarkLibraryReplaced()
{
    g_replacedFrom = g_games.size();
    InterlockedExchange(&g_libraryReplaced, 1);
}

namespace {

struct ByNameAt
{
    const std::vector<library::Game>* games;
    bool operator()(size_t a, size_t b) const { return ByName((*games)[a], (*games)[b]); }
};

}  // namespace

std::vector<size_t> RenameGames(const std::vector<std::wstring>& names)
{
    std::vector<size_t> from(g_all.size());
    for (size_t i = 0; i < from.size(); i++)
        from[i] = i;
    unsigned renamed = 0;
    for (size_t i = 0; i < g_all.size() && i < names.size(); i++)
        if (!names[i].empty() && names[i] != g_all[i].name && CustomGameName(g_all[i].titleId).empty())
        {
            g_all[i].name = names[i];
            renamed++;
        }
    if (!renamed)
        return from;
    ByNameAt by = { &g_all };
    std::stable_sort(from.begin(), from.end(), by);
    std::vector<library::Game> sorted;
    sorted.reserve(g_all.size());
    for (size_t i = 0; i < from.size(); i++)
        sorted.push_back(g_all[from[i]]);
    g_all.swap(sorted);
    ApplyScope();
    Log("library: %u official names", renamed);
    return from;
}

void SetLibraryScope(LibraryScope scope)
{
    // The first time a library opens (Game Library, Arcade, Emulators,
    // Applications): the drives scanned, in the background (the kept list
    // shows meanwhile), once a start-up. Not at start-up itself.
    static bool scanned = false;
    if (!scanned)
        scanned = StartBackgroundScan() || BackgroundScanBusy();
    if (scope == g_scope)
        return;
    g_scope = scope;
    ApplyScope();
    Log("library: scope %d, %u of %u games", (int)scope, (unsigned)g_games.size(), (unsigned)g_all.size());
}

const std::vector<library::Game>& AllGames()
{
    return g_all;
}

size_t AllIndex(size_t game)
{
    return game < g_allIndex.size() ? g_allIndex[game] : (size_t)-1;
}

LibraryScope CurrentLibraryScope()
{
    return g_scope;
}

const std::vector<library::Game>& Games()
{
    return g_games;
}

std::wstring IconLocator(size_t game)
{
    // The title ID too: XUI keeps textures by name, and a rescan moves games
    // to other positions (the number alone would show an old icon).
    wchar_t buf[48];
    _snwprintf_s(buf, 48, _TRUNCATE, L"slopdash://icon/%u/%08X", (unsigned)game,
                 game < g_games.size() ? g_games[game].titleId : 0);
    return buf;
}

// (thread-safe: Launch's prefetch worker reads them too, shotcache.cpp)
int EsrbOf(const std::string& path, unsigned contentType)
{
    static CRITICAL_SECTION lock;
    static volatile LONG made = 0;
    if (InterlockedCompareExchange(&made, 1, 0) == 0)
    {
        InitializeCriticalSection(&lock);
        InterlockedExchange(&made, 2);
    }
    while (made != 2)
        Sleep(0);
    static std::map<std::string, int> known;
    EnterCriticalSection(&lock);
    std::map<std::string, int>::iterator it = known.find(path);
    bool have = it != known.end();
    int esrb = have ? it->second : -1;
    LeaveCriticalSection(&lock);
    if (have)
        return esrb;
    const size_t kHead = 0x10000;   // (a xex's header: its optional headers are well inside)
    std::vector<unsigned char> head;
    bool read = false;
    if (contentType == 0x7000)
    {
        GodFiles files;
        files.dir = path + ".data";
        read = god::ReadRootFileHead(ReadGodData, &files, "default.xex", kHead, head);
    }
    else if (path.size() > 4 && !_stricmp(path.c_str() + path.size() - 4, ".xex"))
        read = ReadHead(path, kHead, head);
    esrb = read && !head.empty() ? library::XexEsrb(&head[0], head.size()) : -1;
    EnterCriticalSection(&lock);
    known[path] = esrb;
    LeaveCriticalSection(&lock);
    return esrb;
}

int GameEsrb(size_t game)
{
    if (game >= g_games.size())
        return -1;
    return EsrbOf(g_games[game].path, g_games[game].contentType);
}

bool IconPng(const wchar_t* locator, const unsigned char** data, unsigned* size)
{
    static const wchar_t kPrefix[] = L"slopdash://icon/";
    const size_t n = sizeof(kPrefix) / sizeof(kPrefix[0]) - 1;
    if (wcsncmp(locator, kPrefix, n) != 0)
        return false;
    size_t i = (size_t)_wtoi(locator + n);
    if (i >= g_games.size() || g_games[i].icon.empty())
        return false;
    *data = &g_games[i].icon[0];
    *size = (unsigned)g_games[i].icon.size();
    return true;
}
