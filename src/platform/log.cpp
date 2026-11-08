#include "platform/log.h"

#include <xtl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

// The log survives a power cut. Appending grows the file, and the new size is
// only written to the USB stick's FAT later, so a crash lost the last lines
// (0.0.17). Instead the file is created at a fixed size, filled with spaces,
// and each line is written in place and flushed: the size never changes, so
// every flushed line is on the stick. Read it by trimming the trailing spaces.
//
// Written and flushed by the caller, a line cost 3.5 ms (2026-10-15: opening a
// list screen logged six, the blades log every focus change), so lines wait
// in memory for a writer thread of their own (a later hardware thread, below
// normal priority). LogFlush writes them at once, for when SlopDash ends.

#ifdef SLOPDASH_LOCAL
#include "../../local/log_ring.inc"
#endif

namespace {

const DWORD kLogSize = 256 * 1024;
HANDLE g_file = INVALID_HANDLE_VALUE;
DWORD g_pos = 0;
bool g_opened = false;

CRITICAL_SECTION g_lock;            // the ring and the pending lines
CRITICAL_SECTION g_fileLock;        // the file (the writer, or LogFlush)
struct LockInit
{
    LockInit()
    {
        InitializeCriticalSection(&g_lock);
        InitializeCriticalSection(&g_fileLock);
    }
} g_lockInit;
std::string g_pending;              // lines (each ending "\r\n") not on the stick yet
HANDLE g_wake = NULL;
bool g_writer = false;

void Open()
{
    g_opened = true;
    // The previous run's log is kept (a relaunch over a running build would
    // otherwise lose it), and the one before: a theme switch relaunches
    // SlopDash, so the run before it would be gone by the time it's read.
    DeleteFileA("game:\\SlopDash.prev2.log");
    MoveFileA("game:\\SlopDash.prev.log", "game:\\SlopDash.prev2.log");
    MoveFileA("game:\\SlopDash.log", "game:\\SlopDash.prev.log");
    g_file = CreateFileA("game:\\SlopDash.log", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_file == INVALID_HANDLE_VALUE)
        return;
    static char blank[4096];
    memset(blank, ' ', sizeof(blank));
    DWORD written = 0;
    for (DWORD done = 0; done < kLogSize; done += sizeof(blank))
        WriteFile(g_file, blank, sizeof(blank), &written, NULL);
    FlushFileBuffers(g_file);
    SetFilePointer(g_file, 0, NULL, FILE_BEGIN);
}

// The pending lines onto the stick (and the debug output), in one write.
void WritePending()
{
    std::string batch;
    EnterCriticalSection(&g_lock);
    batch.swap(g_pending);
    LeaveCriticalSection(&g_lock);
    if (batch.empty())
        return;
    EnterCriticalSection(&g_fileLock);
    OutputDebugStringA(batch.c_str());
    if (g_file != INVALID_HANDLE_VALUE)
    {
        DWORD room = g_pos < kLogSize ? kLogSize - g_pos : 0;
        DWORD n = (DWORD)batch.size() < room ? (DWORD)batch.size() : room;
        DWORD written = 0;
        if (n)
        {
            WriteFile(g_file, batch.data(), n, &written, NULL);
            FlushFileBuffers(g_file);
            g_pos += written;
        }
    }
    LeaveCriticalSection(&g_fileLock);
}

DWORD WINAPI Writer(LPVOID)
{
    for (;;)
    {
        WaitForSingleObject(g_wake, 250);
        Sleep(40);                  // (the lines logged together, written together)
        WritePending();
    }
}

}  // namespace

void Log(const char* fmt, ...)
{
    char line[1024];
    va_list args;
    va_start(args, fmt);
    int n = _vsnprintf_s(line, sizeof(line) - 2, _TRUNCATE, fmt, args);
    va_end(args);
    if (n < 0)
        n = (int)strlen(line);
    line[n] = '\r';
    line[n + 1] = '\n';
    bool start = false;
    EnterCriticalSection(&g_lock);
#ifdef SLOPDASH_LOCAL
#include "../../local/log_ring_write.inc"
#endif
    g_pending.append(line, n + 2);
    if (!g_opened)
    {
        Open();                     // (the first line: on the caller's thread, once)
        start = true;
    }
    LeaveCriticalSection(&g_lock);
    if (start)
    {
        g_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
        HANDLE t = CreateThread(NULL, 0, Writer, NULL, CREATE_SUSPENDED, NULL);
        if (t)
        {
            XSetThreadProcessor(t, 4);
            SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL);
            ResumeThread(t);
            CloseHandle(t);
            g_writer = true;
        }
    }
    if (!g_writer)
        WritePending();             // (no thread: as before, at once)
    else if (g_wake)
        SetEvent(g_wake);
}

void LogFlush()
{
    WritePending();
}
