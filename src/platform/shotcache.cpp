#include "platform/shotcache.h"

#include <xtl.h>
#include <d3dx9.h>
#include <map>

#include "platform/log.h"
#include "platform/scanner.h"

namespace {

// Launch's screenshot box on the screen (344 x 193 design units, at 1.632
// pixels a unit): made this size, DXT1 (a multiple of 4 each way).
const UINT kShotW = 560, kShotH = 316;
const size_t kMostTextures = 96;        // (~8.5 MB at most)

CRITICAL_SECTION g_lock;
volatile LONG g_made = 0;
HANDLE g_thread = NULL, g_wake = NULL;
IDirect3DDevice9* g_device = NULL;

std::vector<PrefetchJob> g_jobs;
std::vector<bool> g_done, g_finished;   // (taken by the worker; and made)
int g_focus = 0;

struct Made
{
    IDirect3DTexture9* tex;
    UINT width, height;
};
std::map<std::wstring, Made> g_textures;    // by locator
std::map<unsigned, bool> g_content;         // by title ID

void Init()
{
    if (InterlockedCompareExchange(&g_made, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_lock);
        g_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
        InterlockedExchange(&g_made, 2);
    }
    while (InterlockedCompareExchange(&g_made, 2, 2) != 2)
        Sleep(0);
}

bool ReadAll(const std::string& path, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL), got = 0;
    bool ok = size && size < (16u << 20);
    if (ok)
    {
        out.resize(size);
        ok = ReadFile(h, &out[0], size, &got, NULL) && got == size;
    }
    CloseHandle(h);
    return ok;
}

// The JPEG as a texture of the shown size; NULL if it couldn't be (a fault in
// the decoder caught here: the crash filter's loop froze the console before).
IDirect3DTexture9* MakeTexture(IDirect3DDevice9* device, const unsigned char* data, UINT size)
{
    IDirect3DTexture9* tex = NULL;
    __try
    {
        if (FAILED(D3DXCreateTextureFromFileInMemoryEx(device, data, size, kShotW, kShotH, 1, 0, D3DFMT_DXT1,
                                                       D3DPOOL_MANAGED, D3DX_FILTER_LINEAR, D3DX_DEFAULT, 0, NULL,
                                                       NULL, &tex)))
            tex = NULL;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        tex = NULL;
    }
    return tex;
}

// The next game to do: the nearest to the highlight not done yet (-1 none).
int NextJob()
{
    int n = (int)g_jobs.size();
    if (g_focus >= n)
        g_focus = n - 1;
    if (g_focus < 0)
        g_focus = 0;
    for (int d = 0; d < n; d++)
    {
        int a = g_focus + d, b = g_focus - d;
        if (a >= 0 && a < n && !g_done[a])
            return a;
        if (b >= 0 && b < n && !g_done[b])
            return b;
    }
    return -1;
}

DWORD WINAPI Worker(LPVOID)
{
    for (;;)
    {
        EnterCriticalSection(&g_lock);
        int job = g_device ? NextJob() : -1;
        PrefetchJob j;
        if (job >= 0)
        {
            j = g_jobs[job];
            g_done[job] = true;
        }
        IDirect3DDevice9* device = g_device;
        bool haveShot = j.shot.empty() || g_textures.count(j.shot) || g_textures.size() >= kMostTextures;
        LeaveCriticalSection(&g_lock);
        if (job < 0)
        {
            WaitForSingleObject(g_wake, 1000);
            continue;
        }
        DWORD t0 = GetTickCount();
        EsrbOf(j.path, j.contentType);
        TitleHasContent(j.titleId);
        if (!haveShot)
        {
            // "file://game:/cache/<key>/shot.jpg": the file's own path
            std::string path;
            for (size_t i = 7; i < j.shot.size(); i++)
                path += j.shot[i] == L'/' ? '\\' : (char)j.shot[i];
            std::vector<unsigned char> data;
            IDirect3DTexture9* tex = ReadAll(path, data) ? MakeTexture(device, &data[0], (UINT)data.size()) : NULL;
            EnterCriticalSection(&g_lock);
            if (tex && !g_textures.count(j.shot))
            {
                Made m = { tex, kShotW, kShotH };
                g_textures[j.shot] = m;
                tex = NULL;
            }
            LeaveCriticalSection(&g_lock);
            if (tex)
                tex->Release();
        }
        EnterCriticalSection(&g_lock);
        if (job < (int)g_finished.size() && g_jobs.size() == g_finished.size())
            g_finished[job] = true;
        LeaveCriticalSection(&g_lock);
        static int logged = 0;
        if (++logged <= 3)
            Log("prefetch: game %d in %u ms", job, (unsigned)(GetTickCount() - t0));
        Sleep(1);
    }
}

}  // namespace

void PrefetchSetDevice(IDirect3DDevice9* device)
{
    if (g_device || !device)
        return;
    Init();
    EnterCriticalSection(&g_lock);
    g_device = device;
    LeaveCriticalSection(&g_lock);
    SetEvent(g_wake);
}

void PrefetchStart(const std::vector<PrefetchJob>& jobs, int focus)
{
    Init();
    EnterCriticalSection(&g_lock);
    g_jobs = jobs;
    g_done.assign(jobs.size(), false);
    g_finished.assign(jobs.size(), false);
    g_focus = focus;
    if (!g_thread)
    {
        g_thread = CreateThread(NULL, 64 * 1024, Worker, NULL, CREATE_SUSPENDED, NULL);
        if (g_thread)
        {
            XSetThreadProcessor(g_thread, 5);   // (away from the UI's hardware thread, and the fetcher's 4)
            SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
            ResumeThread(g_thread);
        }
    }
    LeaveCriticalSection(&g_lock);
    SetEvent(g_wake);
    Log("prefetch: %u games, from %d", (unsigned)jobs.size(), focus);
}

void PrefetchFocus(int focus)
{
    if (g_made != 2)
        return;
    EnterCriticalSection(&g_lock);
    g_focus = focus;
    LeaveCriticalSection(&g_lock);
}

bool PrefetchFinished(int job)
{
    if (g_made != 2)
        return false;
    EnterCriticalSection(&g_lock);
    bool done = job >= 0 && job < (int)g_finished.size() && g_finished[job];
    LeaveCriticalSection(&g_lock);
    return done;
}

bool PrefetchedTexture(const wchar_t* locator, IDirect3DTexture9** tex, UINT* width, UINT* height)
{
    if (g_made != 2 || !locator || !tex)
        return false;
    EnterCriticalSection(&g_lock);
    std::map<std::wstring, Made>::iterator it = g_textures.find(locator);
    bool have = it != g_textures.end();
    if (have)
    {
        *tex = it->second.tex;
        (*tex)->AddRef();
        if (width)
            *width = it->second.width;
        if (height)
            *height = it->second.height;
    }
    LeaveCriticalSection(&g_lock);
    return have;
}

bool TitleHasContent(unsigned titleId)
{
    Init();
    EnterCriticalSection(&g_lock);
    std::map<unsigned, bool>::iterator it = g_content.find(titleId);
    bool known = it != g_content.end(), has = known && it->second;
    LeaveCriticalSection(&g_lock);
    if (known)
        return has;
    static const char* const kDrives[] = { "hdd:", "usb:" };
    for (int d = 0; d < 2 && titleId && !has; d++)
    {
        char path[96];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\Content\\0000000000000000\\%08X\\00000002\\*", kDrives[d],
                    titleId);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(path, &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
                has = !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            while (!has && FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    EnterCriticalSection(&g_lock);
    g_content[titleId] = has;
    LeaveCriticalSection(&g_lock);
    return has;
}
