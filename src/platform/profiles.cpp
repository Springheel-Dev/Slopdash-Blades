#include "platform/profiles.h"

#include "core/stfs.h"

#include <stdio.h>
#include <stdlib.h>

#include "core/library.h"
#include "platform/log.h"

extern "C" {
LONG XexGetModuleHandle(PCHAR name, PHANDLE module);
LONG XexGetProcedureAddress(HANDLE module, DWORD ordinal, PVOID* address);
}

namespace {

std::vector<ProfileEntry> g_profiles;
// Each profile file as the last scan saw it (path, last write, size), so a
// change made while it's signed out (a profile editor, a copy over FTP) is
// read before it's next signed in and locked. Without it the cache kept the
// old numbers: a test profile's 1,308 unlocks showed as 485 (2026-11-05).
struct FileStamp { XUID xuid; std::string path; FILETIME written; DWORD size; };
std::vector<FileStamp> g_stamps;
XUID g_hiddenProfile = 0;              // the BadAvatar exploit's profile (not listed)
const char* const kCacheDir = "game:\\cache\\profiles";

std::string XuidHex(XUID x)
{
    char b[20];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%016I64X", (unsigned __int64)x);
    return b;
}

bool ReadFileBytes(const std::string& path, size_t want, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
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

void WriteFileBytes(const std::string& path, const void* data, size_t size)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD put = 0;
    WriteFile(h, data, (DWORD)size, &put, NULL);
    CloseHandle(h);
}

// The cache keeps what a locked (signed-in) profile can't tell us.
void SaveToCache(const ProfileEntry& p)
{
    CreateDirectoryA("game:\\cache", NULL);
    CreateDirectoryA(kCacheDir, NULL);
    std::string base = std::string(kCacheDir) + "\\" + XuidHex(p.xuid);
    WriteFileBytes(base + ".txt", p.gamertag.c_str(), p.gamertag.size() * sizeof(wchar_t));
    if (!p.picture.empty())
        WriteFileBytes(base + ".png", &p.picture[0], p.picture.size());
    if (p.gamerscore >= 0)
    {
        char card[64];
        int n = _snprintf_s(card, sizeof(card), _TRUNCATE, "%d %d %d", p.gamerscore, p.gamesPlayed, p.achievements);
        WriteFileBytes(base + ".card", card, n > 0 ? (size_t)n : 0);
    }
}

void LoadFromCache(ProfileEntry& p)
{
    std::string base = std::string(kCacheDir) + "\\" + XuidHex(p.xuid);
    std::vector<unsigned char> name;
    if (p.gamertag.empty() && ReadFileBytes(base + ".txt", 0, name) && name.size() >= 2)
        p.gamertag.assign((const wchar_t*)&name[0], name.size() / sizeof(wchar_t));
    if (p.picture.empty())
        ReadFileBytes(base + ".png", 0, p.picture);
    std::vector<unsigned char> card;
    if (p.gamerscore < 0 && ReadFileBytes(base + ".card", 0, card) && !card.empty())
    {
        card.push_back(0);
        sscanf_s((const char*)&card[0], "%d %d %d", &p.gamerscore, &p.gamesPlayed, &p.achievements);
    }
}

// The numbers in a readable profile file (the whole package is read).
void ReadCardNumbers(const std::string& file, ProfileEntry& p)
{
    std::vector<unsigned char> pkg, gpd;
    if (!ReadFileBytes(file, 0, pkg) || pkg.empty() || !stfs::ReadFile(&pkg[0], pkg.size(), "FFFE07D1.gpd", gpd) ||
        gpd.empty())
        return;
    stfs::GamerCardNumbers(&gpd[0], gpd.size(), p.gamerscore, p.gamesPlayed, p.achievements);
    // Its settings files (the titles played and each one's achievements), kept
    // for while it's signed in and locked: game:\cache\profiles\<XUID>\*.gpd.
    // (Opening a signed-in profile through the system, XamProfileOpen, failed
    // on 17559 and then hung SlopDash: 0.0.98.)
    std::string dir = std::string(kCacheDir) + "\\" + XuidHex(p.xuid);
    CreateDirectoryA("game:\\cache", NULL);
    CreateDirectoryA(kCacheDir, NULL);
    CreateDirectoryA(dir.c_str(), NULL);
    std::vector<std::string> names;
    if (stfs::ListFiles(&pkg[0], pkg.size(), names))
        for (size_t i = 0; i < names.size(); i++)
        {
            const std::string& n = names[i];
            if (n.size() < 4 || _stricmp(n.c_str() + n.size() - 4, ".gpd"))
                continue;
            std::vector<unsigned char> f;
            if (stfs::ReadFile(&pkg[0], pkg.size(), n.c_str(), f) && !f.empty())
                WriteFileBytes(dir + "\\" + n, &f[0], f.size());
        }
}

int SignedInSlot(XUID xuid)
{
    for (int i = 0; i < XUSER_MAX_COUNT; i++)
    {
        XUID x = 0;
        if (XUserGetSigninState(i) != eXUserSigninState_NotSignedIn && XUserGetXUID(i, &x) == ERROR_SUCCESS &&
            x == xuid)
            return i;
    }
    return -1;
}

// XamUserLogon (xam ordinal 0x20B): (XUID[4] wanted in each slot, flags,
// overlapped). Not in the XDK; the flags are the ones XeLiveStealth uses on
// retail hardware (0x424 sign in, 0x23 sign out), confirmed by Xenia.
typedef DWORD (*XamUserLogonFn)(const XUID*, DWORD, PXOVERLAPPED);

XamUserLogonFn Logon()
{
    static XamUserLogonFn fn = NULL;
    static bool looked = false;
    if (!looked)
    {
        looked = true;
        HANDLE xam = NULL;
        PVOID p = NULL;
        if (XexGetModuleHandle("xam.xex", &xam) >= 0 && XexGetProcedureAddress(xam, 0x20B, &p) >= 0)
            fn = (XamUserLogonFn)p;
        Log("profiles: XamUserLogon %s", fn ? "found" : "MISSING");
    }
    return fn;
}

volatile LONG g_busy = 0;
struct SwitchRequest
{
    int slot;
    XUID xuid;
};
SwitchRequest g_request;

bool WaitForSlot(int slot, bool signedIn)
{
    for (int i = 0; i < 100; i++)
    {
        if ((XUserGetSigninState(slot) != eXUserSigninState_NotSignedIn) == signedIn)
            return true;
        Sleep(50);
    }
    return false;
}

DWORD WINAPI SwitchThread(LPVOID)
{
    XamUserLogonFn logon = Logon();
    int slot = g_request.slot;
    XUID target = g_request.xuid;
    Log("profiles: switch thread running (slot %d)", slot);
    if (logon)
    {
        Log("profiles: slot %d state %d", slot, (int)XUserGetSigninState(slot));
        if (XUserGetSigninState(slot) != eXUserSigninState_NotSignedIn)
        {
            // Everyone stays except this slot.
            XUID keep[XUSER_MAX_COUNT];
            for (int i = 0; i < XUSER_MAX_COUNT; i++)
                if (XUserGetXUID(i, &keep[i]) != ERROR_SUCCESS || i == slot)
                    keep[i] = INVALID_XUID;
            Log("profiles: signing out slot %d", slot);
            DWORD r = logon(keep, 0x23, NULL);
            bool out = WaitForSlot(slot, false);
            Log("profiles: sign-out %u, %s", (unsigned)r, out ? "done" : "still signed in");
        }
        if (target)
        {
            XUID want[XUSER_MAX_COUNT] = { INVALID_XUID, INVALID_XUID, INVALID_XUID, INVALID_XUID };
            want[slot] = target;
            Log("profiles: signing in %s at slot %d", XuidHex(target).c_str(), slot);
            DWORD r = logon(want, 0x424, NULL);
            bool in = WaitForSlot(slot, true);
            Log("profiles: sign-in %u, %s", (unsigned)r, in ? "done" : "not signed in");
        }
    }
    InterlockedExchange(&g_busy, 0);
    return 0;
}

// The console's own list of profiles (xam's XamProfileCreateEnumerator,
// ordinal 0x231, and XamProfileEnumerate, 0x232; not in the XDK, layout from
// Xenia): each item is 0x188 bytes, the offline XUID then the account info,
// whose gamertag (16 UTF-16BE characters) is at +8. The profile files
// themselves are all held open by the system, so this is where names come from.
typedef DWORD (*XamProfileCreateEnumeratorFn)(DWORD deviceId, PHANDLE handle);
typedef DWORD (*XamProfileEnumerateFn)(HANDLE handle, DWORD flags, PVOID buffer, PXOVERLAPPED overlapped);
const size_t kEnumItem = 0x188;

struct EnumResult
{
    std::vector<XUID> xuids;
    std::vector<std::wstring> names;
};
EnumResult g_enum;                  // the last complete answer (guarded by g_enumLock)
CRITICAL_SECTION g_enumLock;
volatile LONG g_enumBusy = 0;

void SaveName(XUID x, const std::wstring& name);

// This run's answer becomes the one scans use.
void CommitEnum(const EnumResult& found)
{
    g_enum = found;
}

DWORD WINAPI EnumerateThread(LPVOID)
{
    EnumResult found;
    EnumResult& g_enum = found;     // (the code below fills this run's own list)
    HANDLE xam = NULL;
    PVOID create = NULL, next = NULL;
    if (XexGetModuleHandle("xam.xex", &xam) < 0 || XexGetProcedureAddress(xam, 0x231, &create) < 0 ||
        XexGetProcedureAddress(xam, 0x232, &next) < 0 || !create || !next)
    {
        Log("profiles: no profile enumerator");
        return 0;
    }
    HANDLE e = NULL;
    DWORD r = ((XamProfileCreateEnumeratorFn)create)(0, &e);
    Log("profiles: enumerator %u", (unsigned)r);
    if (r != ERROR_SUCCESS || !e)
        return 0;
    std::vector<BYTE> buf(kEnumItem * 16);
    for (int round = 0; round < 16; round++)
    {
        memset(&buf[0], 0, buf.size());
        r = ((XamProfileEnumerateFn)next)(e, 0, &buf[0], NULL);
        if (r != ERROR_SUCCESS)
            break;
        bool any = false;
        for (size_t at = 0; at + kEnumItem <= buf.size(); at += kEnumItem)
        {
            XUID x = *(const XUID*)&buf[at];        // big endian, as the console is
            if (!x)
                continue;
            any = true;
            std::wstring name;
            for (int c = 0; c < 16; c++)
            {
                wchar_t ch = *(const wchar_t*)&buf[at + 8 + 8 + 2 * c];
                if (!ch)
                    break;
                name += ch;
            }
            bool seen = false;
            for (size_t k = 0; k < g_enum.xuids.size(); k++)
                seen = seen || g_enum.xuids[k] == x;
            if (!seen)
            {
                g_enum.xuids.push_back(x);
                g_enum.names.push_back(name);
            }
        }
        if (!any)
            break;
    }
    Log("profiles: enumerated %u (last %u)", (unsigned)g_enum.xuids.size(), (unsigned)r);
    for (size_t i = 0; i < g_enum.xuids.size(); i++)
    {
        Log("profiles:   %016I64X \"%S\"", (unsigned __int64)g_enum.xuids[i], g_enum.names[i].c_str());
        if (!g_enum.names[i].empty())
            SaveName(g_enum.xuids[i], g_enum.names[i]);   // for every later scan, even without an answer
    }
    CloseHandle(e);
    EnterCriticalSection(&g_enumLock);
    CommitEnum(found);
    LeaveCriticalSection(&g_enumLock);
    InterlockedExchange(&g_enumBusy, 0);
    return 0;
}

// Asks the console for its profiles in the background: nothing waits for it.
// (0.0.70-0.0.74 waited 3 s, then gave up for good when it took longer at
// start-up, and the profiles' names went missing.) Each scan starts one if
// none is running; names it finds are kept in the cache (SaveName), so a scan
// has them whether or not this run's answer is in yet.
#pragma warning(disable : 4127)     // a constant condition: the switch below
const bool kAskForProfiles = false;

void EnumerateProfiles()
{
    static bool ready = false;
    if (!ready)
    {
        InitializeCriticalSection(&g_enumLock);
        ready = true;
    }
    // Off: from 0.0.70 the console's profile list (XamProfileEnumerate) stopped
    // answering, and while it hung every other profile call waited behind it,
    // signing in included (0.0.75). Names come from the signed-in profiles
    // (XUserGetName) and the names saved from them (CachedName) instead.
    if (!kAskForProfiles)
        return;
    if (InterlockedCompareExchange(&g_enumBusy, 1, 0) != 0)
        return;
    HANDLE t = CreateThread(NULL, 0, EnumerateThread, NULL, 0, NULL);
    if (!t)
    {
        InterlockedExchange(&g_enumBusy, 0);
        return;
    }
    CloseHandle(t);
}

std::wstring EnumeratedName(XUID x)
{
    std::wstring name;
    EnterCriticalSection(&g_enumLock);
    for (size_t i = 0; i < g_enum.xuids.size(); i++)
        if (g_enum.xuids[i] == x)
            name = g_enum.names[i];
    LeaveCriticalSection(&g_enumLock);
    return name;
}

void SaveName(XUID x, const std::wstring& name)
{
    CreateDirectoryA("game:\\cache", NULL);
    CreateDirectoryA(kCacheDir, NULL);
    std::string base = std::string(kCacheDir) + "\\" + XuidHex(x);
    WriteFileBytes(base + ".txt", name.c_str(), name.size() * sizeof(wchar_t));
}

// A name saved earlier (by a scan or an enumeration), or "".
std::wstring CachedName(XUID x)
{
    std::vector<unsigned char> bytes;
    if (!ReadFileBytes(std::string(kCacheDir) + "\\" + XuidHex(x) + ".txt", 0, bytes) || bytes.size() < 2)
        return L"";
    std::wstring name((const wchar_t*)&bytes[0], bytes.size() / sizeof(wchar_t));
    return name;
}

std::string GamerPicCache(XUID x)
{
    return std::string(kCacheDir) + "\\" + XuidHex(x) + ".argb";
}

}  // namespace

void ScanProfiles()
{
    g_profiles.clear();
    g_stamps.clear();
    EnumerateProfiles();
    static const char* const kContent[] = { "hdd:\\Content\\", "usb:\\Content\\" };
    for (size_t d = 0; d < 2; d++)
    {
        std::string content = kContent[d];
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((content + "*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
        {
            Log("profiles: can't list %s (%u)", content.c_str(), (unsigned)GetLastError());
            continue;
        }
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || strlen(fd.cFileName) != 16 ||
                !strcmp(fd.cFileName, "0000000000000000"))
                continue;
            std::string file = content + fd.cFileName + "\\FFFE07D1\\00010000\\" + fd.cFileName;
            // A directory search, not GetFileAttributes: that opens the file,
            // and a signed-in profile's file is locked (0.0.42 found none).
            WIN32_FIND_DATAA pf;
            HANDLE ph = FindFirstFileA(file.c_str(), &pf);
            if (ph == INVALID_HANDLE_VALUE)
            {
                Log("profiles: %s has no profile file", fd.cFileName);
                continue;
            }
            FindClose(ph);
            FileStamp stamp = { (XUID)_strtoui64(fd.cFileName, NULL, 16), file, pf.ftLastWriteTime, pf.nFileSizeLow };
            g_stamps.push_back(stamp);
            ProfileEntry p;
            p.gamerscore = p.gamesPlayed = p.achievements = -1;
            p.onUsb = d == 1;
            p.live = false;
            p.xuid = (XUID)_strtoui64(fd.cFileName, NULL, 16);
            p.signedInAs = SignedInSlot(p.xuid);
            std::vector<unsigned char> head;
            bool read = ReadFileBytes(file, library::kStfsHeaderBytes, head) && !head.empty() &&
                        library::ReadProfileHeader(&head[0], head.size(), p.gamertag, p.picture);
            if (read)
                ReadCardNumbers(file, p);
            if (!read || p.gamerscore < 0)
                LoadFromCache(p);
            // Xbox Live or not (signin.xex's line 2): the system says so for a
            // signed-in profile; kept for when it's signed out.
            std::string liveFile = std::string(kCacheDir) + "\\" + XuidHex(p.xuid) + ".live";
            XUSER_SIGNIN_INFO info;
            if (p.signedInAs >= 0 &&
                XUserGetSigninInfo(p.signedInAs, XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY, &info) == ERROR_SUCCESS)
            {
                p.live = (info.dwInfoFlags & XUSER_INFO_FLAG_LIVE_ENABLED) != 0;
                CreateDirectoryA("game:\\cache", NULL);
                CreateDirectoryA(kCacheDir, NULL);
                WriteFileBytes(liveFile, p.live ? "1" : "0", 1);
            }
            else
            {
                std::vector<unsigned char> flag;
                p.live = ReadFileBytes(liveFile, 0, flag) && !flag.empty() && flag[0] == '1';
            }
            if (p.signedInAs >= 0)
            {
                char name[XUSER_NAME_SIZE + 1] = { 0 };
                if (XUserGetName(p.signedInAs, name, sizeof(name)) == ERROR_SUCCESS && name[0])
                {
                    p.gamertag.clear();
                    for (const char* c = name; *c; c++)
                        p.gamertag += (wchar_t)(unsigned char)*c;
                }
            }
            // The package's display name is the XUID, not the gamertag (0.0.46):
            // the console's own profile list has the name.
            if (!_wcsicmp(p.gamertag.c_str(), std::wstring(fd.cFileName, fd.cFileName + 16).c_str()))
                p.gamertag.clear();
            std::wstring listed = EnumeratedName(p.xuid);
            if (p.signedInAs < 0 && !listed.empty())
                p.gamertag = listed;
            // No name from the system yet: the one saved before (not an ID).
            std::wstring hex(fd.cFileName, fd.cFileName + 16);
            if (p.gamertag.empty() || !_wcsicmp(p.gamertag.c_str(), hex.c_str()))
            {
                std::wstring saved = CachedName(p.xuid);
                p.gamertag = _wcsicmp(saved.c_str(), hex.c_str()) ? saved : L"";
            }
            if (!p.gamertag.empty())
                SaveToCache(p);             // name and picture, for when the file is locked
            if (p.gamertag.empty())
                p.gamertag = std::wstring(fd.cFileName, fd.cFileName + 16);
            Log("profiles: %s %S (%s, %u-byte picture, %s, card %d/%d/%d)", fd.cFileName, p.gamertag.c_str(),
                read ? "read" : "locked", (unsigned)p.picture.size(), p.signedInAs >= 0 ? "signed in" : "signed out",
                p.gamerscore, p.gamesPlayed, p.achievements);
            // The BadAvatar exploit's profile (it starts the hack; it isn't
            // meant to be signed in): never offered, signed in or not
            // (SigninChanged signs it out).
            if (!_wcsicmp(p.gamertag.c_str(), L"ABadAvatar"))
            {
                g_hiddenProfile = p.xuid;   // (System > Memory leaves it, and what it owns, out)
                continue;
            }
            g_profiles.push_back(p);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    // The count, for the console's Guide (SlopGuide: its signed-out main
    // menu's "N Profiles Found", the same number SlopDash's Sign In card shows).
    char count[16];
    int n = _snprintf_s(count, sizeof(count), _TRUNCATE, "%u", (unsigned)g_profiles.size());
    CreateDirectoryA("game:\\Guide", NULL);
    WriteFileBytes("game:\\Guide\\profiles.txt", count, n > 0 ? (size_t)n : 0);
}

bool ProfilesChangedOnDisk()
{
    for (size_t i = 0; i < g_stamps.size(); i++)
    {
        const FileStamp& st = g_stamps[i];
        if (SignedInSlot(st.xuid) >= 0)
            continue;                       // locked: read when it's signed out
        WIN32_FIND_DATAA pf;
        HANDLE ph = FindFirstFileA(st.path.c_str(), &pf);
        if (ph == INVALID_HANDLE_VALUE)
            return true;                    // gone (deleted or moved)
        FindClose(ph);
        if (CompareFileTime(&pf.ftLastWriteTime, &st.written) != 0 || pf.nFileSizeLow != st.size)
        {
            Log("profiles: %s changed on disk: reading it again", XuidHex(st.xuid).c_str());
            return true;
        }
    }
    return false;
}

XUID HiddenProfile()
{
    return g_hiddenProfile;
}

bool ProfileNumbers(XUID xuid, int& gamerscore, int& gamesPlayed, int& achievements)
{
    for (size_t i = 0; i < g_profiles.size(); i++)
        if (g_profiles[i].xuid == xuid)
        {
            gamerscore = g_profiles[i].gamerscore;
            gamesPlayed = g_profiles[i].gamesPlayed;
            achievements = g_profiles[i].achievements;
            return true;
        }
    return false;
}

const wchar_t* ProfileZoneName(XUID xuid)
{
    // XPROFILE_GAMERCARD_ZONE (xonline.h): 0 Xbox 1, 1 Recreation, 2 Pro, 3 Family, 4 Underground.
    static const wchar_t* const kNames[] = { NULL, L"Recreation", L"Pro", L"Family", L"Underground" };
    // A zone of the user's own first (the Guide's Gamer Zone > Custom: .czone, UTF-16).
    static std::wstring custom;
    std::vector<unsigned char> own;
    if (xuid && ReadFileBytes(std::string(kCacheDir) + "\\" + XuidHex(xuid) + ".czone", 0, own) && own.size() >= 2)
    {
        custom.assign((const wchar_t*)&own[0], own.size() / sizeof(wchar_t));
        return custom.c_str();
    }
    std::vector<unsigned char> text;
    if (!xuid || !ReadFileBytes(std::string(kCacheDir) + "\\" + XuidHex(xuid) + ".zone", 0, text) || text.empty())
        return L"None";
    text.push_back(0);
    int zone = atoi((const char*)&text[0]);
    return zone >= 1 && zone < (int)(sizeof(kNames) / sizeof(kNames[0])) ? kNames[zone] : L"None";
}

const std::vector<ProfileEntry>& Profiles()
{
    return g_profiles;
}

// The picture and numbers the Guide read from the profile itself (SlopGuide
// FillCard writes <XUID>.png and .card while it's signed in): newer than the
// scan's, and the only ones a profile made since its sign-in has.
bool GuidePicture(XUID xuid, std::vector<unsigned char>& png)
{
    return xuid && ReadFileBytes(std::string(kCacheDir) + "\\" + XuidHex(xuid) + ".png", 0, png) && !png.empty();
}

bool GuideCardNumbers(XUID xuid, int& gamerscore, int& gamesPlayed, int& achievements)
{
    std::vector<unsigned char> card;
    if (!xuid || !ReadFileBytes(std::string(kCacheDir) + "\\" + XuidHex(xuid) + ".card", 0, card) || card.empty())
        return false;
    card.push_back(0);
    return sscanf_s((const char*)&card[0], "%d %d %d", &gamerscore, &gamesPlayed, &achievements) >= 1;
}

std::wstring ProfilePictureLocator(XUID xuid)
{
    // The Guide's picture first; its size in the locator, so a changed one is
    // loaded again rather than taken from the texture cache.
    std::vector<unsigned char> png;
    if (GuidePicture(xuid, png))
    {
        wchar_t b[64];
        _snwprintf_s(b, 64, _TRUNCATE, L"slopdash://profile/%016I64X?%u", (unsigned __int64)xuid, (unsigned)png.size());
        return b;
    }
    for (size_t i = 0; i < g_profiles.size(); i++)
        if (g_profiles[i].xuid == xuid && !g_profiles[i].picture.empty())
        {
            wchar_t b[48];
            _snwprintf_s(b, 48, _TRUNCATE, L"slopdash://profile/%016I64X", (unsigned __int64)xuid);
            return b;
        }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(GamerPicCache(xuid).c_str(), &fd);
    if (SignedInSlot(xuid) < 0 && h == INVALID_HANDLE_VALUE)
        return L"";
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    return GamerPicLocator(xuid);
}

std::wstring GamerPicLocator(XUID xuid)
{
    wchar_t b[48];
    _snwprintf_s(b, 48, _TRUNCATE, L"slopdash://gamerpic/%016I64X", (unsigned __int64)xuid);
    return b;
}

bool GamerPicPixels(const wchar_t* locator, std::vector<unsigned char>& pixels)
{
    static const wchar_t kPrefix[] = L"slopdash://gamerpic/";
    const size_t n = sizeof(kPrefix) / sizeof(kPrefix[0]) - 1;
    if (wcsncmp(locator, kPrefix, n) != 0)
        return false;
    XUID xuid = (XUID)_wcstoui64(locator + n, NULL, 16);
    const DWORD kBytes = kGamerPicSize * kGamerPicSize * 4;
    int slot = SignedInSlot(xuid);
    if (slot >= 0)
    {
        pixels.resize(kBytes);
        DWORD r = XUserReadGamerPicture(slot, FALSE, &pixels[0], kGamerPicSize * 4, kGamerPicSize, NULL);
        Log("gamer picture: %s (slot %d): %u", XuidHex(xuid).c_str(), slot, (unsigned)r);
        if (r == ERROR_SUCCESS)
        {
            // Kept for when this profile isn't signed in (its file can't be read).
            CreateDirectoryA("game:\\cache", NULL);
            CreateDirectoryA(kCacheDir, NULL);
            WriteFileBytes(GamerPicCache(xuid), &pixels[0], kBytes);
            return true;
        }
    }
    return ReadFileBytes(GamerPicCache(xuid), 0, pixels) && pixels.size() == kBytes;
}

bool ProfilePng(const wchar_t* locator, const unsigned char** data, unsigned* size)
{
    static const wchar_t kPrefix[] = L"slopdash://profile/";
    const size_t n = sizeof(kPrefix) / sizeof(kPrefix[0]) - 1;
    if (wcsncmp(locator, kPrefix, n) != 0)
        return false;
    XUID xuid = (XUID)_wcstoui64(locator + n, NULL, 16);
    static std::vector<unsigned char> s_guide;      // (kept: the caller reads it after we return)
    if (GuidePicture(xuid, s_guide))
    {
        *data = &s_guide[0];
        *size = (unsigned)s_guide.size();
        return true;
    }
    for (size_t i = 0; i < g_profiles.size(); i++)
        if (g_profiles[i].xuid == xuid && !g_profiles[i].picture.empty())
        {
            *data = &g_profiles[i].picture[0];
            *size = (unsigned)g_profiles[i].picture.size();
            return true;
        }
    return false;
}

bool StartProfileSwitch(int slot, XUID xuid)
{
    if (slot < 0 || slot >= XUSER_MAX_COUNT || !Logon())
        return false;
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0)
        return false;
    g_request.slot = slot;
    g_request.xuid = xuid;
    HANDLE t = CreateThread(NULL, 0, SwitchThread, NULL, 0, NULL);
    if (!t)
    {
        InterlockedExchange(&g_busy, 0);
        return false;
    }
    CloseHandle(t);
    return true;
}

bool ProfileSwitchBusy()
{
    return g_busy != 0;
}

// XNotifyQueueUI (xam ordinal 0x290: type, user, priority, text, context), as
// plugins use it. Types (Free60): 6 signed out, 7 signed in.
typedef void (*XNotifyQueueUIFn)(DWORD type, DWORD user, ULONGLONG priority, WCHAR* text, PVOID context);
const DWORD kToastSignedOut = 6, kToastSignedIn = 7;

void Toast(DWORD type, int slot, const std::wstring& text)
{
    static XNotifyQueueUIFn fn = NULL;
    static bool looked = false;
    if (!looked)
    {
        looked = true;
        HANDLE xam = NULL;
        PVOID p = NULL;
        if (XexGetModuleHandle("xam.xex", &xam) >= 0 && XexGetProcedureAddress(xam, 0x290, &p) >= 0)
            fn = (XNotifyQueueUIFn)p;
    }
    static WCHAR buf[4][64];            // xam may read it after the call returns: one per slot
    if (!fn || slot < 0 || slot >= 4)
        return;
    wcsncpy_s(buf[slot], 64, text.c_str(), _TRUNCATE);
    fn(type, (DWORD)slot, 2, buf[slot], NULL);       // 2: high priority
    Log("toast: %S", text.c_str());
}

void SigninChanged(bool announce)
{
    static XUID before[XUSER_MAX_COUNT] = { 0 };
    static std::wstring names[XUSER_MAX_COUNT];
    for (int i = 0; i < XUSER_MAX_COUNT; i++)
    {
        XUID now = 0;
        if (XUserGetSigninState(i) == eXUserSigninState_NotSignedIn || XUserGetXUID(i, &now) != ERROR_SUCCESS)
            now = 0;
        if (now == before[i])
            continue;
        if (announce && before[i])
            Toast(kToastSignedOut, i, names[i]);     // the notification adds "signed out"
        names[i].clear();
        if (now)
        {
            char n[XUSER_NAME_SIZE + 1] = { 0 };
            if (XUserGetName(i, n, sizeof(n)) == ERROR_SUCCESS)
                for (const char* c = n; *c; c++)
                    names[i] += (wchar_t)(unsigned char)*c;
            // The BadAvatar exploit's profile is never to be signed in:
            // signed straight back out, without a toast.
            if (!_wcsicmp(names[i].c_str(), L"ABadAvatar"))
            {
                Log("profiles: ABadAvatar signed in at slot %d: signing it out", i);
                StartProfileSwitch(i, 0);
                names[i].clear();
                continue;           // (before[i] stays: its sign-out isn't announced either)
            }
            if (announce)
                Toast(kToastSignedIn, i, names[i]);  // ... and "signed in" (0.0.65 said it twice)
        }
        before[i] = now;
    }
}
