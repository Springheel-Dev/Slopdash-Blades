#include "platform/storage.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>

#include "core/library.h"
#include "platform/log.h"
#include "platform/profiles.h"

namespace storage {

namespace {

std::string g_drive;
std::vector<Item> g_items;
std::vector<Game> g_games;

// The operation running (or the last one): its own copies, the thread reads them.
struct Running
{
    Op op;
    Item item;
    std::string to;
    unsigned long long done, total;
    bool ok;
    const wchar_t* error;
};
Running g_run;
volatile LONG g_busy = 0;

bool IsHex(const char* s, size_t n)
{
    if (strlen(s) != n)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isxdigit((unsigned char)s[i]))
            return false;
    return true;
}

std::vector<std::string> Folders(const std::string& dir)
{
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.')
            out.push_back(fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

unsigned long long FolderSize(const std::string& dir)
{
    unsigned long long total = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (fd.cFileName[0] == '.')
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            total += FolderSize(dir + "\\" + fd.cFileName);
        else
            total += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return total;
}

// The BadAvatar exploit's profile: its XUID, if it's on the console.
// (Profiles() leaves it out: profiles.cpp keeps its XUID for this.)
XUID HiddenOwner()
{
    return HiddenProfile();
}

void ReadPackage(const std::string& path, Item& it)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    std::vector<unsigned char> head(0x971A);
    DWORD got = 0;
    if (!ReadFile(h, &head[0], (DWORD)head.size(), &got, NULL))
        got = 0;
    CloseHandle(h);
    library::ContentHeader c;
    if (got && library::ReadContentHeader(&head[0], got, c))
    {
        it.name = c.name;
        it.titleName = c.titleName;
        it.icon = !c.icon.empty() ? c.icon : c.titleIcon;
    }
}

Category CategoryOf(DWORD type)
{
    switch (type)
    {
    case 0x10000: return kProfiles;
    case 0x20000: return kGamerPictures;
    case 0x30000: return kThemes;
    case 0x80000: return kDemos;
    case 0x90000: case 0xC0000: return kVideos;
    default: return kGames;
    }
}

struct ByName
{
    const std::vector<Item>* items;
    bool operator()(size_t a, size_t b) const
    {
        return _wcsicmp((*items)[a].name.c_str(), (*items)[b].name.c_str()) < 0;
    }
};

struct GameByName
{
    bool operator()(const Game& a, const Game& b) const { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; }
};

bool MakeFolders(const std::string& path)
{
    // Every folder of the path in turn ("usb:\Content\...\<file>": up to the file).
    for (size_t at = path.find('\\'); at != std::string::npos; at = path.find('\\', at + 1))
        CreateDirectoryA(path.substr(0, at).c_str(), NULL);
    return true;
}

bool CopyOne(const std::string& from, const std::string& to)
{
    HANDLE in = CreateFileA(from.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (in == INVALID_HANDLE_VALUE)
        return false;
    MakeFolders(to);
    HANDLE out = CreateFileA(to.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (out == INVALID_HANDLE_VALUE)
    {
        CloseHandle(in);
        return false;
    }
    std::vector<unsigned char> buf(1024 * 1024);
    bool ok = true;
    for (;;)
    {
        DWORD got = 0, put = 0;
        if (!ReadFile(in, &buf[0], (DWORD)buf.size(), &got, NULL))
        {
            ok = false;
            break;
        }
        if (!got)
            break;
        if (!WriteFile(out, &buf[0], got, &put, NULL) || put != got)
        {
            ok = false;
            break;
        }
        g_run.done += got;
    }
    CloseHandle(out);
    CloseHandle(in);
    if (!ok)
        DeleteFileA(to.c_str());
    return ok;
}

bool CopyTree(const std::string& from, const std::string& to)
{
    CreateDirectoryA(to.c_str(), NULL);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((from + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return true;
    bool ok = true;
    do
    {
        if (fd.cFileName[0] == '.')
            continue;
        std::string a = from + "\\" + fd.cFileName, b = to + "\\" + fd.cFileName;
        ok = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? CopyTree(a, b) : CopyOne(a, b);
    } while (ok && FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

bool DeleteTree(const std::string& dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.cFileName[0] == '.')
                continue;
            std::string p = dir + "\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                DeleteTree(p);
            else
                DeleteFileA(p.c_str());
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryA(dir.c_str()) != FALSE;
}

std::string Swap(const std::string& path, const std::string& drive)
{
    size_t colon = path.find(':');
    return colon == std::string::npos ? path : drive + path.substr(colon + 1);
}

DWORD WINAPI Work(LPVOID)
{
    Running& r = g_run;
    bool ok = true;
    if (r.op == kCopy || r.op == kMove)
    {
        ok = CopyOne(r.item.path, Swap(r.item.path, r.to));
        if (ok && !r.item.dataDir.empty())
            ok = CopyTree(r.item.dataDir, Swap(r.item.dataDir, r.to));
        if (!ok)
            r.error = L"There was a problem performing that action.";
    }
    if (ok && (r.op == kMove || r.op == kDelete))
    {
        ok = DeleteFileA(r.item.path.c_str()) != FALSE;
        if (!r.item.dataDir.empty())
            DeleteTree(r.item.dataDir);
        if (!ok)
            r.error = L"There was a problem performing that action.";
    }
    r.ok = ok;
    Log("memory: %s %s%s%s: %s", r.op == kCopy ? "copy" : r.op == kMove ? "move" : "delete", r.item.path.c_str(),
        r.op == kDelete ? "" : " to ", r.op == kDelete ? "" : r.to.c_str(), ok ? "done" : "FAILED");
    g_busy = 0;
    return 0;
}

}  // namespace

const wchar_t* CategoryName(Category c)
{
    static const wchar_t* const kNames[] = { L"Games", L"Gamer Profiles", L"Demos", L"Videos and Trailers",
                                             L"Themes", L"Gamer Pictures" };
    return c >= 0 && c < kCategoryCount ? kNames[c] : L"";
}

size_t Scan(const std::string& drive)
{
    DWORD t0 = GetTickCount();
    g_drive = drive;
    g_items.clear();
    g_games.clear();
    XUID hidden = HiddenOwner();
    std::string content = drive + "\\Content";
    std::vector<std::string> owners = Folders(content);
    for (size_t o = 0; o < owners.size(); o++)
    {
        if (!IsHex(owners[o].c_str(), 16))
            continue;
        XUID owner = _strtoui64(owners[o].c_str(), NULL, 16);
        if (hidden && owner == hidden)
            continue;                           // (the exploit's profile's own content)
        std::string od = content + "\\" + owners[o];
        std::vector<std::string> titles = Folders(od);
        for (size_t t = 0; t < titles.size(); t++)
        {
            if (!IsHex(titles[t].c_str(), 8))
                continue;
            DWORD titleId = strtoul(titles[t].c_str(), NULL, 16);
            std::string td = od + "\\" + titles[t];
            std::vector<std::string> types = Folders(td);
            for (size_t k = 0; k < types.size(); k++)
            {
                if (!IsHex(types[k].c_str(), 8))
                    continue;
                DWORD type = strtoul(types[k].c_str(), NULL, 16);
                std::string kd = td + "\\" + types[k];
                WIN32_FIND_DATAA fd;
                HANDLE h = FindFirstFileA((kd + "\\*").c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE)
                    continue;
                do
                {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        continue;
                    Item it;
                    it.path = kd + "\\" + fd.cFileName;
                    it.owner = owner;
                    it.titleId = titleId;
                    it.type = type;
                    it.size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                    std::string data = it.path + ".data";
                    if (GetFileAttributesA(data.c_str()) != (DWORD)-1)
                    {
                        it.dataDir = data;
                        it.size += FolderSize(data);
                    }
                    ReadPackage(it.path, it);
                    if (type == 0x10000)
                    {
                        // A profile's package names it by its XUID: its gamertag
                        // from the profile list. Not listed there and not
                        // read: left out (the exploit's, or one never seen).
                        XUID x = _strtoui64(fd.cFileName, NULL, 16);
                        if (x == hidden || !_wcsicmp(it.name.c_str(), L"ABadAvatar"))
                            continue;
                        it.name.clear();
                        const std::vector<ProfileEntry>& list = Profiles();
                        for (size_t k = 0; k < list.size(); k++)
                            if (list[k].xuid == x)
                                it.name = list[k].gamertag;
                        if (it.name.empty())
                            continue;
                    }
                    if (it.name.empty())
                        it.name = TypeName(type);
                    g_items.push_back(it);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
    }
    // The games that have items here, by name (their content's own title name).
    std::map<DWORD, size_t> at;
    for (size_t i = 0; i < g_items.size(); i++)
    {
        if (CategoryOf(g_items[i].type) != kGames)
            continue;
        std::map<DWORD, size_t>::iterator f = at.find(g_items[i].titleId);
        if (f == at.end())
        {
            Game g;
            g.titleId = g_items[i].titleId;
            at[g.titleId] = g_games.size();
            g_games.push_back(g);
            f = at.find(g_items[i].titleId);
        }
        Game& g = g_games[f->second];
        if (g.name.empty() && !g_items[i].titleName.empty())
            g.name = g_items[i].titleName;
        g.items.push_back(i);
    }
    for (size_t i = 0; i < g_games.size(); i++)
    {
        if (g_games[i].name.empty())
            g_games[i].name = L"Unknown Game";
        ByName order = { &g_items };
        std::sort(g_games[i].items.begin(), g_games[i].items.end(), order);
    }
    std::sort(g_games.begin(), g_games.end(), GameByName());
    Log("memory: %s: %u items, %u games (%u ms)", drive.c_str(), (unsigned)g_items.size(), (unsigned)g_games.size(),
        (unsigned)(GetTickCount() - t0));
    return g_items.size();
}

const std::string& Drive()
{
    return g_drive;
}

std::wstring DriveName(const std::string& drive)
{
    return !_stricmp(drive.c_str(), "hdd:") ? L"Hard Drive" : L"USB Storage Device";
}

size_t Count()
{
    return g_items.size();
}

const Item& At(size_t i)
{
    static const Item none = Item();
    return i < g_items.size() ? g_items[i] : none;
}

std::vector<size_t> InCategory(Category c)
{
    std::vector<size_t> out;
    if (c == kGames)
    {
        for (size_t g = 0; g < g_games.size(); g++)
            out.insert(out.end(), g_games[g].items.begin(), g_games[g].items.end());
        return out;
    }
    for (size_t i = 0; i < g_items.size(); i++)
        if (CategoryOf(g_items[i].type) == c)
            out.push_back(i);
    ByName order = { &g_items };
    std::sort(out.begin(), out.end(), order);
    return out;
}

const std::vector<Game>& Games()
{
    return g_games;
}

const wchar_t* TypeName(DWORD type)
{
    switch (type)
    {
    case 0x1: return L"Saved Game";
    case 0x2: return L"Downloaded Content";
    case 0x10000: return L"Gamer Profile";
    case 0x20000: return L"Gamer Picture";
    case 0x30000: return L"Theme";
    case 0x80000: return L"Game Demo";
    case 0x90000: return L"Video";
    case 0xC0000: return L"Game Trailer";
    case 0x7000: case 0xD0000: case 0x5000: case 0x4000: return L"Game";
    default: return L"Game Content";
    }
}

std::wstring OwnerName(size_t i)
{
    XUID owner = At(i).owner;
    if (!owner)
        return L"";
    const std::vector<ProfileEntry>& p = Profiles();
    for (size_t k = 0; k < p.size(); k++)
        if (p[k].xuid == owner)
            return p[k].gamertag;
    return L"Unknown Profile";
}

std::wstring SizeText(unsigned long long bytes)
{
    wchar_t b[32];
    double mb = bytes / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        _snwprintf_s(b, 32, _TRUNCATE, L"%.1f GB", mb / 1024.0);
    else if (mb >= 1.0)
        _snwprintf_s(b, 32, _TRUNCATE, L"%.1f MB", mb);
    else
        _snwprintf_s(b, 32, _TRUNCATE, L"%u KB", (unsigned)((bytes + 1023) / 1024));
    return b;
}

std::wstring Locator(size_t i)
{
    if (At(i).icon.empty())
        return L"";
    wchar_t b[48];
    _snwprintf_s(b, 48, _TRUNCATE, L"slopdash://content/%u", (unsigned)i);
    return b;
}

bool ContentPng(const wchar_t* locator, const unsigned char** data, unsigned* size)
{
    static const wchar_t kPrefix[] = L"slopdash://content/";
    if (!locator || wcsncmp(locator, kPrefix, ARRAYSIZE(kPrefix) - 1))
        return false;
    size_t i = (size_t)_wtoi(locator + ARRAYSIZE(kPrefix) - 1);
    if (i >= g_items.size() || g_items[i].icon.empty())
        return false;
    *data = &g_items[i].icon[0];
    *size = (unsigned)g_items[i].icon.size();
    return true;
}

std::string OtherDrive()
{
    std::string other = !_stricmp(g_drive.c_str(), "hdd:") ? "usb:" : "hdd:";
    ULARGE_INTEGER a, b, c;
    return GetDiskFreeSpaceExA((other + "\\").c_str(), &a, &b, &c) ? other : std::string();
}

std::string PathOn(size_t i, const std::string& drive)
{
    return Swap(At(i).path, drive);
}

bool Start(Op op, size_t item, const std::string& toDrive)
{
    if (item >= g_items.size() || InterlockedCompareExchange(&g_busy, 1, 0) != 0)
        return false;
    g_run.op = op;
    g_run.item = g_items[item];
    g_run.to = toDrive;
    g_run.done = 0;
    g_run.total = g_items[item].size;
    g_run.ok = false;
    g_run.error = L"";
    if (op != kDelete)
    {
        ULARGE_INTEGER freeBytes, total, totalFree;
        if (!GetDiskFreeSpaceExA((toDrive + "\\").c_str(), &freeBytes, &total, &totalFree) ||
            freeBytes.QuadPart < g_run.total)
        {
            g_run.error = L"The destination device is full.";
            g_busy = 0;
            return false;
        }
    }
    HANDLE t = CreateThread(NULL, 0, Work, NULL, 0, NULL);
    if (!t)
    {
        g_busy = 0;
        return false;
    }
    CloseHandle(t);
    return true;
}

bool Busy()
{
    return g_busy != 0;
}

int Percent()
{
    if (g_run.op == kDelete || !g_run.total)
        return g_busy ? 50 : 100;
    return (int)(g_run.done * 100 / g_run.total);
}

bool LastSucceeded()
{
    return g_run.ok;
}

const wchar_t* LastError()
{
    return g_run.error ? g_run.error : L"";
}

}  // namespace storage
