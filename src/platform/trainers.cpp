#include "platform/trainers.h"

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/library.h"
#include "platform/log.h"
#include "platform/scanner.h"
#include "platform/settings.h"

extern "C" {
LONG XexGetModuleHandle(PCHAR name, PHANDLE module);
LONG XexGetProcedureAddress(HANDLE module, DWORD ordinal, PVOID* address);
LONG XexLoadImageFromMemory(PVOID image, DWORD size, PCSTR name, DWORD flags, DWORD minVersion, PHANDLE module);
VOID XexUnloadImage(HANDLE module);
LONG ExCreateThread(PHANDLE thread, DWORD stack, LPDWORD id, PVOID startup, LPTHREAD_START_ROUTINE start, LPVOID param,
                    DWORD flags);
}

namespace trainers {

namespace {

// The name Aurora gives a trainer as it loads it, and looks it up by after.
char kModuleName[] = "dll.aurora.trainer";
const DWORD kTag = 0x41554F41;              // 'AUOA'
const DWORD kDllFlags = 8;                  // (Aurora's: a DLL)

std::string Hex(unsigned v)
{
    char b[12];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%08X", v);
    return b;
}

std::string CheatsKey(unsigned titleId, const std::string& file)
{
    return "trainer." + Hex(titleId) + "." + file + ".cheats";
}

bool ReadWhole(const std::string& path, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL), got = 0;
    bool ok = size && size < 64u * 1024 * 1024;
    if (ok)
    {
        out.resize(size);
        ok = ::ReadFile(h, &out[0], size, &got, NULL) && got == size;
    }
    CloseHandle(h);
    return ok;
}

// The load and the unload, each on a system thread of its own, as Aurora's
// (its 82361AA8: ExCreateThread with flag 2). From SlopDash's own thread the
// load was refused (C0000022, access denied: the test of 2026-10-15).
struct LoadJob
{
    PVOID data;
    DWORD size;
    LONG status;
    HANDLE module;
};

DWORD WINAPI LoadThread(LPVOID p)
{
    LoadJob* job = (LoadJob*)p;
    job->status = XexLoadImageFromMemory(job->data, job->size, kModuleName, kDllFlags, 0, NULL);
    return 0;
}

DWORD WINAPI UnloadThread(LPVOID p)
{
    LoadJob* job = (LoadJob*)p;
    *(WORD*)((BYTE*)job->module + 0x40) = 1;    // (Aurora's: the load count, so it really goes)
    XexUnloadImage(job->module);
    return 0;
}

bool OnSystemThread(LPTHREAD_START_ROUTINE fn, LoadJob* job)
{
    HANDLE t = NULL;
    LONG r = ExCreateThread(&t, 0, NULL, NULL, fn, job, 2);
    if (r < 0 || !t)
    {
        Log("trainers: no system thread: %08X", (unsigned)r);
        return false;
    }
    // Made suspended: as Aurora (82361AD8-82361AF4), on hardware thread 3, its
    // priority 15, then started. (Not started, it never ran: 2026-10-15.)
    XSetThreadProcessor(t, 3);
    SetThreadPriority(t, THREAD_PRIORITY_TIME_CRITICAL);
    ResumeThread(t);
    WaitForSingleObject(t, 20000);
    CloseHandle(t);
    return true;
}

// The first bytes of a file (up to `size`).
bool ReadHead(const std::string& path, size_t size, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    out.resize(size);
    DWORD got = 0;
    bool ok = ::ReadFile(h, &out[0], (DWORD)size, &got, NULL) && got > 0;
    out.resize(got);
    CloseHandle(h);
    return ok;
}

// The title updates switched on (in a drive's Content; SlopDash keeps the
// ones switched off elsewhere): the highest number among Microsoft-named
// files (tu00000003_00000000), 0 if there are none, -1 if there are only
// files it can't tell the number of.
int ActiveTitleUpdate(unsigned titleId, unsigned mediaId)
{
    static const char* const kDrives[] = { "hdd:", "usb:" };
    int best = 0;
    bool unknown = false;
    for (int d = 0; d < 2; d++)
    {
        std::string content = std::string(kDrives[d]) + "\\Content";
        WIN32_FIND_DATAA owner;
        HANDLE o = FindFirstFileA((content + "\\*").c_str(), &owner);
        if (o == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if (!(owner.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            std::string dir = content + "\\" + owner.cFileName + "\\" + Hex(titleId) + "\\000B0000";
            WIN32_FIND_DATAA tu;
            HANDLE t = FindFirstFileA((dir + "\\*").c_str(), &tu);
            if (t == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                if (tu.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                int n = library::TitleUpdateNumber(tu.cFileName);
                if (n < 0)
                    unknown = true;
                else if (n > best)
                    best = n;
                Log("trainers: title update %s\\%s: %d", dir.c_str(), tu.cFileName, n);
            } while (FindNextFileA(t, &tu));
            FindClose(t);
        } while (FindNextFileA(o, &owner));
        FindClose(o);
    }
    // "Cache" title updates (<drive>:\Cache\TU_..., the form the No-Intro
    // set and Aurora's downloads keep): each one's header says its game,
    // release and number ("... Title Update #3"). Only those for this game's
    // own release count (another release's would never be applied).
    for (int d = 0; d < 2; d++)
    {
        std::string cache = std::string(kDrives[d]) + "\\Cache";
        WIN32_FIND_DATAA tu;
        HANDLE t = FindFirstFileA((cache + "\\TU_*").c_str(), &tu);
        if (t == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if (tu.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::vector<unsigned char> head;
            library::TitleUpdate u;
            std::string path = cache + "\\" + tu.cFileName;
            if (!ReadHead(path, 0x1000, head) || !library::ReadTitleUpdate(&head[0], head.size(), u) ||
                u.titleId != titleId || (mediaId && u.mediaId != mediaId))
                continue;
            if (u.number < 0)
                unknown = true;
            else if (u.number > best)
                best = u.number;
            Log("trainers: cache title update %s: %08X media %08X #%d", path.c_str(), u.titleId, u.mediaId, u.number);
        } while (FindNextFileA(t, &tu));
        FindClose(t);
    }
    return best == 0 && unknown ? -1 : best;
}

}  // namespace

trainer::GameVersion VersionOf(unsigned titleId)
{
    trainer::GameVersion v;
    v.titleId = titleId;
    const std::vector<library::Game>& games = AllGames();
    for (size_t i = 0; i < games.size(); i++)
    {
        if (games[i].titleId != titleId || games[i].path.empty())
            continue;
        // A package's header, or an executable's headers (the XEX header's
        // size is at 8).
        std::vector<unsigned char> head;
        library::Execution e;
        if (ReadHead(games[i].path, 0x1000, head) && head.size() >= 12 && !memcmp(&head[0], "XEX2", 4))
        {
            size_t need = ((size_t)head[8] << 24) | ((size_t)head[9] << 16) | ((size_t)head[10] << 8) | head[11];
            if (need > head.size() && need <= 0x100000)
                ReadHead(games[i].path, need, head);
        }
        if (!head.empty() && library::ReadExecution(&head[0], head.size(), e))
        {
            v.mediaId = e.mediaId;
            v.baseVersion = e.baseVersion;
            v.discNum = e.discNum;
            break;
        }
    }
    v.titleUpdate = ActiveTitleUpdate(titleId, v.mediaId);
    Log("trainers: %08X is media %08X, base %08X, disc %u, title update %d", titleId, v.mediaId, v.baseVersion,
        v.discNum, v.titleUpdate);
    return v;
}

std::string Folder(unsigned titleId)
{
    return "game:\\Trainers\\" + Hex(titleId);
}

std::vector<Found> Find(unsigned titleId)
{
    std::vector<Found> out;
    std::string dir = Folder(titleId);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    trainer::GameVersion game = VersionOf(titleId);
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        Found f;
        f.file = fd.cFileName;
        f.path = dir + "\\" + f.file;
        f.valid = false;
        std::vector<unsigned char> data;
        if (!ReadWhole(f.path, data))
            f.error = "can't be read";
        else
            f.valid = trainer::Read(&data[0], data.size(), f.info, f.error);
        if (!f.valid)
            Log("trainers: %s: %s", f.path.c_str(), f.error.c_str());
        f.fit = f.valid ? trainer::Check(f.info, game) : trainer::kOtherGame;
        // Another release of the game's (another media ID, base version or
        // disc): left out, as Aurora leaves it out.
        if (f.valid && f.fit == trainer::kOtherGame)
        {
            Log("trainers: %s is for another release of the game", f.path.c_str());
            continue;
        }
        out.push_back(f);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

std::string Chosen(unsigned titleId)
{
    return GetSetting(("trainer." + Hex(titleId)).c_str());
}

void Choose(unsigned titleId, const std::string& file)
{
    SetSetting(("trainer." + Hex(titleId)).c_str(), file);
}

unsigned Cheats(unsigned titleId, const Found& t)
{
    std::string v = GetSetting(CheatsKey(titleId, t.file).c_str());
    return v.empty() ? t.info.DefaultMask() : (unsigned)strtoul(v.c_str(), NULL, 16);
}

void SetCheats(unsigned titleId, const Found& t, unsigned mask)
{
    SetSetting(CheatsKey(titleId, t.file).c_str(), Hex(mask));
}

bool AnyLoaded()
{
    HANDLE m = NULL;
    return XexGetModuleHandle(kModuleName, &m) >= 0 && m;
}

bool LoadFor(unsigned titleId, std::wstring& why)
{
    std::string file = Chosen(titleId);
    if (file.empty())
        return true;
    std::string path = Folder(titleId) + "\\" + file;
    if (AnyLoaded())
    {
        // (as Aurora: one until the console restarts)
        Log("trainers: one is loaded already; %s not loaded", path.c_str());
        why = L"Trainer Not Loaded. Restart to Change It";
        return false;
    }
    std::vector<unsigned char> data;
    trainer::Info info;
    std::string error;
    if (!ReadWhole(path, data) || !trainer::Read(&data[0], data.size(), info, error))
    {
        Log("trainers: %s isn't a trainer: %s", path.c_str(), error.c_str());
        why = L"Trainer Not Loaded";
        return false;
    }
    // Only for the game as it is (Aurora's rule, trainer::Check): a trainer
    // for another release or title update patches the wrong code.
    trainer::Fit fit = trainer::Check(info, VersionOf(titleId));
    if (fit == trainer::kOtherGame || fit == trainer::kWrongUpdate)
    {
        Log("trainers: %s isn't for this version of the game (%d)", path.c_str(), (int)fit);
        why = fit == trainer::kOtherGame ? L"Trainer Not Loaded. Wrong Game Version"
                                         : L"Trainer Not Loaded. Needs " + info.UpdatesText();
        return false;
    }
    Found f;
    f.file = file;
    f.info = info;
    unsigned mask = Cheats(titleId, f);

    // Loaded from memory of the kind Aurora gives it (large pages).
    LoadJob job;
    job.size = (DWORD)data.size();
    job.status = -1;
    job.data = VirtualAlloc(NULL, job.size, MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    if (!job.data)
    {
        why = L"Trainer Not Loaded";
        return false;
    }
    memcpy(job.data, &data[0], job.size);
    job.module = NULL;
    OnSystemThread(LoadThread, &job);
    VirtualFree(job.data, 0, MEM_RELEASE);
    Log("trainers: load %s: %08X", path.c_str(), (unsigned)job.status);
    if (job.status < 0)
    {
        why = L"Trainer Not Loaded";
        return false;
    }
    HANDLE module = NULL;
    PVOID init = NULL, abort = NULL;
    XexGetModuleHandle(kModuleName, &module);
    if (module)
    {
        XexGetProcedureAddress(module, 2, &init);
        XexGetProcedureAddress(module, 3, &abort);
    }
    typedef BOOL (*InitFn)(DWORD tag, DWORD cheats, DWORD reserved);
    typedef void (*AbortFn)(DWORD tag);
    BOOL running = init ? ((InitFn)init)(kTag, mask, 0) : FALSE;
    Log("trainers: %S %S started with cheats %08X: %d", info.name.c_str(), info.version.c_str(), mask, (int)running);
    if (!running)
    {
        if (abort)
            ((AbortFn)abort)(kTag);
        if (module)
        {
            job.module = module;
            OnSystemThread(UnloadThread, &job);
        }
        why = L"Trainer Not Loaded";
        return false;
    }
    return true;
}

}  // namespace trainers
