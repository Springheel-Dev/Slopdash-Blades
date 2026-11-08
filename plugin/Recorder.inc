// The screen recorder: the screen, games included, to the Media section, run
// from a Guide section; video only for now. Included by SlopGuide.cpp:
// SlopGuide is loaded system-wide, so
// what's on the TV can be read in a game as on the dashboard.
//
// The screen: XBDM's own "screenshot" (xbdm.xex 17559, 9100D5D0) shows how -
// the display controller's primary surface address (GPU register 7FC86110,
// physical) seen through the first of 0xC0000000+, 0xA0000000+ and
// 0xE0000000-0x1000+ that MmIsAddressValid takes; its size from
// VdGetCurrentDisplayInformation (front buffer width and height, the first
// two halfwords); its format from 7FC86104's low 3 bits (0: 8:8:8:8, read as
// B, G, R, X; else 2:10:10:10, the games' format); the pixels in the GPU's
// tiled layout (xgraphics.h XGAddress2DTiledOffset).
// XBDM stops the title while it copies; the recorder reads it as it is.
//
// Recorded as Motion-JPEG AVI (src/core/mjpeg.cpp: PC players and SlopDash's
// Media > Recordings) to Hdd:\SlopDash\Recordings (SlopDashPics:).

#include <new>
#include "../src/core/mjpeg.cpp"

extern "C" BOOLEAN MmIsAddressValid(PVOID address);
extern "C" VOID VdGetCurrentDisplayInformation(PVOID info);

namespace recorder {

const int kWidth = 640, kHeight = 360;      // what's recorded (scaled from the front buffer)

struct Screen
{
    const BYTE* pixels;                     // the front buffer, tiled
    int width, height;                      // its size
    bool tenBit;                            // 2:10:10:10 (else 8:8:8:8)
};

bool FindScreen(Screen& s)
{
    DWORD phys = *(volatile DWORD*)0x7FC86110;
    if (!phys)
        return false;
    DWORD at = 0xC0000000 + phys;
    if (!MmIsAddressValid((PVOID)at))
    {
        at = 0xA0000000 + phys;
        if (!MmIsAddressValid((PVOID)at))
            at = 0xE0000000 + phys - 0x1000;
    }
    __declspec(align(16)) BYTE info[0x100];
    ZeroMemory(info, sizeof(info));
    VdGetCurrentDisplayInformation(info);
    s.width = *(WORD*)&info[0];
    s.height = *(WORD*)&info[2];
    s.tenBit = (*(volatile BYTE*)0x7FC86104 & 7) != 0;
    s.pixels = (const BYTE*)at;
    return s.width > 0 && s.height > 0 && MmIsAddressValid((PVOID)at);
}

// xgraphics.h XGAddress2DTiledOffset, 32-bit texels.
inline DWORD Tiled(DWORD x, DWORD y, DWORD width)
{
    DWORD aligned = (width + 31) & ~31u;
    DWORD macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << 9;
    DWORD micro = ((x & 7) + ((y & 6) << 2)) << 2;
    DWORD off = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << 5) + ((y & 1) << 4);
    return ((((off & ~511u) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) +
             (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> 2);
}

// The screen scaled to w x h, RGB (each output pixel the source pixel at its
// centre: fast, and sharp enough for 640 x 360 from 1280 x 720).
void Grab(const Screen& s, BYTE* rgb, int w, int h)
{
    for (int y = 0; y < h; y++)
    {
        DWORD sy = (DWORD)((y * 2 + 1) * s.height / (h * 2));
        BYTE* out = rgb + y * w * 3;
        for (int x = 0; x < w; x++)
        {
            DWORD sx = (DWORD)((x * 2 + 1) * s.width / (w * 2));
            const BYTE* p = s.pixels + Tiled(sx, sy, s.width) * 4;
            if (s.tenBit)
            {
                // A2 R10 G10 B10 in a little-endian word: the top 8 bits of each.
                DWORD v = p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24);
                out[0] = (BYTE)(v >> 22);
                out[1] = (BYTE)(v >> 12);
                out[2] = (BYTE)(v >> 2);
            }
            else
            {
                out[0] = p[2];
                out[1] = p[1];
                out[2] = p[0];
            }
            out += 3;
        }
    }
}

// The scaled picture's buffer (690 KB), only while it's used. Not part of
// SlopGuide's image: there, it pushed SlopGuide over SlopDash's load address
// (0x82000000) and SlopDash couldn't start ("the game cannot be loaded",
// 0.90-0.92). Not malloc: a system DLL's heap is too small, and 0.93's
// capture quietly did nothing. The console's memory directly (XPhysicalAlloc).
BYTE* Buffer()
{
    BYTE* b = (BYTE*)XPhysicalAlloc(kWidth * kHeight * 3, MAXULONG_PTR, 0, PAGE_READWRITE);
    if (!b)
        Log("recorder: no memory for a frame (%u bytes)", (unsigned)(kWidth * kHeight * 3));
    return b;
}

bool WriteAll(const char* path, const void* data, DWORD size)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD put = 0;
    BOOL ok = WriteFile(h, data, size, &put, NULL) && put == size;
    CloseHandle(h);
    return ok != FALSE;
}

// One frame, as a JPEG (the capture's first test: remote command F0C0).
void Still()
{
    Screen s;
    if (!FindScreen(s))
    {
        Log("recorder: no screen (%08X)", (unsigned)*(volatile DWORD*)0x7FC86110);
        return;
    }
    BYTE* rgb = Buffer();
    if (!rgb)
        return;
    LARGE_INTEGER f, t0, t1, t2;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    Grab(s, rgb, kWidth, kHeight);
    QueryPerformanceCounter(&t1);
    std::vector<unsigned char> jpeg;
    mjpeg::Encode(rgb, kWidth, kHeight, kWidth * 3, 85, jpeg);
    QueryPerformanceCounter(&t2);
    CreateDirectoryA("SlopDashPics:\\Recordings", NULL);
    bool ok = WriteAll("SlopDashPics:\\Recordings\\capture.jpg", &jpeg[0], (DWORD)jpeg.size());
    Log("recorder: still %dx%d %s at %08X -> %u bytes %s (grab %.1f ms, encode %.1f ms)", s.width, s.height,
        s.tenBit ? "10-bit" : "8-bit", (unsigned)(DWORD)s.pixels, (unsigned)jpeg.size(), ok ? "saved" : "NOT saved",
        (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart, (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / f.QuadPart);
    XPhysicalFree(rgb);
}

// --- Recording ----------------------------------------------------------------------
// A thread of its own: a frame every 1/30 s (one that can't keep up is
// skipped, not queued: the clip stays in time), each read, scaled, encoded and
// appended to the AVI. Stopped: the index written, the header put right. A
// clip goes on in a new file every 15 minutes (each about 300 MB: well under
// FATX's 4 GB and AVI 1.0's 1 GB).
const int kFps = 30;
const DWORD kSplitFrames = kFps * 60 * 15;

// Everything a recording uses, taken once at its start from the console's
// memory (XPhysicalAlloc) and nothing while it runs: 0.95-1.01 grew vectors
// on the system heap SlopGuide shares (each frame's JPEG and chunk, the
// index), which slowed the recorder to about a frame a second and then hung
// it inside an allocation, Stop and all.
const DWORD kJpegMax = 512 * 1024;                  // a frame's chunk: its 8-byte head, the JPEG, a pad byte
const DWORD kIndexBytes = 8 + 16 * kSplitFrames;    // idx1: its head, 16 bytes a frame

volatile LONG g_recording = 0;          // asked for (Start/Stop)
volatile LONG g_recThread = 0;          // the thread running
volatile DWORD g_recFrames = 0;         // frames in the clip so far (all its files)
volatile DWORD g_recStarted = 0;        // GetTickCount at the start
volatile DWORD g_recSkipped = 0;        // frames that couldn't keep up
volatile DWORD g_recLastFrame = 0;      // GetTickCount at the last frame written
WCHAR g_recName[64];                    // the clip's name (its first file's)

struct Clip
{
    HANDLE file;
    mjpeg::AviWriter* avi;
    BYTE* index;                        // idx1, built as the frames are written
    char path[96];
};

// The clip's AviWriter, in place (no heap).
__declspec(align(16)) BYTE g_aviSpace[sizeof(mjpeg::AviWriter)];

void PutLe32(BYTE* p, DWORD v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
    p[2] = (BYTE)(v >> 16);
    p[3] = (BYTE)(v >> 24);
}

bool OpenClip(Clip& c, int part)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    CreateDirectoryA("SlopDashPics:\\Recordings", NULL);
    char name[64];
    _snprintf_s(name, sizeof(name), _TRUNCATE, "%04u-%02u-%02u %02u.%02u.%02u%s", t.wYear, t.wMonth, t.wDay,
                t.wHour, t.wMinute, t.wSecond, part ? " (more)" : "");
    _snprintf_s(c.path, sizeof(c.path), _TRUNCATE, "SlopDashPics:\\Recordings\\%s.avi", name);
    if (!part)
        _snwprintf_s(g_recName, ARRAYSIZE(g_recName), _TRUNCATE, L"%S", name);
    c.file = CreateFileA(c.path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (c.file == INVALID_HANDLE_VALUE)
    {
        Log("recorder: can't make %s (%u)", c.path, (unsigned)GetLastError());
        return false;
    }
    c.avi = new (g_aviSpace) mjpeg::AviWriter(kWidth, kHeight, kFps);
    BYTE head[mjpeg::AviWriter::kHeaderSize];
    DWORD put = 0;
    WriteFile(c.file, head, (DWORD)c.avi->Header(head), &put, NULL);
    Log("recorder: recording to %s", c.path);
    return true;
}

void CloseClip(Clip& c)
{
    if (c.file == INVALID_HANDLE_VALUE)
        return;
    DWORD frames = (DWORD)c.avi->Frames(), put = 0;
    memcpy(c.index, "idx1", 4);
    PutLe32(c.index + 4, 16 * frames);
    WriteFile(c.file, c.index, 8 + 16 * frames, &put, NULL);
    BYTE head[mjpeg::AviWriter::kHeaderSize];
    DWORD size = (DWORD)c.avi->Header(head);
    SetFilePointer(c.file, 0, NULL, FILE_BEGIN);
    WriteFile(c.file, head, size, &put, NULL);
    CloseHandle(c.file);
    Log("recorder: %s: %u frames, %u bytes", c.path, (unsigned)frames, (unsigned)(c.avi->MovieBytes() + size));
    c.avi->~AviWriter();
    c.avi = NULL;
    c.file = INVALID_HANDLE_VALUE;
}

DWORD WINAPI RecordThread(LPVOID)
{
    BYTE* rgb = Buffer();
    BYTE* chunk = (BYTE*)XPhysicalAlloc(kJpegMax, MAXULONG_PTR, 0, PAGE_READWRITE);
    BYTE* index = (BYTE*)XPhysicalAlloc(kIndexBytes, MAXULONG_PTR, 0, PAGE_READWRITE);
    Clip clip = { INVALID_HANDLE_VALUE, NULL, index };
    int part = 0;
    if (!rgb || !chunk || !index)
    {
        Log("recorder: no memory to record (%u bytes)", (unsigned)(kWidth * kHeight * 3 + kJpegMax + kIndexBytes));
        g_recording = 0;
    }
    else if (!OpenClip(clip, part))
        g_recording = 0;
    LARGE_INTEGER f, start, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&start);
    double grabMs = 0, encodeMs = 0, writeMs = 0;   // this stretch's, for the log every 2 s
    DWORD stretch = 0, logged = GetTickCount();
    DWORD next = 0, partFrames = 0;     // the next frame due (in frames from the start)
    while (g_recording)
    {
        QueryPerformanceCounter(&now);
        DWORD due = (DWORD)((now.QuadPart - start.QuadPart) * kFps / f.QuadPart);
        if (due < next)
        {
            Sleep(2);
            continue;
        }
        if (due > next)
            g_recSkipped += due - next; // (behind: those frames are gone, not queued)
        next = due + 1;
        Screen s;
        if (!FindScreen(s))
            continue;
        LARGE_INTEGER a, b, c, d;
        QueryPerformanceCounter(&a);
        Grab(s, rgb, kWidth, kHeight);
        QueryPerformanceCounter(&b);
        // The chunk in one piece: '00dc', the size, the JPEG, a pad byte if odd.
        size_t size = mjpeg::Encode(rgb, kWidth, kHeight, kWidth * 3, 75, chunk + 8, kJpegMax - 9);
        QueryPerformanceCounter(&c);
        if (!size)
        {
            g_recSkipped++;             // (too big for its buffer: not seen at 640 x 360)
            continue;
        }
        memcpy(chunk, "00dc", 4);
        PutLe32(chunk + 4, (DWORD)size);
        DWORD total = (DWORD)(8 + size);
        if (size & 1)
            chunk[total++] = 0;
        DWORD put = 0;
        if (!WriteFile(clip.file, chunk, total, &put, NULL) || put != total)
        {
            Log("recorder: write failed (%u): stopping", (unsigned)GetLastError());
            break;
        }
        BYTE* entry = index + 8 + 16 * partFrames;
        memcpy(entry, "00dc", 4);
        PutLe32(entry + 4, 0x10);                       // AVIIF_KEYFRAME
        PutLe32(entry + 8, clip.avi->Count(size));
        PutLe32(entry + 12, (DWORD)size);
        QueryPerformanceCounter(&d);
        grabMs += (double)(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
        encodeMs += (double)(c.QuadPart - b.QuadPart) * 1000.0 / f.QuadPart;
        writeMs += (double)(d.QuadPart - c.QuadPart) * 1000.0 / f.QuadPart;
        stretch++;
        g_recFrames++;
        g_recLastFrame = GetTickCount();
        if (g_recLastFrame - logged >= 2000)
        {
            Log("recorder: %u frames (%u skipped); a frame: grab %.1f ms, encode %.1f ms, write %.1f ms",
                (unsigned)g_recFrames, (unsigned)g_recSkipped, grabMs / stretch, encodeMs / stretch, writeMs / stretch);
            grabMs = encodeMs = writeMs = 0;
            stretch = 0;
            logged = g_recLastFrame;
        }
        if (++partFrames >= kSplitFrames)
        {
            CloseClip(clip);
            partFrames = 0;
            if (!OpenClip(clip, ++part))
                break;
        }
    }
    CloseClip(clip);
    Log("recorder: stopped: %u frames, %u skipped", (unsigned)g_recFrames, (unsigned)g_recSkipped);
    if (rgb)
        XPhysicalFree(rgb);
    if (chunk)
        XPhysicalFree(chunk);
    if (index)
        XPhysicalFree(index);
    g_recording = 0;
    g_recThread = 0;
    return 0;
}

void Start()
{
    if (InterlockedCompareExchange(&g_recThread, 1, 0) != 0)
    {
        Log("recorder: the last recording hasn't finished");
        return;
    }
    g_recFrames = g_recSkipped = 0;
    g_recStarted = g_recLastFrame = GetTickCount();
    g_recording = 1;
    HANDLE t = NULL;
    if (NT_OK(StartThread(&t, RecordThread, NULL)) && t)
        CloseHandle(t);
    else
    {
        g_recording = 0;
        g_recThread = 0;
        Log("recorder: no thread");
    }
}

// Stop takes effect at once for the Guide (Recording() is false, Stop greys
// out) even if the thread is still finishing the file.
void Stop()
{
    g_recording = 0;
}

bool Recording()
{
    return g_recording != 0;
}

// Recording, but no frame for 5 s: the panel says so.
bool Failed()
{
    return g_recording && GetTickCount() - g_recLastFrame > 5000;
}

// --- Screenshots ------------------------------------------------
// The screen as the Guide opens - the front buffer copied as it is (tiled),
// before the Guide draws over it - kept while the Guide is open. The panel's
// Screenshot tile saves it, full size, as a JPEG:
// Hdd:\SlopDash\Screenshots\<TID>\<TID>YYYYMMDDhhmmssfff.jpg (SlopDashPics:),
// where SlopDash's Screenshots (Media > Pictures, a game's menu) look.
BYTE* g_shotRaw = NULL;             // the copy (XPhysicalAlloc), while the Guide is open
DWORD g_shotRawSize = 0;
Screen g_shot;                      // its size and format; pixels = g_shotRaw
DWORD g_shotTitle = 0;
volatile LONG g_shotBusy = 0;       // being saved
volatile LONG g_shotRelease = 0;    // the Guide closed while it was: freed after
volatile DWORD g_shotSaidAt = 0;    // the panel's line says how it went, for a few seconds
volatile LONG g_shotSaved = 0;

void FreeFrame()
{
    if (g_shotRaw)
        XPhysicalFree(g_shotRaw);
    g_shotRaw = NULL;
    g_shotRawSize = 0;
}

// The Guide opening (NotifyThread, at once): the screen kept.
void HoldFrame(DWORD titleId)
{
    if (g_shotBusy)
        return;
    Screen s;
    if (!FindScreen(s))
        return;
    DWORD size = ((s.width + 31) & ~31u) * ((s.height + 31) & ~31u) * 4;
    if (g_shotRaw && g_shotRawSize < size)
        FreeFrame();
    if (!g_shotRaw)
    {
        g_shotRaw = (BYTE*)XPhysicalAlloc(size, MAXULONG_PTR, 0, PAGE_READWRITE);
        if (!g_shotRaw)
        {
            Log("screenshot: no memory for the screen (%u bytes)", (unsigned)size);
            return;
        }
        g_shotRawSize = size;
    }
    memcpy(g_shotRaw, s.pixels, size);
    g_shot = s;
    g_shot.pixels = g_shotRaw;
    g_shotTitle = titleId;
}

// The Guide closing: the copy let go (after the save, if one is running).
void ReleaseFrame()
{
    if (InterlockedCompareExchange(&g_shotBusy, 1, 0) == 0)
    {
        FreeFrame();
        g_shotBusy = 0;
    }
    else
        g_shotRelease = 1;
}

DWORD WINAPI ShotThread(LPVOID)
{
    int w = g_shot.width, h = g_shot.height;
    const DWORD jpegMax = 2 * 1024 * 1024;
    BYTE* rgb = (BYTE*)XPhysicalAlloc(w * h * 3, MAXULONG_PTR, 0, PAGE_READWRITE);
    BYTE* jpeg = (BYTE*)XPhysicalAlloc(jpegMax, MAXULONG_PTR, 0, PAGE_READWRITE);
    bool ok = false;
    if (rgb && jpeg)
    {
        Grab(g_shot, rgb, w, h);
        size_t n = mjpeg::Encode(rgb, w, h, w * 3, 90, jpeg, jpegMax);
        SYSTEMTIME t;
        GetLocalTime(&t);
        char dir[96], path[160];
        _snprintf_s(dir, sizeof(dir), _TRUNCATE, "SlopDashPics:\\Screenshots\\%08X", (unsigned)g_shotTitle);
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%08X%04u%02u%02u%02u%02u%02u%03u.jpg", dir,
                    (unsigned)g_shotTitle, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                    t.wMilliseconds);
        CreateDirectoryA("SlopDashPics:\\Screenshots", NULL);
        CreateDirectoryA(dir, NULL);
        ok = n && WriteAll(path, jpeg, (DWORD)n);
        Log("screenshot: %s, %d x %d, %u bytes: %s", path, w, h, (unsigned)n, ok ? "saved" : "FAILED");
    }
    else
        Log("screenshot: no memory to save it");
    if (rgb)
        XPhysicalFree(rgb);
    if (jpeg)
        XPhysicalFree(jpeg);
    g_shotSaved = ok ? 1 : 0;
    g_shotSaidAt = GetTickCount();
    if (InterlockedExchange(&g_shotRelease, 0))
        FreeFrame();
    g_shotBusy = 0;
    return 0;
}

// The panel's Screenshot tile.
void TakeScreenshot()
{
    if (!g_shotRaw)
    {
        g_shotSaved = 0;
        g_shotSaidAt = GetTickCount();
        Log("screenshot: no screen kept");
        return;
    }
    if (InterlockedCompareExchange(&g_shotBusy, 1, 0) != 0)
        return;
    if (!g_shotRaw)
    {
        g_shotBusy = 0;                 // (let go as the Guide closed)
        return;
    }
    HANDLE t = NULL;
    if (NT_OK(StartThread(&t, ShotThread, NULL)) && t)
        CloseHandle(t);
    else
        g_shotBusy = 0;
}

// What the panel's line says just after one (NULL: nothing to say).
LPCWSTR ShotLine()
{
    if (!g_shotSaidAt || GetTickCount() - g_shotSaidAt > 3000)
        return g_shotBusy ? L"Saving Screenshot" : NULL;
    return g_shotSaved ? L"Screenshot Saved" : L"Cannot Take Screenshot";
}

}  // namespace recorder
