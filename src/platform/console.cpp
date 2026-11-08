#include "platform/console.h"

#include <xtl.h>
#include <stdio.h>

#include "platform/log.h"

extern "C" {
LONG XexGetModuleHandle(PCHAR name, PHANDLE module);
LONG XexGetProcedureAddress(HANDLE module, DWORD ordinal, PVOID* address);
LONG XeKeysGetKey(WORD key, PVOID buffer, PDWORD size);
VOID XeKeysGetConsoleID(PBYTE id, PCHAR text);
VOID HalSendSMCMessage(PVOID command, PVOID reply);     // 16-byte buffers
VOID HalReturnToFirmware(DWORD routine);
}

namespace console {

std::string SerialNumber()
{
    // (1888's dash 92121034: key 0x14 into 12 bytes, ended at the 13th)
    char serial[13] = { 0 };
    DWORD size = 12;
    if (XeKeysGetKey(0x14, serial, &size) < 0)
        return "";
    serial[12] = 0;
    return serial;
}

std::string ConsoleId()
{
    // (1888's dash 92121060: no binary ID, the text into 12 bytes)
    char text[16] = { 0 };
    XeKeysGetConsoleID(NULL, text);
    text[12] = 0;
    return text;
}

std::wstring KernelVersion()
{
    // The kernel's XboxKrnlVersion (export 344): major, minor, build, QFE.
    HANDLE kernel = NULL;
    PVOID p = NULL;
    if (XexGetModuleHandle("xboxkrnl.exe", &kernel) < 0 || XexGetProcedureAddress(kernel, 344, &p) < 0 || !p)
        return L"";
    const WORD* v = (const WORD*)p;
    wchar_t b[32];
    _snwprintf_s(b, 32, _TRUNCATE, L"%u.%u.%u.%u", v[0], v[1], v[2], v[3]);
    return b;
}

bool Temperatures(Temps& t)
{
    // The system controller's command 0x07: four sensors, CPU, GPU, memory
    // and board, each 2 bytes, low byte first, in 1/256 degrees (libxenon's
    // xenon_smc_query_sensors). 1888 itself never asked (its xam's only SMC
    // call is the disc tray's).
    BYTE ask[16] = { 0x07 }, reply[16] = { 0 };
    HalSendSMCMessage(ask, reply);
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        Log("smc temperatures: %02X %02X%02X %02X%02X %02X%02X %02X%02X", reply[0], reply[1], reply[2], reply[3],
            reply[4], reply[5], reply[6], reply[7], reply[8]);
    }
    if (reply[0] != 0x07)
        return false;
    float* out[4] = { &t.cpu, &t.gpu, &t.memory, &t.board };
    for (int i = 0; i < 4; i++)
    {
        *out[i] = (reply[1 + i * 2] | (reply[2 + i * 2] << 8)) / 256.0f;
        if (*out[i] < 1.0f || *out[i] > 150.0f)         // (not a reading)
            return false;
    }
    return true;
}

std::wstring IpAddress()
{
    XNADDR addr;
    ZeroMemory(&addr, sizeof(addr));
    DWORD got = XNetGetTitleXnAddr(&addr);
    if (got == XNET_GET_XNADDR_PENDING || !addr.ina.s_addr)
        return L"";
    wchar_t b[20];
    _snwprintf_s(b, 20, _TRUNCATE, L"%u.%u.%u.%u", addr.ina.S_un.S_un_b.s_b1, addr.ina.S_un.S_un_b.s_b2,
                 addr.ina.S_un.S_un_b.s_b3, addr.ina.S_un.S_un_b.s_b4);
    return b;
}

bool DebugMonitorOn()
{
    HANDLE m = NULL;
    return XexGetModuleHandle("xbdm.xex", &m) >= 0 && m;
}

bool Space(const char* root, ULONGLONG& freeBytes, ULONGLONG& total)
{
    ULARGE_INTEGER f, t;
    if (!GetDiskFreeSpaceExA(root, &f, &t, NULL))
        return false;
    freeBytes = f.QuadPart;
    total = t.QuadPart;
    return true;
}

bool LowOnSpace(const char* root)
{
    ULONGLONG f = 0, t = 0;
    return Space(root, f, t) && f < kLowSpace;
}

// XNotifyQueueUI (xam 0x290), as profiles.cpp uses it:
// type 14 shows the text with a happy face, 15 with a sad one.
typedef void (*XNotifyQueueUIFn)(DWORD type, DWORD user, ULONGLONG priority, WCHAR* text, PVOID context);

void Notify(const std::wstring& text, bool warning)
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
    static WCHAR buf[128];              // xam may read it after the call returns
    if (!fn)
        return;
    wcsncpy_s(buf, 128, text.c_str(), _TRUNCATE);
    fn(warning ? 15 : 14, XUSER_INDEX_ANY, 2, buf, NULL);       // 2: high priority
    Log("toast: %S", text.c_str());
}

void CheckSpace()
{
    static bool wasLow = false;
    ULONGLONG f = 0, t = 0;
    if (!Space("hdd:\\", f, t))
        return;
    bool low = f < kLowSpace;
    if (low && !wasLow)
    {
        wchar_t b[64];
        _snwprintf_s(b, 64, _TRUNCATE, L"Hard Drive Almost Full: %.1f GB Free", f / 1073741824.0);
        Notify(b, true);
    }
    wasLow = low;
}

void Restart()
{
    Log("console: restarting");
    LogFlush();
    HalReturnToFirmware(1);
}

void TurnOff()
{
    Log("console: turning off");
    LogFlush();
    HalReturnToFirmware(5);
}

void RestartDashboard()
{
    Log("console: restarting SlopDash");
    LogFlush();
    XLaunchNewImage("game:\\SlopDash.xex", 0);
}

}  // namespace console
